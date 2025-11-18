/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <nrf_socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/posix/sys/eventfd.h>
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_socket.h"
#include "sm_sockopt.h"

LOG_MODULE_REGISTER(sm_sock, CONFIG_SM_LOG_LEVEL);

#define SM_FDS_COUNT CONFIG_POSIX_OPEN_MAX
#define SM_MAX_SOCKET_COUNT (SM_FDS_COUNT - 1)

/**@brief Socket operations. */
enum sm_socket_operation {
	AT_SOCKET_OPEN = 0x1,
	AT_SOCKET_OPEN6
};

/**@brief Socketopt operations. */
enum sm_socketopt_operation {
	AT_SOCKETOPT_GET,
	AT_SOCKETOPT_SET
};

/**@brief Socket roles. */
enum sm_socket_role {
	AT_SOCKET_ROLE_CLIENT,
	AT_SOCKET_ROLE_SERVER
};

/**@brief Socket send modes. */
enum sm_socket_send_mode {
	AT_SOCKET_SEND_MODE_STRING = 0,	/* String data in command */
	AT_SOCKET_SEND_MODE_HEXSTRING = 1,	/* Hexadecimal string data */
	AT_SOCKET_SEND_MODE_DATA = 2	/* Enter data mode */
};

/**@brief Socket send result modes. */
enum sm_socket_send_result_mode {
	AT_SOCKET_SEND_RESULT_DEFAULT = 0, /* Data pushed to modem. */
	AT_SOCKET_SEND_RESULT_NW_ACK_URC = 1 /* URC from network acknowledgment will follow. */
};

/**@brief Socket receive modes. */
enum sm_socket_recv_mode {
	AT_SOCKET_RECV_MODE_BINARY = 0,	/* Receive binary data */
	AT_SOCKET_RECV_MODE_HEXSTRING = 1	/* Receive hexadecimal string data */
};

static char udp_url[SM_MAX_URL];
static uint16_t udp_port;

struct sm_async_poll {
	int events;        /* Events to poll for this socket. */
	atomic_t revents;  /* Events received for this socket. */
	int update_events; /* Events to update for this socket. */
	bool specific: 1;  /* Specific socket to poll. */
	bool disable: 1;   /* Poll needs to stay disabled for this socket. */
};

#define SM_MSG_SEND_ACK 0x2000
struct sm_send_ntf {
	atomic_t ready;    /* Notification received. */
	int status;        /* Send status. */
	size_t bytes_sent; /* Bytes sent. */
};

static struct sm_socket {
	int type;           /* SOCK_STREAM or SOCK_DGRAM */
	uint16_t role;      /* Client or Server */
	sec_tag_t sec_tag;  /* Security tag of the credential */
	int family;         /* Socket address family */
	int fd;             /* Socket descriptor. */
	int fd_peer;        /* Socket descriptor for peer. */
	uint16_t cid;       /* PDP Context ID, 0: primary; 1~10: secondary */
	int send_flags;     /* Send flags */
	bool send_cb_set;   /* Send callback set */
	struct sm_async_poll async_poll; /* Async poll info. */
	struct sm_send_ntf send_ntf; /* Send notification info. */
} socks[SM_MAX_SOCKET_COUNT];

static struct sm_socket *datamode_sock; /* Socket for data mode */
static char hex_data[1400 + 1]; /* Buffer for hex data conversion */

static struct async_poll_ctx {
	struct k_work poll_work; /* Work to send poll URCs. */
	int poll_events;         /* Events to poll for async poll. */
	bool poll_all: 1;        /* Poll all the sockets. */
	bool poll_running: 1;    /* Async poll is running. */
} poll_ctx;

/* forward declarations */
#define SOCKET_SEND_TMO_SEC 30

static void init_socket(struct sm_socket *socket)
{
	if (socket == NULL) {
		return;
	}

	socket->family = AF_UNSPEC;
	socket->sec_tag = SEC_TAG_TLS_INVALID;
	socket->role = AT_SOCKET_ROLE_CLIENT;
	socket->fd = INVALID_SOCKET;
	socket->fd_peer = INVALID_SOCKET;
	socket->cid = 0;
	socket->send_flags = 0;
	socket->send_cb_set = false;
	socket->send_ntf = (struct sm_send_ntf){0};
	socket->async_poll = (struct sm_async_poll){0};
}

static struct sm_socket *find_socket(int fd)
{
	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd == fd) {
			return &socks[i];
		}
	}

	return NULL;
}

static struct sm_socket *find_avail_socket(void)
{
	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd == INVALID_SOCKET) {
			return &socks[i];
		}
	}

	return NULL;
}

static int bind_to_pdn(struct sm_socket *sock)
{
	int ret = 0;

	if (sock->cid > 0) {
		int pdn_id = sm_util_pdn_id_get(sock->cid);

		if (pdn_id < 0) {
			return pdn_id;
		}

		ret = nrf_setsockopt(sock->fd, NRF_SOL_SOCKET, NRF_SO_BINDTOPDN, &pdn_id,
				     sizeof(int));
		if (ret < 0) {
			LOG_ERR("nrf_setsockopt(%d) error: %d", NRF_SO_BINDTOPDN, -errno);
			ret = -errno;
		}
	}

	return ret;
}

/* Called in IRQ context */
static void poll_cb(struct nrf_pollfd *pollfd)
{
	LOG_DBG("Poll event fd %d, revents 0x%x", pollfd->fd, pollfd->revents);

	struct sm_socket *sock = find_socket(pollfd->fd);

	if (sock == NULL) {
		LOG_DBG("Poll callback for unknown socket fd %d", pollfd->fd);
		return;
	}
	atomic_or(&sock->async_poll.revents, pollfd->revents);

	k_work_submit_to_queue(&sm_work_q, &poll_ctx.poll_work);
}


static int set_so_poll_cb(struct sm_socket *socket, short events)
{
	int err;

	if (socket == NULL) {
		return -EINVAL;
	}

	LOG_DBG("Set poll cb for socket %d, events %d", socket->fd, events);

	struct nrf_modem_pollcb pcb = {
		.callback = poll_cb,
		.events = events,
		.oneshot = true,
	};

	err = nrf_setsockopt(socket->fd, NRF_SOL_SOCKET, NRF_SO_POLLCB, &pcb, sizeof(pcb));
	if (err < 0) {
		LOG_ERR("nrf_setsockopt(%d,%d,%d) error: %d", socket->fd, NRF_SOL_SOCKET,
			NRF_SO_POLLCB, -errno);
		return -errno;
	}
	return 0;
}

static int clear_so_poll_cb(struct sm_socket *socket)
{
	int err;

	if (socket == NULL) {
		return -EINVAL;
	}

	err = nrf_setsockopt(socket->fd, NRF_SOL_SOCKET, NRF_SO_POLLCB, NULL, 0);
	if (err < 0) {
		LOG_ERR("nrf_setsockopt(%d,%d,%d) error: %d", socket->fd, NRF_SOL_SOCKET,
			NRF_SO_POLLCB, -errno);
		return -errno;
	}
	return 0;
}

