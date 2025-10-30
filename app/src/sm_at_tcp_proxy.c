/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/posix/sys/eventfd.h>
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_socket.h"
#include "sm_at_tcp_proxy.h"

LOG_MODULE_REGISTER(sm_tcp, CONFIG_SM_LOG_LEVEL);

#define THREAD_STACK_SIZE	KB(4)
#define THREAD_PRIORITY		K_LOWEST_APPLICATION_THREAD_PRIO

/**@brief Proxy operations. */
enum sm_tcp_proxy_operation {
	CLIENT_DISCONNECT,
	CLIENT_CONNECT,
	CLIENT_CONNECT6
};

/**@brief Commands conveyed to threads. */
enum proxy_event {
	PROXY_CLOSE = 0,
	PROXY_EVENT_COUNT
};
K_MSGQ_DEFINE(proxy_event_queue, sizeof(enum proxy_event), PROXY_EVENT_COUNT, 1);

static struct k_thread tcp_thread;
static K_THREAD_STACK_DEFINE(tcp_thread_stack, THREAD_STACK_SIZE);

static struct tcp_proxy {
	int sock;		/* Socket descriptor. */
	int family;		/* Socket address family */
	sec_tag_t sec_tag;	/* Security tag of the credential */
	int peer_verify;	/* Peer verification level for TLS connection. */
	bool hostname_verify;	/* Verify hostname against the certificate. */
	int efd;		/* Event file descriptor for signaling threads. */
	int send_flags;         /* Send flags */
} proxy;

/** forward declaration of thread function **/
static void tcpcli_thread_func(void *p1, void *p2, void *p3);

static int do_tcp_proxy_close(void)
{
	int ret;
	const enum proxy_event event = PROXY_CLOSE;

	if (proxy.efd == INVALID_SOCKET) {
		return 0;
	}
	ret = k_msgq_put(&proxy_event_queue, &event, K_NO_WAIT);
	if (ret) {
		return ret;
	}
	ret = eventfd_write(proxy.efd, 1);
	if (ret < 0) {
		return -errno;
	}
	ret = k_thread_join(&tcp_thread, K_SECONDS(CONFIG_SM_TCP_POLL_TIME));
	if (ret) {
		LOG_WRN("Thread terminate failed: %d", ret);

		/* Attempt to make the thread exit by closing the sockets. */
		if (proxy.sock != INVALID_SOCKET) {
			zsock_close(proxy.sock);
			proxy.sock = INVALID_SOCKET;
		}
	}
	k_msgq_purge(&proxy_event_queue);
	zsock_close(proxy.efd);
	proxy.efd = INVALID_SOCKET;

	return ret;
}