static void poll_work_fn(struct k_work *work)
{
	if (poll_ctx.poll_running == false) {
		return;
	}

	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd == INVALID_SOCKET) {
			continue;
		}

		short revents = atomic_clear(&socks[i].async_poll.revents);

		if (revents) {
			urc_send("\r\n#XAPOLL: %d,%d\r\n", socks[i].fd, revents);
			if (revents & (NRF_POLLERR | NRF_POLLNVAL | NRF_POLLHUP)) {
				/* Remove socket from poll until closed. */
				socks[i].async_poll.disable = true;
			} else {
				/* Remove POLLIN/POLLOUT from poll, until AT-operation. */
				socks[i].async_poll.events &= ~revents;
			}
		}

		if (socks[i].async_poll.disable == false &&
		    (revents != 0 || socks[i].async_poll.update_events != 0)) {
			/* Re-register for remaining events */
			socks[i].async_poll.events |= socks[i].async_poll.update_events;
			socks[i].async_poll.update_events = 0;
			set_so_poll_cb(&socks[i], socks[i].async_poll.events);
		}
	}
}

static void delegate_poll_event(struct sm_socket *s, int events)
{
	if (poll_ctx.poll_running && (poll_ctx.poll_all || s->async_poll.specific)) {
		LOG_DBG("Delegate poll events %d for socket %d", events, s->fd);
		s->async_poll.update_events |= (poll_ctx.poll_events & events);

		k_work_submit_to_queue(&sm_work_q, &poll_ctx.poll_work);
	}
}

static void send_cb_fn(struct k_work *work)
{
	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd == INVALID_SOCKET) {
			continue;
		}
		if (atomic_get(&socks[i].send_ntf.ready) != 0) {
			int status = socks[i].send_ntf.status;
			int bytes_sent = socks[i].send_ntf.bytes_sent;

			atomic_clear(&socks[i].send_ntf.ready);
			if (status) {
				LOG_ERR("Send cb failed for socket %d: %d, %d", socks[i].fd,
					-status, bytes_sent);
				status = -1;
			}
			urc_send("\r\n#XSENDNTF: %d,%d,%d\r\n", socks[i].fd, status, bytes_sent);
			delegate_poll_event(&socks[i], NRF_POLLOUT);
		}
	}
}

/* Called in IRQ context */
static void send_cb(const struct nrf_modem_sendcb_params *params)
{
	static K_WORK_DEFINE(work, send_cb_fn);

	LOG_DBG("Send cb fd %d, status %d, bytes_sent %d",
		params->fd, params->status, params->bytes_sent);

	struct sm_socket *sock = find_socket(params->fd);

	if (sock == NULL) {
		LOG_DBG("Send callback for unknown socket fd %d", params->fd);
		return;
	}
	if (atomic_get(&sock->send_ntf.ready)) {
		LOG_ERR("Send notification pending for socket fd %d", params->fd);
		return;
	}
	sock->send_ntf.status = params->status;
	sock->send_ntf.bytes_sent = params->bytes_sent;
	atomic_set(&sock->send_ntf.ready, 1);

	k_work_submit_to_queue(&sm_work_q, &work);
}

static int set_so_send_cb(struct sm_socket *socket)
{
	int err;

	if (socket == NULL) {
		return -EINVAL;
	}
	if (socket->send_cb_set) {
		return 0;
	}

	LOG_DBG("Set send cb for socket %d", socket->fd);

	struct nrf_modem_sendcb pcb = {
		.callback = send_cb,
	};

	err = nrf_setsockopt(socket->fd, NRF_SOL_SOCKET, NRF_SO_SENDCB, &pcb, sizeof(pcb));
	if (err < 0) {
		LOG_ERR("nrf_setsockopt(%d,%d,%d) error: %d", socket->fd, NRF_SOL_SOCKET,
			NRF_SO_SENDCB, -errno);

		return -errno;
	}

	socket->send_cb_set = true;

	return 0;
}

static int clear_so_send_cb(struct sm_socket *socket)
{
	int err;

	if (socket == NULL) {
		return -EINVAL;
	}
	if (!socket->send_cb_set) {
		return 0;
	}

	LOG_DBG("Clear send cb for socket %d", socket->fd);

	err = nrf_setsockopt(socket->fd, NRF_SOL_SOCKET, NRF_SO_SENDCB, NULL, 0);
	if (err < 0) {
		LOG_ERR("nrf_setsockopt(%d,%d,%d) error: %d", socket->fd, NRF_SOL_SOCKET,
			NRF_SO_SENDCB, -errno);
		err = -errno;
	}

	socket->send_cb_set = false;

	return err;
}

static int do_socket_open(struct sm_socket *sock)
{
	int ret = 0;
	int proto = IPPROTO_TCP;

	if (sock->type == NRF_SOCK_STREAM) {
		ret = nrf_socket(sock->family, NRF_SOCK_STREAM, NRF_IPPROTO_TCP);
	} else if (sock->type == NRF_SOCK_DGRAM) {
		ret = nrf_socket(sock->family, NRF_SOCK_DGRAM, NRF_IPPROTO_UDP);
		proto = NRF_IPPROTO_UDP;
	} else if (sock->type == NRF_SOCK_RAW) {
		sock->family = NRF_SOCK_RAW;
		sock->role = NRF_SO_SEC_ROLE_CLIENT;
		ret = nrf_socket(sock->family, NRF_SOCK_RAW, NRF_IPPROTO_RAW);
		proto = NRF_IPPROTO_IP;
	} else {
		LOG_ERR("socket type %d not supported", sock->type);
		return -ENOTSUP;
	}
	if (ret < 0) {
		LOG_ERR("nrf_socket() error: %d", -errno);
		return -errno;
	}

	sock->fd = ret;
	struct timeval tmo = {.tv_sec = SOCKET_SEND_TMO_SEC};

	ret = nrf_setsockopt(sock->fd, NRF_SOL_SOCKET, NRF_SO_SNDTIMEO, &tmo, sizeof(tmo));
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d) error: %d", NRF_SO_SNDTIMEO, -errno);
		ret = -errno;
		goto error;
	}

	/* Explicitly bind to secondary PDP context if required */
	ret = bind_to_pdn(sock);
	if (ret) {
		goto error;
	}

	rsp_send("\r\n#XSOCKET: %d,%d,%d\r\n", sock->fd, sock->type, proto);

	delegate_poll_event(sock, NRF_POLLIN | NRF_POLLOUT);

	return 0;

error:
	nrf_close(sock->fd);
	sock->fd = INVALID_SOCKET;
	return ret;
}

static int do_secure_socket_open(struct sm_socket *sock, int peer_verify)
{
	int ret = 0;
	int proto = sock->type == NRF_SOCK_STREAM ? NRF_SPROTO_TLS1v2 : NRF_SPROTO_DTLS1v2;

	if (sock->type != NRF_SOCK_STREAM && sock->type != NRF_SOCK_DGRAM) {
		LOG_ERR("socket type %d not supported", sock->type);
		return -ENOTSUP;
	}

	ret = nrf_socket(sock->family, sock->type, proto);
	if (ret < 0) {
		LOG_ERR("nrf_socket() error: %d", -errno);
		return -errno;
	}
	sock->fd = ret;

	struct timeval tmo = {.tv_sec = SOCKET_SEND_TMO_SEC};

	ret = nrf_setsockopt(sock->fd, NRF_SOL_SOCKET, NRF_SO_SNDTIMEO, &tmo, sizeof(tmo));
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d) error: %d", NRF_SO_SNDTIMEO, -errno);
		ret = -errno;
		goto error;
	}

	/* Explicitly bind to secondary PDP context if required */
	ret = bind_to_pdn(sock);
	if (ret) {
		goto error;
	}
	sec_tag_t sec_tag_list[1] = { sock->sec_tag };

	ret = nrf_setsockopt(sock->fd, NRF_SOL_SECURE, NRF_SO_SEC_TAG_LIST, sec_tag_list,
			       sizeof(sec_tag_t));
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d) error: %d", NRF_SO_SEC_TAG_LIST, -errno);
		ret = -errno;
		goto error;
	}

	/* Set up (D)TLS peer verification */
	ret = nrf_setsockopt(sock->fd, NRF_SOL_SECURE, NRF_SO_SEC_PEER_VERIFY, &peer_verify,
			       sizeof(peer_verify));
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d) error: %d", NRF_SO_SEC_PEER_VERIFY, -errno);
		ret = -errno;
		goto error;
	}
	/* Set up (D)TLS server role if applicable */
	if (sock->role == AT_SOCKET_ROLE_SERVER) {
		int tls_role = NRF_SO_SEC_ROLE_SERVER;

		ret = nrf_setsockopt(sock->fd, NRF_SOL_SECURE, NRF_SO_SEC_ROLE, &tls_role,
				     sizeof(int));
		if (ret) {
			LOG_ERR("nrf_setsockopt(%d) error: %d", NRF_SO_SEC_ROLE, -errno);
			ret = -errno;
			goto error;
		}
	}

	rsp_send("\r\n#XSSOCKET: %d,%d,%d\r\n", sock->fd, sock->type, proto);

	delegate_poll_event(sock, NRF_POLLIN | NRF_POLLOUT);

	return 0;

error:
	nrf_close(sock->fd);
	sock->fd = INVALID_SOCKET;
	return ret;
}

static int do_socket_close(struct sm_socket *sock)
{
	int ret;

	if (sock->fd == INVALID_SOCKET) {
		return 0;
	}

	if (sock->fd_peer != INVALID_SOCKET) {
		ret = nrf_close(sock->fd_peer);
		if (ret) {
			LOG_WRN("peer nrf_close() error: %d", -errno);
		}
		sock->fd_peer = INVALID_SOCKET;
	}
	ret = nrf_close(sock->fd);
	if (ret) {
		LOG_WRN("nrf_close() error: %d", -errno);
		ret = -errno;
	}

	rsp_send("\r\n#XCLOSE: %d,%d\r\n", sock->fd, ret);

	init_socket(sock);

	return ret;
}

static int at_sockopt_to_sockopt(enum at_sockopt at_option, int *level, int *option)
{
	switch (at_option) {
	case AT_SO_REUSEADDR:
		*level = NRF_SOL_SOCKET;
		*option = NRF_SO_REUSEADDR;
		break;
	case AT_SO_RCVTIMEO:
		*level = NRF_SOL_SOCKET;
		*option = NRF_SO_RCVTIMEO;
		break;
	case AT_SO_SNDTIMEO:
		*level = NRF_SOL_SOCKET;
		*option = NRF_SO_SNDTIMEO;
		break;
	case AT_SO_SILENCE_ALL:
		*level = NRF_IPPROTO_ALL;
		*option = NRF_SO_SILENCE_ALL;
		break;
	case AT_SO_IP_ECHO_REPLY:
		*level = NRF_IPPROTO_IP;
		*option = NRF_SO_IP_ECHO_REPLY;
		break;
	case AT_SO_IPV6_ECHO_REPLY:
		*level = NRF_IPPROTO_IPV6;
		*option = NRF_SO_IPV6_ECHO_REPLY;
		break;
	case AT_SO_IPV6_DELAYED_ADDR_REFRESH:
		*level = NRF_IPPROTO_IPV6;
		*option = NRF_SO_IPV6_DELAYED_ADDR_REFRESH;
		break;
	case AT_SO_BINDTOPDN:
		*level = NRF_SOL_SOCKET;
		*option = NRF_SO_BINDTOPDN;
		break;
	case AT_SO_RAI:
		*level = NRF_SOL_SOCKET;
		*option = NRF_SO_RAI;
		break;

	default:
		LOG_WRN("Unsupported option: %d", at_option);
		return -ENOTSUP;
	}

	return 0;
}

static int sockopt_set(struct sm_socket *sock, enum at_sockopt at_option, int at_value)
{
	int ret, level, option;
	void *value = &at_value;
	socklen_t len = sizeof(at_value);
	struct timeval tmo;

	ret = at_sockopt_to_sockopt(at_option, &level, &option);
	if (ret) {
		return ret;
	}

	/* Options with special handling. */
	if (level == NRF_SOL_SOCKET && (option == NRF_SO_RCVTIMEO || option == NRF_SO_SNDTIMEO)) {
		tmo.tv_sec = at_value;
		value = &tmo;
		len = sizeof(tmo);
	}

	ret = nrf_setsockopt(sock->fd, level, option, value, len);
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d,%d,%d) error: %d", sock->fd, level, option, -errno);
	}

	return ret;
}

static int sockopt_get(struct sm_socket *sock, enum at_sockopt at_option)
{
	int ret, value, level, option;
	socklen_t len = sizeof(int);

	ret = at_sockopt_to_sockopt(at_option, &level, &option);
	if (ret) {
		return ret;
	}

	/* Options with special handling. */
	if (level == NRF_SOL_SOCKET && (option == NRF_SO_RCVTIMEO || option == NRF_SO_SNDTIMEO)) {
		struct timeval tmo;

		len = sizeof(struct timeval);
		ret = nrf_getsockopt(sock->fd, level, option, &tmo, &len);
		if (ret == 0) {
			rsp_send("\r\n#XSOCKETOPT: %d,%ld\r\n", sock->fd, (long)tmo.tv_sec);
		}
	} else {
		/* Default */
		ret = nrf_getsockopt(sock->fd, level, option, &value, &len);
		if (ret == 0) {
			rsp_send("\r\n#XSOCKETOPT: %d,%d\r\n", sock->fd, value);
		}
	}

	if (ret) {
		LOG_ERR("nrf_getsockopt(%d,%d,%d) error: %d", sock->fd, level, option, -errno);
	}

	return ret;
}

static int at_sec_sockopt_to_sockopt(enum at_sec_sockopt at_option, int *level, int *option)
{
	*level = NRF_SOL_SECURE;

	switch (at_option) {
	case AT_TLS_HOSTNAME:
		*option = NRF_SO_SEC_HOSTNAME;
		break;
	case AT_TLS_CIPHERSUITE_USED:
		*option = NRF_SO_SEC_CIPHERSUITE_USED;
		break;
	case AT_TLS_PEER_VERIFY:
		*option = NRF_SO_SEC_PEER_VERIFY;
		break;
	case AT_TLS_SESSION_CACHE:
		*option = NRF_SO_SEC_SESSION_CACHE;
		break;
	case AT_TLS_SESSION_CACHE_PURGE:
		*option = NRF_SO_SEC_SESSION_CACHE_PURGE;
		break;
	case AT_TLS_DTLS_CID:
		*option = NRF_SO_SEC_DTLS_CID;
		break;
	case AT_TLS_DTLS_CID_STATUS:
		*option = NRF_SO_SEC_DTLS_CID_STATUS;
		break;
	case AT_TLS_DTLS_HANDSHAKE_TIMEO:
		*option = NRF_SO_SEC_DTLS_HANDSHAKE_TIMEO;
		break;
	default:
		LOG_WRN("Unsupported option: %d", at_option);
		return -ENOTSUP;
	}

	return 0;
}