static int do_tcp_client_connect(const char *url, uint16_t port, uint16_t cid)
{
	int ret;
	struct sockaddr sa;

	/* Open socket */
	if (proxy.sec_tag == SEC_TAG_TLS_INVALID) {
		ret = zsock_socket(proxy.family, SOCK_STREAM, IPPROTO_TCP);
	} else {
		ret = zsock_socket(proxy.family, SOCK_STREAM, IPPROTO_TLS_1_2);
	}
	if (ret < 0) {
		LOG_ERR("zsock_socket() failed: %d", -errno);
		return ret;
	}
	proxy.sock = ret;

	if (proxy.sec_tag != SEC_TAG_TLS_INVALID) {
		sec_tag_t sec_tag_list[1] = { proxy.sec_tag };

		ret = zsock_setsockopt(proxy.sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_list,
				 sizeof(sec_tag_t));
		if (ret) {
			LOG_ERR("zsock_setsockopt(TLS_SEC_TAG_LIST) error: %d", -errno);
			ret = -errno;
			goto exit_cli;
		}
		ret = zsock_setsockopt(proxy.sock, SOL_TLS, TLS_PEER_VERIFY, &proxy.peer_verify,
				 sizeof(proxy.peer_verify));
		if (ret) {
			LOG_ERR("zsock_setsockopt(TLS_PEER_VERIFY) error: %d", errno);
			ret = -errno;
			goto exit_cli;
		}
		if (proxy.hostname_verify) {
			ret = zsock_setsockopt(proxy.sock, SOL_TLS, TLS_HOSTNAME, url, strlen(url));
		} else {
			ret = zsock_setsockopt(proxy.sock, SOL_TLS, TLS_HOSTNAME, NULL, 0);
		}
		if (ret) {
			LOG_ERR("zsock_setsockopt(TLS_HOSTNAME) error: %d", errno);
			ret = -errno;
			goto exit_cli;
		}
	}

	/* Explicitly bind to a PDP context if necessary */
	if (cid > 0) {
		int cid_int = cid;

		ret = zsock_setsockopt(proxy.sock, SOL_SOCKET, SO_BINDTOPDN,
				&cid_int, sizeof(int));
		if (ret < 0) {
			LOG_ERR("zsock_setsockopt(SO_BINDTOPDN) error: %d", -errno);
			goto exit_cli;
		}
	}

	/* Connect to remote host */
	ret = util_resolve_host(0, url, port, proxy.family, &sa);
	if (ret) {
		goto exit_cli;
	}
	if (sa.sa_family == AF_INET) {
		ret = zsock_connect(proxy.sock, &sa, sizeof(struct sockaddr_in));
	} else {
		ret = zsock_connect(proxy.sock, &sa, sizeof(struct sockaddr_in6));
	}
	if (ret) {
		LOG_ERR("zsock_connect() failed: %d", -errno);
		ret = -errno;
		goto exit_cli;
	}

	proxy.efd = eventfd(0, 0);
	k_thread_create(&tcp_thread, tcp_thread_stack,
			K_THREAD_STACK_SIZEOF(tcp_thread_stack),
			tcpcli_thread_func, NULL, NULL, NULL,
			THREAD_PRIORITY, K_USER, K_NO_WAIT);

	rsp_send("\r\n#XTCPCLI: %d,\"connected\"\r\n", proxy.sock);

	return 0;

exit_cli:
	zsock_close(proxy.sock);
	proxy.sock = INVALID_SOCKET;
	rsp_send("\r\n#XTCPCLI: %d,\"not connected\"\r\n", ret);

	return ret;
}

static int do_tcp_send(const uint8_t *data, int datalen)
{
	int ret = 0;
	uint32_t offset = 0;

	while (offset < datalen) {
		ret = zsock_send(proxy.sock, data + offset, datalen - offset, proxy.send_flags);
		if (ret < 0) {
			LOG_ERR("zsock_send() failed: %d, sent: %d", -errno, offset);
			ret = -errno;
			break;
		} else {
			offset += ret;
		}
	}

	if (ret >= 0) {
		rsp_send("\r\n#XTCPSEND: %d\r\n", offset);
		return 0;
	}

	return ret;
}

static int do_tcp_send_datamode(const uint8_t *data, int datalen)
{
	int ret = 0;
	uint32_t offset = 0;

	while (offset < datalen) {
		ret = zsock_send(proxy.sock, data + offset, datalen - offset, proxy.send_flags);
		if (ret < 0) {
			LOG_ERR("zsock_send() failed: %d, sent: %d", -errno, offset);
			break;
		} else {
			offset += ret;
		}
	}

	return (offset > 0) ? offset : -1;
}

static int tcp_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int ret = 0;

	ARG_UNUSED(flags);

	if (op == DATAMODE_SEND) {
		ret = do_tcp_send_datamode(data, len);
		LOG_DBG("datamode send: %d", ret);
	} else if (op == DATAMODE_EXIT) {
		LOG_DBG("datamode exit");
		if ((flags & SM_DATAMODE_FLAGS_EXIT_HANDLER) != 0) {
			/* Datamode exited unexpectedly. */
			rsp_send(CONFIG_SM_DATAMODE_TERMINATOR);
		}
	}

	return ret;
}