static int sec_sockopt_set(struct sm_socket *sock, enum at_sec_sockopt at_option, void *value,
			   socklen_t len)
{
	int ret, level, option;

	ret = at_sec_sockopt_to_sockopt(at_option, &level, &option);
	if (ret) {
		return ret;
	}

	/* Options with special handling. */
	if (level == SOL_TLS && option == TLS_HOSTNAME) {
		if (sm_util_casecmp(value, "NULL")) {
			value = NULL;
			len = 0;
		}
	} else if (len != sizeof(int)) {
		return -EINVAL;
	}

	ret = nrf_setsockopt(sock->fd, level, option, value, len);
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d,%d,%d) error: %d", sock->fd, level, option, -errno);
	}

	return ret;
}

static int sec_sockopt_get(struct sm_socket *sock, enum at_sec_sockopt at_option)
{
	int ret, value, level, option;
	socklen_t len = sizeof(int);

	ret = at_sec_sockopt_to_sockopt(at_option, &level, &option);
	if (ret) {
		return ret;
	}

	/* Options with special handling. */
	if (level == NRF_SOL_SECURE && option == NRF_SO_SEC_CIPHERSUITE_USED) {
		ret = nrf_getsockopt(sock->fd, level, option, &value, &len);
		if (ret == 0) {
			rsp_send("\r\n#XSSOCKETOPT: %d,0x%x\r\n", sock->fd, value);
		}
	} else if (level == NRF_SOL_SECURE && option == NRF_SO_SEC_HOSTNAME) {
		char hostname[SM_MAX_URL] = {0};

		len = sizeof(hostname);
		ret = nrf_getsockopt(sock->fd, level, option, &hostname, &len);
		if (ret == 0) {
			rsp_send("\r\n#XSSOCKETOPT: %d,%s\r\n", sock->fd, hostname);
		}
	} else {
		/* Default */
		ret = nrf_getsockopt(sock->fd, level, option, &value, &len);
		if (ret == 0) {
			rsp_send("\r\n#XSSOCKETOPT: %d,%d\r\n", sock->fd, value);
		}
	}

	if (ret) {
		LOG_ERR("nrf_getsockopt(%d,%d,%d) error: %d", sock->fd, level, option, -errno);
	}

	return ret;
}

int bind_to_local_addr(struct sm_socket *sock, uint16_t port)
{
	int ret;

	if (sock->family == NRF_AF_INET) {
		char ipv4_addr[NRF_INET_ADDRSTRLEN];

		util_get_ip_addr(sock->cid, ipv4_addr, NULL);
		if (!*ipv4_addr) {
			LOG_ERR("Get local IPv4 address failed");
			return -ENETDOWN;
		}

		struct nrf_sockaddr_in local = {
			.sin_family = NRF_AF_INET,
			.sin_port = htons(port)
		};

		if (nrf_inet_pton(NRF_AF_INET, ipv4_addr, &local.sin_addr) != 1) {
			LOG_ERR("Parse local IPv4 address failed: %d", -errno);
			return -EINVAL;
		}

		ret = nrf_bind(sock->fd, (struct nrf_sockaddr *)&local,
			       sizeof(struct nrf_sockaddr_in));
		if (ret) {
			LOG_ERR("nrf_bind() sock %d failed: %d", sock->fd, -errno);
			return -errno;
		}
		LOG_DBG("bind sock %d to %s", sock->fd, ipv4_addr);
	} else if (sock->family == NRF_AF_INET6) {
		char ipv6_addr[NRF_INET6_ADDRSTRLEN];

		util_get_ip_addr(sock->cid, NULL, ipv6_addr);
		if (!*ipv6_addr) {
			LOG_ERR("Get local IPv6 address failed");
			return -ENETDOWN;
		}

		struct nrf_sockaddr_in6 local = {
			.sin6_family = NRF_AF_INET6,
			.sin6_port = htons(port)
		};

		if (nrf_inet_pton(NRF_AF_INET6, ipv6_addr, &local.sin6_addr) != 1) {
			LOG_ERR("Parse local IPv6 address failed: %d", -errno);
			return -EINVAL;
		}
		ret = nrf_bind(sock->fd, (struct nrf_sockaddr *)&local,
			       sizeof(struct nrf_sockaddr_in6));
		if (ret) {
			LOG_ERR("nrf_bind() sock %d failed: %d", sock->fd, -errno);
			return -errno;
		}
		LOG_DBG("bind sock %d to %s", sock->fd, ipv6_addr);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int do_connect(struct sm_socket *sock, const char *url, uint16_t port)
{
	int ret = 0;
	struct sockaddr sa = {.sa_family = AF_UNSPEC};

	LOG_DBG("connect %s:%d", url, port);
	ret = util_resolve_host(sock->cid, url, port, sock->family, &sa);
	if (ret) {
		return -EAGAIN;
	}
	if (sa.sa_family == AF_INET) {
		ret = nrf_connect(sock->fd, (struct nrf_sockaddr *)&sa,
				  sizeof(struct nrf_sockaddr_in));
	} else {
		ret = nrf_connect(sock->fd, (struct nrf_sockaddr *)&sa,
				  sizeof(struct nrf_sockaddr_in6));
	}
	if (ret) {
		LOG_ERR("nrf_connect() error: %d", -errno);
		return -errno;
	}

	rsp_send("\r\n#XCONNECT: %d,1\r\n", sock->fd);

	return ret;
}

static int do_send(struct sm_socket *sock, const uint8_t *data, int len, int flags)
{
	int ret = 0;
	int sockfd = sock->fd;
	bool send_ntf = (flags & SM_MSG_SEND_ACK) != 0;

	LOG_DBG("send flags=%d", flags);

	/* For TCP/TLS Server, send to incoming socket */
	if (sock->type == NRF_SOCK_STREAM && sock->role == AT_SOCKET_ROLE_SERVER) {
		if (sock->fd_peer != INVALID_SOCKET) {
			sockfd = sock->fd_peer;
		} else {
			LOG_ERR("No connection");
			return -EINVAL;
		}
	}

	if (send_ntf) {
		/* Set send callback. */
		flags &= ~SM_MSG_SEND_ACK;
		ret = set_so_send_cb(sock);
		if (ret < 0) {
			return ret;
		}
	} else {
		/* Clear previously set send callback. */
		ret = clear_so_send_cb(sock);
		if (ret < 0) {
			return ret;
		}
	}

	uint32_t sent = 0;

	while (sent < len) {
		ret = nrf_send(sockfd, data + sent, len - sent, flags);
		if (ret < 0) {
			LOG_ERR("Sent %u out of %u bytes. (%d)", sent, len, -errno);
			ret = -errno;
			break;
		}
		sent += ret;
	}

	if (!in_datamode()) {
		rsp_send("\r\n#XSEND: %d,%d,%d\r\n", sock->fd,
			 send_ntf ? AT_SOCKET_SEND_RESULT_NW_ACK_URC
				  : AT_SOCKET_SEND_RESULT_DEFAULT,
			 sent);
		if (!send_ntf) {
			delegate_poll_event(sock, NRF_POLLOUT);
		}
	}

	return sent > 0 ? sent : ret;
}

static int data_send_hex(struct sm_socket *sock, const uint8_t *buf, int recv_len)
{
	int consumed = 0;
	char hex_buf[256] = {0};
	uint16_t data_len = recv_len < (sizeof(hex_buf) / 2) ? recv_len : (sizeof(hex_buf) / 2);

	/* For hex string mode, convert the received data to hex string */
	while (consumed < recv_len) {
		int size = sm_util_htoa(buf + consumed, data_len, hex_buf, sizeof(hex_buf));

		if (size < 0) {
			LOG_ERR("Failed to convert binary data to hex string");
			return size;
		}
		data_send(hex_buf, size);
		consumed += size / 2; /* size is in hex string length */
		if (recv_len - consumed < data_len) {
			data_len = recv_len - consumed;
		}
	}
	return 0;
}

static int do_recv(struct sm_socket *sock, int timeout, int flags, enum sm_socket_recv_mode mode,
		   size_t data_len)
{
	int ret;
	int sockfd = sock->fd;

	/* For TCP/TLS Server, receive from incoming socket */
	if (sock->type == NRF_SOCK_STREAM && sock->role == AT_SOCKET_ROLE_SERVER) {
		if (sock->fd_peer != INVALID_SOCKET) {
			sockfd = sock->fd_peer;
		} else {
			LOG_ERR("No remote connection");
			return -EINVAL;
		}
	}
	struct timeval tmo = {.tv_sec = timeout};

	ret = nrf_setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d) error: %d", SO_RCVTIMEO, -errno);
		return -errno;
	}
	ret = nrf_recv(sockfd, (void *)sm_data_buf, data_len, flags);
	if (ret < 0) {
		LOG_WRN("nrf_recv() error: %d", -errno);
		return -errno;
	}
	/**
	 * When a stream socket peer has performed an orderly shutdown,
	 * the return value will be 0 (the traditional "end-of-file")
	 * The value 0 may also be returned if the requested number of
	 * bytes to receive from a stream socket was 0
	 * In both cases, treat as normal shutdown by remote
	 */
	if (ret == 0) {
		LOG_WRN("nrf_recv() return 0");
	} else {
		rsp_send("\r\n#XRECV: %d,%d,%d\r\n", sock->fd, mode, ret);

		if (mode == AT_SOCKET_RECV_MODE_HEXSTRING) {
			ret = data_send_hex(sock, sm_data_buf, ret);
			if (ret) {
				return ret;
			}
		} else {
			data_send(sm_data_buf, ret);
		}
		ret = 0;

		delegate_poll_event(sock, NRF_POLLIN);
	}

	return ret;
}

static int do_sendto(struct sm_socket *sock, const char *url, uint16_t port, const uint8_t *data,
		     int len, int flags)
{
	int ret = 0;
	uint32_t sent = 0;
	struct sockaddr sa = {.sa_family = AF_UNSPEC};
	bool send_ntf = (flags & SM_MSG_SEND_ACK) != 0;

	LOG_DBG("sendto %s:%d, flags=%d", url, port, flags);
	ret = util_resolve_host(sock->cid, url, port, sock->family, &sa);
	if (ret) {
		return -EAGAIN;
	}

	if (send_ntf) {
		/* Set send callback. */
		flags &= ~SM_MSG_SEND_ACK;
		ret = set_so_send_cb(sock);
		if (ret < 0) {
			return ret;
		}
	} else {
		/* Clear previously set send callback. */
		ret = clear_so_send_cb(sock);
		if (ret < 0) {
			return ret;
		}
	}

	do {
		ret = nrf_sendto(sock->fd, data + sent, len - sent, flags,
				 (struct nrf_sockaddr *)&sa,
				 sa.sa_family == AF_INET ? sizeof(struct nrf_sockaddr_in)
							 : sizeof(struct nrf_sockaddr_in6));
		if (ret <= 0) {
			ret = -errno;
			break;
		}
		sent += ret;

	} while (sock->type != NRF_SOCK_DGRAM && sent < len);

	if (ret >= 0 && sock->type == NRF_SOCK_DGRAM && sent != len) {
		/* Partial send of datagram. */
		ret = -EAGAIN;
		sent = 0;
	}

	if (ret < 0) {
		LOG_ERR("Sent %u out of %u bytes. (%d)", sent, len, ret);
	}

	if (!in_datamode()) {
		rsp_send("\r\n#XSENDTO: %d,%d,%d\r\n", sock->fd,
			 send_ntf ? AT_SOCKET_SEND_RESULT_NW_ACK_URC
				  : AT_SOCKET_SEND_RESULT_DEFAULT,
			 sent);
		if (!send_ntf) {
			delegate_poll_event(sock, NRF_POLLOUT);
		}
	}

	return sent > 0 ? sent : ret;
}

static int do_recvfrom(struct sm_socket *sock, int timeout, int flags,
		       enum sm_socket_recv_mode mode, size_t data_len)
{
	int ret;
	struct sockaddr remote;
	socklen_t addrlen = sizeof(struct sockaddr);
	struct timeval tmo = {.tv_sec = timeout};

	ret = nrf_setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
	if (ret) {
		LOG_ERR("nrf_setsockopt(%d) error: %d", SO_RCVTIMEO, -errno);
		return -errno;
	}
	ret = nrf_recvfrom(sock->fd, (void *)sm_data_buf, data_len, flags,
			   (struct nrf_sockaddr *)&remote, &addrlen);
	if (ret < 0) {
		LOG_ERR("nrf_recvfrom() error: %d", -errno);
		return -errno;
	}
	/**
	 * Datagram sockets in various domains permit zero-length
	 * datagrams. When such a datagram is received, the return
	 * value is 0. Treat as normal case
	 */
	if (ret == 0) {
		LOG_WRN("nrf_recvfrom() return 0");
	} else {
		char peer_addr[NRF_INET6_ADDRSTRLEN] = {0};
		uint16_t peer_port = 0;

		util_get_peer_addr(&remote, peer_addr, &peer_port);
		rsp_send("\r\n#XRECVFROM: %d,%d,%d,\"%s\",%d\r\n", sock->fd, mode, ret, peer_addr,
			 peer_port);

		if (mode == AT_SOCKET_RECV_MODE_HEXSTRING) {
			ret = data_send_hex(sock, sm_data_buf, ret);
			if (ret) {
				return ret;
			}
		} else {
			data_send(sm_data_buf, ret);
		}

		delegate_poll_event(sock, ZSOCK_POLLIN);
	}

	return 0;
}

static int socket_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int ret = 0;

	if (op == DATAMODE_SEND) {
		if (datamode_sock->type == SOCK_DGRAM &&
		    (flags & SM_DATAMODE_FLAGS_MORE_DATA) != 0) {
			LOG_ERR("Datamode buffer overflow");
			exit_datamode_handler(-EOVERFLOW);
			return -EOVERFLOW;
		} else {
			if (strlen(udp_url) > 0) {
				ret = do_sendto(datamode_sock, udp_url, udp_port, data, len,
						datamode_sock->send_flags);
			} else {
				ret = do_send(datamode_sock, data, len, datamode_sock->send_flags);
			}
			LOG_INF("datamode send: %d", ret);
		}
	} else if (op == DATAMODE_EXIT) {
		LOG_DBG("datamode exit");
		memset(udp_url, 0, sizeof(udp_url));
		if ((datamode_sock->send_flags & SM_MSG_SEND_ACK) == 0) {
			delegate_poll_event(datamode_sock, NRF_POLLOUT);
		}
		datamode_sock = NULL;
	}

	return ret;
}