/* TCP client thread */
static void tcpcli_thread_func(void *p1, void *p2, void *p3)
{
	enum {
		SOCK,
		EVENT_FD,
		FD_COUNT
	};

	int ret;
	struct zsock_pollfd fds[FD_COUNT];

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	fds[SOCK].fd = proxy.sock;
	fds[SOCK].events = ZSOCK_POLLIN;
	fds[EVENT_FD].fd = proxy.efd;
	fds[EVENT_FD].events = ZSOCK_POLLIN;
	while (true) {
		ret = zsock_poll(fds, ARRAY_SIZE(fds), MSEC_PER_SEC * CONFIG_SM_TCP_POLL_TIME);
		if (ret < 0) {
			LOG_WRN("zsock_poll() error: %d", ret);
			ret = -EIO;
			break;
		}
		if (ret == 0) {
			/* timeout */
			continue;
		}
		LOG_DBG("sock events 0x%08x", fds[SOCK].revents);
		LOG_DBG("efd events 0x%08x", fds[EVENT_FD].revents);
		if ((fds[SOCK].revents & ZSOCK_POLLIN) != 0) {
			while (true) {
				ret = zsock_recv(fds[SOCK].fd, (void *)sm_data_buf,
					sizeof(sm_data_buf), ZSOCK_MSG_DONTWAIT);
				/* No more data to receive */
				if ((ret == 0) || (ret < 0 && errno == EAGAIN)) {
					break;
				}
				/* Receive error */
				if (ret < 0) {
					LOG_WRN("recv() error: %d", -errno);
					break;
				}
				/* Data received */
				if (!in_datamode()) {
					rsp_send("\r\n#XTCPDATA: %d\r\n", ret);
				}
				data_send(sm_data_buf, ret);
			}
		}
		if ((fds[SOCK].revents & ZSOCK_POLLERR) != 0) {
			LOG_WRN("SOCK (%d): ZSOCK_POLLERR", fds[SOCK].fd);
			ret = -EIO;
			break;
		}
		if ((fds[SOCK].revents & ZSOCK_POLLNVAL) != 0) {
			LOG_WRN("SOCK (%d): ZSOCK_POLLNVAL", fds[SOCK].fd);
			ret = -ENETDOWN;
			break;
		}
		if ((fds[SOCK].revents & ZSOCK_POLLHUP) != 0) {
			/* Lose LTE connection / remote end close */
			LOG_WRN("SOCK (%d): ZSOCK_POLLHUP", fds[SOCK].fd);
			ret = -ECONNRESET;
			break;
		}
		/* Events from AT-commands. */
		if ((fds[EVENT_FD].revents & ZSOCK_POLLIN) != 0) {
			eventfd_t value;

			/* AT-command event can only close the client. */
			LOG_DBG("Close proxy");
			eventfd_read(fds[EVENT_FD].fd, &value);
			ret = 0;
			break;
		}
		if (fds[EVENT_FD].revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
			LOG_ERR("efd: unexpected event: %d", fds[EVENT_FD].revents);
			break;
		}
	}

	zsock_close(proxy.sock);
	proxy.sock = INVALID_SOCKET;

	if (in_datamode()) {
		exit_datamode_handler(ret);
	} else {
		rsp_send("\r\n#XTCPCLI: %d,\"disconnected\"\r\n", ret);
	}

	LOG_INF("TCP client thread terminated");
}