SM_AT_CMD_CUSTOM(xsocket, "AT#XSOCKET", handle_at_socket);
static int handle_at_socket(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			    uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t op;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err) {
			return err;
		}
		if (op == AT_SOCKET_OPEN || op == AT_SOCKET_OPEN6) {
			sock = find_avail_socket();
			if (sock == NULL) {
				LOG_ERR("Max socket count reached");
				err = -EINVAL;
				goto error;
			}
			init_socket(sock);
			err = at_parser_num_get(parser, 2, &sock->type);
			if (err) {
				goto error;
			}
			err = at_parser_num_get(parser, 3, &sock->role);
			if (err) {
				goto error;
			}
			sock->family = (op == AT_SOCKET_OPEN) ? NRF_AF_INET : NRF_AF_INET6;
			if (param_count > 4) {
				err = at_parser_num_get(parser, 4, &sock->cid);
				if (err) {
					goto error;
				}
				if (sock->cid > 10) {
					err = -EINVAL;
					goto error;
				}
			}
			err = do_socket_open(sock);
			if (err) {
				LOG_ERR("do_socket_open() failed: %d", err);
				goto error;
			}
		} else {
			err = -EINVAL;
		} break;

	case AT_PARSER_CMD_TYPE_READ:
		for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
			if (socks[i].fd != INVALID_SOCKET &&
			    socks[i].sec_tag == SEC_TAG_TLS_INVALID) {
				rsp_send("\r\n#XSOCKET: %d,%d,%d,%d,%d\r\n", socks[i].fd,
					 socks[i].family, socks[i].role, socks[i].type,
					 socks[i].cid);
			}
		}
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XSOCKET: <handle>,(%d,%d),(%d,%d,%d),(%d,%d),<cid>\r\n",
			AT_SOCKET_OPEN, AT_SOCKET_OPEN6,
			SOCK_STREAM, SOCK_DGRAM, SOCK_RAW,
			AT_SOCKET_ROLE_CLIENT, AT_SOCKET_ROLE_SERVER);
		err = 0;
		break;

	default:
		break;
	}

	return err;

error:
	init_socket(sock);

	return err;
}

SM_AT_CMD_CUSTOM(xssocket, "AT#XSSOCKET", handle_at_secure_socket);
static int handle_at_secure_socket(enum at_parser_cmd_type cmd_type,
				   struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t op;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err) {
			return err;
		}
		if (op == AT_SOCKET_OPEN || op == AT_SOCKET_OPEN6) {
			/** Peer verification level for TLS connection.
			 *    - 0 - none
			 *    - 1 - optional
			 *    - 2 - required
			 * If not set, socket will use defaults (none for servers,
			 * required for clients)
			 */
			uint16_t peer_verify;

			sock = find_avail_socket();
			if (sock == NULL) {
				LOG_ERR("Max socket count reached");
				err = -EINVAL;
				goto error;
			}
			init_socket(sock);
			err = at_parser_num_get(parser, 2, &sock->type);
			if (err) {
				goto error;
			}
			err = at_parser_num_get(parser, 3, &sock->role);
			if (err) {
				goto error;
			}
			if (sock->role == AT_SOCKET_ROLE_SERVER) {
				peer_verify = TLS_PEER_VERIFY_NONE;
			} else if (sock->role == AT_SOCKET_ROLE_CLIENT) {
				peer_verify = TLS_PEER_VERIFY_REQUIRED;
			} else {
				err = -EINVAL;
				goto error;
			}
			sock->sec_tag = SEC_TAG_TLS_INVALID;
			err = at_parser_num_get(parser, 4, &sock->sec_tag);
			if (err) {
				goto error;
			}
			if (param_count > 5) {
				err = at_parser_num_get(parser, 5, &peer_verify);
				if (err) {
					goto error;
				}
			}
			sock->family = (op == AT_SOCKET_OPEN) ? AF_INET : AF_INET6;
			if (param_count > 6) {
				err = at_parser_num_get(parser, 6, &sock->cid);
				if (err) {
					goto error;
				}
				if (sock->cid > 10) {
					err = -EINVAL;
					goto error;
				}
			}
			err = do_secure_socket_open(sock, peer_verify);
			if (err) {
				LOG_ERR("do_secure_socket_open() failed: %d", err);
				goto error;
			}
		} else {
			err = -EINVAL;
		} break;

	case AT_PARSER_CMD_TYPE_READ:
		for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
			if (socks[i].fd != INVALID_SOCKET &&
			    socks[i].sec_tag != SEC_TAG_TLS_INVALID) {
				rsp_send("\r\n#XSSOCKET: %d,%d,%d,%d,%d,%d\r\n", socks[i].fd,
					 socks[i].family, socks[i].role, socks[i].type,
					 socks[i].sec_tag, socks[i].cid);
			}
		}
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XSSOCKET: <handle>,(%d,%d),(%d,%d),(%d,%d),"
			 "<sec_tag>,<peer_verify>,<cid>\r\n",
			AT_SOCKET_OPEN, AT_SOCKET_OPEN6,
			SOCK_STREAM, SOCK_DGRAM,
			AT_SOCKET_ROLE_CLIENT, AT_SOCKET_ROLE_SERVER);
		err = 0;
		break;

	default:
		break;
	}

	return err;

error:
	init_socket(sock);

	return err;

}

SM_AT_CMD_CUSTOM(xclose, "AT#XCLOSE", handle_at_close);
static int handle_at_close(enum at_parser_cmd_type cmd_type,
			   struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	int fd;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count > 1) {
			err = at_parser_num_get(parser, 1, &fd);
			if (err) {
				return err;
			}
			sock = find_socket(fd);
			if (sock == NULL) {
				return -EINVAL;
			}
			err = do_socket_close(sock);
		} else {
			int ret;

			err = 0;
			/* Close all opened sockets */
			for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
				if (socks[i].fd != INVALID_SOCKET) {
					ret = do_socket_close(&socks[i]);
					if (ret < 0) {
						err = ret;
					}
				}
			}
		}
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xsocketopt, "AT#XSOCKETOPT", handle_at_socketopt);
static int handle_at_socketopt(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	int err = -EINVAL;
	int fd;
	uint16_t op;
	uint16_t name;
	int value = 0;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &op);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 3, &name);
		if (err) {
			return err;
		}
		if (op == AT_SOCKETOPT_SET) {
			/* some options don't require a value */
			if (param_count > 4) {
				err = at_parser_num_get(parser, 4, &value);
				if (err) {
					return err;
				}
			}

			err = sockopt_set(sock, name, value);
		} else if (op == AT_SOCKETOPT_GET) {
			err = sockopt_get(sock, name);
		} else {
			err = -EINVAL;
		} break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XSOCKETOPT: <handle>,(%d,%d),<name>,<value>\r\n",
			AT_SOCKETOPT_GET, AT_SOCKETOPT_SET);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xssocketopt, "AT#XSSOCKETOPT", handle_at_secure_socketopt);
static int handle_at_secure_socketopt(enum at_parser_cmd_type cmd_type,
				      struct at_parser *parser, uint32_t)
{
	int err = -EINVAL;
	int fd;
	uint16_t op;
	uint16_t name;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		if (sock->sec_tag == SEC_TAG_TLS_INVALID) {
			LOG_ERR("Not secure socket");
			return err;
		}
		err = at_parser_num_get(parser, 2, &op);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 3, &name);
		if (err) {
			return err;
		}
		if (op == AT_SOCKETOPT_SET) {
			int value_int = 0;
			char value_str[SM_MAX_URL] = {0};
			int size = SM_MAX_URL;

			err = at_parser_num_get(parser, 4, &value_int);
			if (err == -EOPNOTSUPP) {
				err = util_string_get(parser, 4, value_str, &size);
				if (err) {
					return err;
				}
				err = sec_sockopt_set(sock, name, value_str, strlen(value_str));
			} else if (err == 0) {
				err = sec_sockopt_set(sock, name, &value_int, sizeof(value_int));
			} else {
				return -EINVAL;
			}
		}  else if (op == AT_SOCKETOPT_GET) {
			err = sec_sockopt_get(sock, name);
		}  else {
			err = -EINVAL;
		} break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XSSOCKETOPT: <handle>,(%d,%d),<name>,<value>\r\n",
			AT_SOCKETOPT_GET, AT_SOCKETOPT_SET);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xbind, "AT#XBIND", handle_at_bind);
static int handle_at_bind(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t)
{
	int err = -EINVAL;
	int fd;
	uint16_t port;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &port);
		if (err < 0) {
			return err;
		}
		err = bind_to_local_addr(sock, port);
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xconnect, "AT#XCONNECT", handle_at_connect);
static int handle_at_connect(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			     uint32_t)
{
	int err = -EINVAL;
	int fd;
	char url[SM_MAX_URL] = {0};
	int size = SM_MAX_URL;
	uint16_t port;
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		if (sock->role != AT_SOCKET_ROLE_CLIENT) {
			LOG_ERR("Invalid role");
			return -EOPNOTSUPP;
		}
		err = util_string_get(parser, 2, url, &size);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 3, &port);
		if (err) {
			return err;
		}
		err = do_connect(sock, url, port);
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xsend, "AT#XSEND", handle_at_send);
static int handle_at_send(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	int err = -EINVAL;
	int fd;
	uint16_t mode;
	int size;
	struct sm_socket *sock = NULL;
	const char *str_ptr;
	int data_len = 0;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &mode);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 3, &sock->send_flags);
		if (err) {
			return err;
		}
		if (mode == AT_SOCKET_SEND_MODE_STRING || mode == AT_SOCKET_SEND_MODE_HEXSTRING) {
			if (param_count > 4) {
				err = at_parser_string_ptr_get(parser, 4, &str_ptr, &size);
				if (err) {
					return err;
				}
			} else {
				return -EINVAL; /* Missing string data */
			}

			/* Convert hex string to binary data */
			if (mode == AT_SOCKET_SEND_MODE_HEXSTRING) {
				err = sm_util_atoh(str_ptr, size, hex_data, sizeof(hex_data));
				if (err < 0) {
					LOG_ERR("Failed to convert hex string to binary data");
					return err;
				}
				str_ptr = hex_data;
				size = err;
			}

			err = do_send(sock, str_ptr, size, sock->send_flags);
			if (err == size) {
				err = 0;
			} else {
				err = err < 0 ? err : -EAGAIN;
			}
		} else if (mode == AT_SOCKET_SEND_MODE_DATA) {
			if (param_count > 4) {
				err = at_parser_num_get(parser, 4, &data_len);
				if (err) {
					return err;
				}
			}
			datamode_sock = sock;
			err = enter_datamode(socket_datamode_callback, data_len);
		} else {
			return -EINVAL;
		}
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xrecv, "AT#XRECV", handle_at_recv);
static int handle_at_recv(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	int err = -EINVAL;
	int fd;
	uint16_t mode;
	int timeout;
	int flags = 0;
	int data_len = sizeof(sm_data_buf);
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &mode);
		if (err) {
			return err;
		}
		if (mode != AT_SOCKET_RECV_MODE_BINARY && mode != AT_SOCKET_RECV_MODE_HEXSTRING) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 3, &flags);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 4, &timeout);
		if (err) {
			return err;
		}
		if (param_count > 5) {
			err = at_parser_num_get(parser, 5, &data_len);
			if (err) {
				return err;
			}
			if (data_len > sizeof(sm_data_buf)) {
				LOG_ERR("data_len is too large for receive buffer");
				return -ENOBUFS;
			}
		}
		err = do_recv(sock, timeout, flags, mode, data_len);
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xsendto, "AT#XSENDTO", handle_at_sendto);
static int handle_at_sendto(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			    uint32_t param_count)
{

	int err = -EINVAL;
	int fd;
	uint16_t mode;
	int size;
	struct sm_socket *sock = NULL;
	const char *str_ptr;
	int data_len = 0;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &mode);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 3, &sock->send_flags);
		if (err) {
			return err;
		}
		size = sizeof(udp_url);
		err = util_string_get(parser, 4, udp_url, &size);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 5, &udp_port);
		if (err) {
			return err;
		}
		if (mode == AT_SOCKET_SEND_MODE_STRING || mode == AT_SOCKET_SEND_MODE_HEXSTRING) {
			if (param_count > 6) {
				err = at_parser_string_ptr_get(parser, 6, &str_ptr, &size);
				if (err) {
					return err;
				}
			} else {
				return -EINVAL; /* Missing string data */
			}

			/* Convert hex string to binary data */
			if (mode == AT_SOCKET_SEND_MODE_HEXSTRING) {
				err = sm_util_atoh(str_ptr, size, hex_data, sizeof(hex_data));
				if (err < 0) {
					LOG_ERR("Failed to convert hex string to binary data");
					return err;
				}
				str_ptr = hex_data;
				size = err;
			}

			err = do_sendto(sock, udp_url, udp_port, str_ptr, size, sock->send_flags);
			if (err == size) {
				err = 0;
			} else {
				err = err < 0 ? err : -EAGAIN;
			}
			memset(udp_url, 0, sizeof(udp_url));
		} else if (mode == AT_SOCKET_SEND_MODE_DATA) {
			if (param_count > 6) {
				err = at_parser_num_get(parser, 6, &data_len);
				if (err) {
					return err;
				}
			}
			datamode_sock = sock;
			err = enter_datamode(socket_datamode_callback, data_len);
		} else {
			return -EINVAL;
		}
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xrecvfrom, "AT#XRECVFROM", handle_at_recvfrom);
static int handle_at_recvfrom(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	int err = -EINVAL;
	int fd;
	uint16_t mode;
	int timeout;
	int flags = 0;
	int data_len = sizeof(sm_data_buf);
	struct sm_socket *sock = NULL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &fd);
		if (err) {
			return err;
		}
		sock = find_socket(fd);
		if (sock == NULL) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &mode);
		if (err) {
			return err;
		}
		if (mode != AT_SOCKET_RECV_MODE_BINARY && mode != AT_SOCKET_RECV_MODE_HEXSTRING) {
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 3, &flags);
		if (err) {
			return err;
		}
		err = at_parser_num_get(parser, 4, &timeout);
		if (err) {
			return err;
		}
		if (param_count > 5) {
			err = at_parser_num_get(parser, 5, &data_len);
			if (err) {
				return err;
			}
			if (data_len > sizeof(sm_data_buf)) {
				LOG_ERR("data_len is too large for receive buffer");
				return -ENOBUFS;
			}
		}
		err = do_recvfrom(sock, timeout, flags, mode, data_len);
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xgetaddrinfo, "AT#XGETADDRINFO", handle_at_getaddrinfo);
static int handle_at_getaddrinfo(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				 uint32_t param_count)
{
	int err = -EINVAL;
	char hostname[NI_MAXHOST];
	char host[SM_MAX_URL];
	int size = SM_MAX_URL;
	struct nrf_addrinfo *result;
	struct nrf_addrinfo *res;
	char rsp_buf[256];

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = util_string_get(parser, 1, host, &size);
		if (err) {
			return err;
		}
		if (param_count == 3) {
			/* DNS query with designated address family */
			struct nrf_addrinfo hints = {
				.ai_family = AF_UNSPEC
			};
			err = at_parser_num_get(parser, 2, &hints.ai_family);
			if (err) {
				return err;
			}
			if (hints.ai_family < 0  || hints.ai_family > AF_INET6) {
				return -EINVAL;
			}
			err = nrf_getaddrinfo(host, NULL, &hints, &result);
		} else if (param_count == 2) {
			err = nrf_getaddrinfo(host, NULL, NULL, &result);
		} else {
			return -EINVAL;
		}
		if (err) {
			rsp_send("\r\n#XGETADDRINFO: \"%s\"\r\n", zsock_gai_strerror(err));
			return err;
		} else if (result == NULL) {
			rsp_send("\r\n#XGETADDRINFO: \"not found\"\r\n");
			return -ENOENT;
		}

		sprintf(rsp_buf, "\r\n#XGETADDRINFO: \"");
		/* loop over all returned results and do inverse lookup */
		for (res = result; res != NULL; res = res->ai_next) {
			if (res->ai_family == NRF_AF_INET) {
				struct nrf_sockaddr_in *host =
					(struct nrf_sockaddr_in *)result->ai_addr;

				nrf_inet_ntop(NRF_AF_INET, &host->sin_addr, hostname,
					      sizeof(hostname));
			} else if (res->ai_family == AF_INET6) {
				struct nrf_sockaddr_in6 *host =
					(struct nrf_sockaddr_in6 *)result->ai_addr;

				nrf_inet_ntop(NRF_AF_INET6, &host->sin6_addr, hostname,
					      sizeof(hostname));
			} else {
				continue;
			}

			strcat(rsp_buf, hostname);
			if (res->ai_next) {
				strcat(rsp_buf, " ");
			}
		}
		strcat(rsp_buf, "\"\r\n");
		rsp_send("%s", rsp_buf);
		nrf_freeaddrinfo(result);
		break;

	default:
		break;
	}

	return err;
}