SM_AT_CMD_CUSTOM(xtcpcli, "AT#XTCPCLI", handle_at_tcp_client);
static int handle_at_tcp_client(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t op;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err) {
			return err;
		}
		if (op == CLIENT_CONNECT || op == CLIENT_CONNECT6) {
			uint16_t port;
			char url[SM_MAX_URL];
			int size = SM_MAX_URL;
			uint16_t cid = 0;  /* CID0 for initial PDN connection */

			if (proxy.sock != INVALID_SOCKET || proxy.efd != INVALID_SOCKET) {
				LOG_ERR("Proxy is running.");
				return -EINVAL;
			}
			err = util_string_get(parser, 2, url, &size);
			if (err) {
				return err;
			}
			if (at_parser_num_get(parser, 3, &port)) {
				return -EINVAL;
			}
			proxy.sec_tag = SEC_TAG_TLS_INVALID;
			if (param_count > 4) { /* optional param */
				err = at_parser_num_get(parser, 4, &proxy.sec_tag);
				if (err != 0 && err != -EOPNOTSUPP) {
					return -EINVAL;
				}
			}
			proxy.peer_verify = TLS_PEER_VERIFY_REQUIRED;
			if (param_count > 5) { /* optional param */
				err = at_parser_num_get(parser, 5, &proxy.peer_verify);
				if ((err != 0 && err != -EOPNOTSUPP) ||
				    (proxy.peer_verify != TLS_PEER_VERIFY_NONE &&
				     proxy.peer_verify != TLS_PEER_VERIFY_OPTIONAL &&
				     proxy.peer_verify != TLS_PEER_VERIFY_REQUIRED)) {
					return -EINVAL;
				}
			}
			proxy.hostname_verify = true;
			if (param_count > 6) { /* optional param */
				uint16_t hostname_verify = 0;

				err = at_parser_num_get(parser, 6, &hostname_verify);
				if ((err != 0 && err != -EOPNOTSUPP) ||
				    (hostname_verify != 0 && hostname_verify != 1)) {
					return -EINVAL;
				}
				proxy.hostname_verify = (bool)hostname_verify;
			}
			if (param_count > 7) { /* optional param, last */
				if (at_parser_num_get(parser, 7, &cid)) {
					return -EINVAL;
				}
			}

			proxy.family = (op == CLIENT_CONNECT) ? AF_INET : AF_INET6;
			err = do_tcp_client_connect(url, port, cid);
		} else if (op == CLIENT_DISCONNECT) {
			err = do_tcp_proxy_close();
		} break;

	case AT_PARSER_CMD_TYPE_READ:
		rsp_send("\r\n#XTCPCLI: %d,%d\r\n", proxy.sock, proxy.family);
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XTCPCLI: (%d,%d,%d),<url>,<port>,"
			 "<sec_tag>,<peer_verify>,<hostname_verify>,<cid>\r\n",
			 CLIENT_DISCONNECT, CLIENT_CONNECT, CLIENT_CONNECT6);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xtcpsend, "AT#XTCPSEND", handle_at_tcp_send);
static int handle_at_tcp_send(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	int err = -EINVAL;
	char data[SM_MAX_PAYLOAD_SIZE + 1] = {0};
	int size;
	bool datamode = false;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count > 1) {
			size = sizeof(data);
			err = util_string_get(parser, 1, data, &size);
			if (err == -ENODATA) {
				/* -ENODATA means data is empty so we go into datamode */
				datamode = true;
			} else if (err != 0) {
				return err;
			}
			if (param_count > 2) {
				err = at_parser_num_get(parser, 2, &proxy.send_flags);
				if (err) {
					return err;
				}
			}
		} else {
			datamode = true;
		}
		if (datamode) {
			err = enter_datamode(tcp_datamode_callback, 0);
		} else {
			err = do_tcp_send(data, size);
		}
		break;

	default:
		break;
	}

	return err;
}

/**@brief API to initialize TCP proxy AT commands handler
 */
int sm_at_tcp_proxy_init(void)
{
	proxy.sock	= INVALID_SOCKET;
	proxy.family	= AF_UNSPEC;
	proxy.sec_tag	= SEC_TAG_TLS_INVALID;
	proxy.efd	= INVALID_SOCKET;

	return 0;
}

/**@brief API to uninitialize TCP proxy AT commands handler
 */
int sm_at_tcp_proxy_uninit(void)
{
	return do_tcp_proxy_close();
}