static int apoll_stop(void)
{
	poll_ctx.poll_all = false;
	poll_ctx.poll_events = 0;
	poll_ctx.poll_running = false;

	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd != INVALID_SOCKET) {
			clear_so_poll_cb(&socks[i]);
		}
		socks[i].async_poll = (struct sm_async_poll){0};
	}

	return 0;
}

static void format_apoll_read_response(char *response, size_t size)
{
	int offset = 0;

	offset = snprintf(response, size, "\r\n#XAPOLL: %d,%d", poll_ctx.poll_running,
			  poll_ctx.poll_events);

	for (int i = 0; i < SM_MAX_SOCKET_COUNT && offset < size; i++) {
		if (socks[i].fd != INVALID_SOCKET &&
		    (poll_ctx.poll_all || socks[i].async_poll.specific)) {
			offset += snprintf(&response[offset], size - offset, ",%d", socks[i].fd);
		}
	}

	if (offset > size - sizeof("\r\n")) {
		offset = size - sizeof("\r\n");
	}
	snprintf(&response[offset], size - offset, "\r\n");
}

static void set_apoll_all_sockets(void)
{
	poll_ctx.poll_running = true;
	poll_ctx.poll_all = true;
	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd != INVALID_SOCKET) {
			socks[i].async_poll.update_events = poll_ctx.poll_events;
		}
	}
}

static int set_apoll_specific_sockets(struct at_parser *parser, uint32_t param_count)
{
	int handle;
	int err;
	bool socket_found;

	poll_ctx.poll_running = true;
	poll_ctx.poll_all = false;

	/* Clear previously set values. */
	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd != INVALID_SOCKET) {
			clear_so_poll_cb(&socks[i]);
		}
		socks[i].async_poll = (struct sm_async_poll){0};
	}

	/* Go through all the given handles. */
	for (int i = 3; i < param_count; i++) {
		err = at_parser_num_get(parser, i, &handle);
		if (err) {
			return err;
		}
		socket_found = false;

		/* Match given handles to socket handles. */
		for (int j = 0; j < SM_MAX_SOCKET_COUNT; j++) {
			if (socks[j].fd == handle) {
				socks[j].async_poll.specific = true;
				socks[j].async_poll.update_events = poll_ctx.poll_events;
				socket_found = true;
				break;
			}
		}
		if (!socket_found) {
			LOG_ERR("Socket %d not found", handle);
			return -EINVAL;
		}
	}

	return 0;
}

SM_AT_CMD_CUSTOM(xapoll, "AT#XAPOLL", handle_at_apoll);
static int handle_at_apoll(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	int err = -EINVAL;
	int op;

	enum async_poll_operation {
		AT_ASYNCPOLL_STOP = 0,
		AT_ASYNCPOLL_START = 1,
	};

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (at_parser_num_get(parser, 1, &op) ||
		    (op != AT_ASYNCPOLL_START && op != AT_ASYNCPOLL_STOP)) {
			return -EINVAL;
		}
		if (op == AT_ASYNCPOLL_STOP) {
			return apoll_stop();
		}

		/* APOLL START */
		err = at_parser_num_get(parser, 2, &poll_ctx.poll_events);
		if (err) {
			return err;
		}
		if (poll_ctx.poll_events & ~(NRF_POLLIN | NRF_POLLOUT)) {
			LOG_ERR("Invalid poll events: 0x%02x", poll_ctx.poll_events);
			return -EINVAL;
		}
		if (param_count == 3) {
			set_apoll_all_sockets();
		} else {
			err = set_apoll_specific_sockets(parser, param_count);
			if (err) {
				return err;
			}
		}
		poll_work_fn(NULL);
		break;

	case AT_PARSER_CMD_TYPE_READ:
		char response[64];

		format_apoll_read_response(response, sizeof(response));
		err = sm_at_send_str(response);
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XAPOLL: (%d,%d),<0,%d,%d,%d>,<handle1>,<handle2>,...\r\n",
			 AT_ASYNCPOLL_STOP, AT_ASYNCPOLL_START, ZSOCK_POLLIN, ZSOCK_POLLOUT,
			 ZSOCK_POLLIN | ZSOCK_POLLOUT);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

/**@brief API to initialize Socket AT commands handler
 */
int sm_at_socket_init(void)
{
	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		init_socket(&socks[i]);
	}

	k_work_init(&poll_ctx.poll_work, poll_work_fn);

	return 0;
}

/**@brief API to uninitialize Socket AT commands handler
 */
int sm_at_socket_uninit(void)
{
	apoll_stop();

	for (int i = 0; i < SM_MAX_SOCKET_COUNT; i++) {
		if (socks[i].fd != INVALID_SOCKET) {
			do_socket_close(&socks[i]);
		}
	}

	return 0;
}
