/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/random.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_coap, CONFIG_SM_LOG_LEVEL);

#define COAP_MAX_PATH_LEN	128
#define COAP_MAX_CONTEXTS	3
#define COAP_MAX_PAYLOAD	512

/**@brief CoAP context state */
struct sm_coap_ctx {
	bool in_use;
	int sock;
	struct sockaddr_storage server_addr;
	socklen_t server_addr_len;
	uint16_t message_id;
	char server_host[SM_MAX_URL + 1];
	uint16_t server_port;
	sec_tag_t sec_tag;
};

static struct sm_coap_ctx coap_contexts[COAP_MAX_CONTEXTS];

#define THREAD_STACK_SIZE	KB(2)
#define THREAD_PRIORITY		K_LOWEST_APPLICATION_THREAD_PRIO

static struct k_thread coap_thread;
static K_THREAD_STACK_DEFINE(coap_thread_stack, THREAD_STACK_SIZE);
static bool coap_thread_running = false;

/* Buffers for CoAP */
static uint8_t coap_rx_buffer[COAP_MAX_PAYLOAD];

/**@brief Send CoAP AT error URC */
static const char *coap_err_str(int err, char *buf, size_t buf_len)
{
	if (err == 0) {
		return "OK";
	}

	if (err > 0) {
		return zsock_gai_strerror(err);
	}

	const char *str = strerror(-err);
	if (str == NULL) {
		snprintk(buf, buf_len, "errno %d", -err);
		return buf;
	}

	snprintk(buf, buf_len, "%s", str);
	return buf;
}

static void coap_send_error(const char *cmd, int ctx_id, int err)
{
	char err_buf[64];
	const char *err_str = coap_err_str(err, err_buf, sizeof(err_buf));

	rsp_send("\r\n%%COAPERROR: %s,%d,%d\r\n", cmd, ctx_id, err);
	rsp_send("\r\n%%COAPERRORINFO: %s,%d,%d,\"%s\"\r\n", cmd, ctx_id, err, err_str);
}

static void coap_send_error_step(const char *cmd, int ctx_id, const char *step, int err)
{
	char err_buf[64];
	const char *err_str = coap_err_str(err, err_buf, sizeof(err_buf));

	rsp_send("\r\n%%COAPERRORSTEP: %s,%d,%s,%d,\"%s\"\r\n",
		 cmd, ctx_id, step, err, err_str);
}

/**@brief Helper to convert binary to hex string */
static void bin_to_hex(const uint8_t *bin, size_t bin_len, char *hex, size_t hex_size)
{
	const char *hex_chars = "0123456789ABCDEF";
	size_t i;
	
	for (i = 0; i < bin_len && i * 2 + 1 < hex_size; i++) {
		hex[i * 2] = hex_chars[(bin[i] >> 4) & 0xF];
		hex[i * 2 + 1] = hex_chars[bin[i] & 0xF];
	}
	hex[i * 2] = '\0';
}

/**@brief Helper to convert hex string to binary */
static int hex_to_bin(const char *hex, size_t hex_len, uint8_t *bin, size_t bin_size)
{
	size_t bin_len = hex_len / 2;
	
	if (bin_len > bin_size) {
		return -EINVAL;
	}
	
	for (size_t i = 0; i < bin_len; i++) {
		char hex_byte[3] = {hex[i*2], hex[i*2+1], '\0'};
		bin[i] = (uint8_t)strtol(hex_byte, NULL, 16);
	}
	
	return bin_len;
}

/**@brief Find free CoAP context */
static int find_free_context(void)
{
	for (int i = 0; i < COAP_MAX_CONTEXTS; i++) {
		if (!coap_contexts[i].in_use) {
			return i;
		}
	}
	return -1;
}

/**@brief Validate context ID */
static bool is_valid_context(int ctx_id)
{
	return (ctx_id >= 0 && ctx_id < COAP_MAX_CONTEXTS && coap_contexts[ctx_id].in_use);
}

/**@brief Create CoAP context */
static int do_coap_create(void)
{
	int ctx_id = find_free_context();
	
	if (ctx_id < 0) {
		LOG_ERR("No free CoAP contexts");
		return -ENOMEM;
	}
	
	memset(&coap_contexts[ctx_id], 0, sizeof(struct sm_coap_ctx));
	coap_contexts[ctx_id].in_use = true;
	coap_contexts[ctx_id].sock = -1;
	coap_contexts[ctx_id].message_id = 1;
	coap_contexts[ctx_id].sec_tag = SEC_TAG_TLS_INVALID;
	
	LOG_INF("CoAP context created: id=%d", ctx_id);
	return ctx_id;
}

/**@brief Set CoAP server */
static int do_coap_set_server(int ctx_id, const char *host, uint16_t port, sec_tag_t sec_tag)
{
	int err;
	struct sm_coap_ctx *ctx = &coap_contexts[ctx_id];
	struct sockaddr_storage ss = {0};
	struct sockaddr *sa = (struct sockaddr *)&ss;
	
	if (!is_valid_context(ctx_id)) {
		return -EINVAL;
	}
	
	/* Resolve hostname or parse numeric IP */
	{
		struct sockaddr_in sa4 = {0};
		struct sockaddr_in6 sa6 = {0};

		if (zsock_inet_pton(AF_INET, host, &sa4.sin_addr) == 1) {
			sa4.sin_family = AF_INET;
			sa4.sin_port = htons(port);
			memcpy(&ss, &sa4, sizeof(sa4));
		} else if (zsock_inet_pton(AF_INET6, host, &sa6.sin6_addr) == 1) {
			sa6.sin6_family = AF_INET6;
			sa6.sin6_port = htons(port);
			memcpy(&ss, &sa6, sizeof(sa6));
		} else {
			err = util_resolve_host(0, host, port, AF_INET, sa);
			if (err) {
				coap_send_error_step("COAPSERVER", ctx_id, "RESOLVE", err);
				LOG_ERR("Failed to resolve host: %d", err);
				return err;
			}
		}
	}
	
	/* Store server info */
	strncpy(ctx->server_host, host, sizeof(ctx->server_host) - 1);
	ctx->server_port = port;
	ctx->sec_tag = sec_tag;
	
	if (sa->sa_family == AF_INET) {
		memcpy(&ctx->server_addr, &ss, sizeof(struct sockaddr_in));
		ctx->server_addr_len = sizeof(struct sockaddr_in);
	} else if (sa->sa_family == AF_INET6) {
		memcpy(&ctx->server_addr, &ss, sizeof(struct sockaddr_in6));
		ctx->server_addr_len = sizeof(struct sockaddr_in6);
	}
	
	/* Create socket */
	if (ctx->sock >= 0) {
		zsock_close(ctx->sock);
	}
	
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	if (sec_tag != SEC_TAG_TLS_INVALID) {
		ctx->sock = zsock_socket(sa->sa_family, SOCK_DGRAM, IPPROTO_DTLS_1_2);
		if (ctx->sock < 0) {
			coap_send_error_step("COAPSERVER", ctx_id, "SOCKET", -errno);
			LOG_ERR("Failed to create DTLS socket: %d", errno);
			return -errno;
		}
		
		sec_tag_t sec_tag_list[] = { sec_tag };
		err = zsock_setsockopt(ctx->sock, SOL_TLS, TLS_SEC_TAG_LIST,
				       sec_tag_list, sizeof(sec_tag_list));
		if (err) {
			coap_send_error_step("COAPSERVER", ctx_id, "TLS_SEC_TAG", -errno);
			LOG_ERR("Failed to set TLS sec tag: %d", errno);
			zsock_close(ctx->sock);
			ctx->sock = -1;
			return -errno;
		}
		
		err = zsock_setsockopt(ctx->sock, SOL_TLS, TLS_HOSTNAME,
				       host, strlen(host));
		if (err) {
			LOG_WRN("Failed to set TLS hostname: %d", errno);
		}
	} else
#endif
	{
		ctx->sock = zsock_socket(sa->sa_family, SOCK_DGRAM, IPPROTO_UDP);
		if (ctx->sock < 0) {
			coap_send_error_step("COAPSERVER", ctx_id, "SOCKET", -errno);
			LOG_ERR("Failed to create UDP socket: %d", errno);
			return -errno;
		}
	}
	
	/* Set socket to non-blocking */
	int flags = zsock_fcntl(ctx->sock, NRF_F_GETFL, 0);
	if (flags < 0) {
		coap_send_error_step("COAPSERVER", ctx_id, "F_GETFL", -errno);
		LOG_ERR("Failed to get socket flags: %d", errno);
		return -errno;
	}
	if (zsock_fcntl(ctx->sock, NRF_F_SETFL, flags | NRF_O_NONBLOCK) < 0) {
		coap_send_error_step("COAPSERVER", ctx_id, "F_SETFL", -errno);
		LOG_ERR("Failed to set socket nonblocking: %d", errno);
		return -errno;
	}
	
	LOG_INF("CoAP server set: %s:%u (ctx=%d)", host, port, ctx_id);
	return 0;
}

/**@brief Send CoAP request */
static int do_coap_request(int ctx_id, enum coap_method method, const char *path,
			   const uint8_t *payload, size_t payload_len)
{
	int err;
	struct sm_coap_ctx *ctx = &coap_contexts[ctx_id];
	uint8_t request_buf[COAP_MAX_PAYLOAD];
	struct coap_packet request;
	
	if (!is_valid_context(ctx_id)) {
		return -EINVAL;
	}
	
	if (ctx->sock < 0) {
		LOG_ERR("Socket not initialized");
		return -ENOTCONN;
	}
	
	/* Initialize CoAP request */
	err = coap_packet_init(&request, request_buf, sizeof(request_buf),
			       COAP_VERSION_1, COAP_TYPE_CON, COAP_TOKEN_MAX_LEN,
			       coap_next_token(), method, ctx->message_id++);
	if (err < 0) {
		LOG_ERR("Failed to init CoAP packet: %d", err);
		return err;
	}
	
	/* Add URI path */
	const char *path_ptr = path;
	while (*path_ptr == '/') {
		path_ptr++;  /* Skip leading slashes */
	}
	
	/* Split path by '/' and add each segment */
	char path_seg[64];
	const char *seg_start = path_ptr;
	while (*seg_start) {
		const char *seg_end = strchr(seg_start, '/');
		size_t seg_len;
		
		if (seg_end) {
			seg_len = seg_end - seg_start;
		} else {
			seg_len = strlen(seg_start);
		}
		
		if (seg_len > 0 && seg_len < sizeof(path_seg)) {
			memcpy(path_seg, seg_start, seg_len);
			path_seg[seg_len] = '\0';
			
			err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
							path_seg, seg_len);
			if (err < 0) {
				LOG_ERR("Failed to add URI path: %d", err);
				return err;
			}
		}
		
		if (!seg_end) {
			break;
		}
		seg_start = seg_end + 1;
	}
	
	/* Add payload if present */
	if (payload && payload_len > 0) {
		err = coap_packet_append_payload_marker(&request);
		if (err < 0) {
			LOG_ERR("Failed to add payload marker: %d", err);
			return err;
		}
		
		err = coap_packet_append_payload(&request, payload, payload_len);
		if (err < 0) {
			LOG_ERR("Failed to add payload: %d", err);
			return err;
		}
	}
	
	/* Send request */
	err = zsock_sendto(ctx->sock, request.data, request.offset, 0,
			   (struct sockaddr *)&ctx->server_addr, ctx->server_addr_len);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request: %d", errno);
		return -errno;
	}
	
	LOG_DBG("CoAP %s sent to %s (ctx=%d, msgid=%d, len=%d)",
		method == COAP_METHOD_GET ? "GET" :
		method == COAP_METHOD_POST ? "POST" :
		method == COAP_METHOD_PUT ? "PUT" : "DELETE",
		path, ctx_id, ctx->message_id - 1, err);
	
	return 0;
}

/**@brief Delete CoAP context */
static int do_coap_delete(int ctx_id)
{
	if (!is_valid_context(ctx_id)) {
		return -EINVAL;
	}
	
	struct sm_coap_ctx *ctx = &coap_contexts[ctx_id];
	
	if (ctx->sock >= 0) {
		zsock_close(ctx->sock);
		ctx->sock = -1;
	}
	
	memset(ctx, 0, sizeof(struct sm_coap_ctx));
	
	LOG_INF("CoAP context deleted: id=%d", ctx_id);
	return 0;
}

/**@brief Background thread for receiving CoAP responses */
static void coap_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	
	struct zsock_pollfd fds[COAP_MAX_CONTEXTS];
	int nfds;
	
	LOG_INF("CoAP thread started");
	
	while (coap_thread_running) {
		/* Build poll list */
		nfds = 0;
		for (int i = 0; i < COAP_MAX_CONTEXTS; i++) {
			if (coap_contexts[i].in_use && coap_contexts[i].sock >= 0) {
				fds[nfds].fd = coap_contexts[i].sock;
				fds[nfds].events = ZSOCK_POLLIN;
				fds[nfds].revents = 0;
				nfds++;
			}
		}
		
		if (nfds == 0) {
			k_sleep(K_MSEC(100));
			continue;
		}
		
		int ret = zsock_poll(fds, nfds, 100);
		if (ret < 0) {
			LOG_ERR("Poll error: %d", errno);
			k_sleep(K_MSEC(100));
			continue;
		}
		
		if (ret == 0) {
			continue;  /* Timeout */
		}
		
		/* Check which socket has data */
		for (int i = 0; i < nfds; i++) {
			if (!(fds[i].revents & ZSOCK_POLLIN)) {
				continue;
			}
			
			/* Find context for this socket */
			int ctx_id = -1;
			for (int j = 0; j < COAP_MAX_CONTEXTS; j++) {
				if (coap_contexts[j].sock == fds[i].fd) {
					ctx_id = j;
					break;
				}
			}
			
			if (ctx_id < 0) {
				continue;
			}
			
			/* Receive response */
			struct sockaddr_storage src_addr;
			socklen_t src_addr_len = sizeof(src_addr);
			
			ssize_t len = zsock_recvfrom(fds[i].fd, coap_rx_buffer,
						     sizeof(coap_rx_buffer), 0,
						     (struct sockaddr *)&src_addr,
						     &src_addr_len);
			if (len <= 0) {
				continue;
			}
			
			/* Parse CoAP response */
			struct coap_packet response;
			int err = coap_packet_parse(&response, coap_rx_buffer, len,
						    NULL, 0);
			if (err < 0) {
				LOG_ERR("Failed to parse CoAP response: %d", err);
				continue;
			}
			
			/* Extract response code */
			uint8_t code = coap_header_get_code(&response);
			
			/* Extract payload */
			uint16_t payload_len = 0;
			const uint8_t *payload = coap_packet_get_payload(&response, &payload_len);
			len = payload_len;
			if (!payload) {
				len = 0;
			}
			
			/* Convert payload to hex */
			char hex_payload[COAP_MAX_PAYLOAD * 2 + 1] = {0};
			if (len > 0) {
				bin_to_hex(payload, len, hex_payload, sizeof(hex_payload));
			}
			
			/* Send URC: %COAPRECV: <id>,"<path>",<code>,<len>,<hex_payload> */
			/* Note: We don't track the original path in this simple implementation */
			rsp_send("\r\n%%COAPRECV: %d,\"\",\%d,%d,%s\r\n",
				 ctx_id, code, (int)len, hex_payload);
			
			LOG_DBG("CoAP response received: ctx=%d, code=%d, len=%d",
				ctx_id, code, (int)len);
		}
	}
	
	LOG_INF("CoAP thread terminated");
}

/**@brief Start CoAP background thread */
static void start_coap_thread(void)
{
	if (coap_thread_running) {
		return;
	}
	
	coap_thread_running = true;
	k_thread_create(&coap_thread, coap_thread_stack,
			K_THREAD_STACK_SIZEOF(coap_thread_stack),
			coap_thread_fn, NULL, NULL, NULL,
			THREAD_PRIORITY, K_USER, K_NO_WAIT);
	k_thread_name_set(&coap_thread, "coap");
}

/* AT Command Handlers */

SM_AT_CMD_CUSTOM(xcoapcreate, "AT%COAPCREATE", handle_at_coap_create);
static int handle_at_coap_create(enum at_parser_cmd_type cmd_type,
				 struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	
	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = do_coap_create();
		if (err >= 0) {
			rsp_send("\r\n%%COAPCREATE: %d\r\n", err);
			start_coap_thread();
			err = 0;
		} else {
			coap_send_error("COAPCREATE", -1, err);
		}
		break;
		
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n%%COAPCREATE\r\n");
		err = 0;
		break;
		
	default:
		break;
	}
	
	return err;
}

SM_AT_CMD_CUSTOM(xcoapserver, "AT%COAPSERVER", handle_at_coap_server);
static int handle_at_coap_server(enum at_parser_cmd_type cmd_type,
				 struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	
	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count >= 3) {
			uint16_t ctx_id = (uint16_t)-1, port;
			char host[SM_MAX_URL + 1];
			size_t host_sz = sizeof(host);
			sec_tag_t sec_tag = SEC_TAG_TLS_INVALID;
			
			err = at_parser_num_get(parser, 1, &ctx_id);
			if (err) {
				coap_send_error_step("COAPSERVER", -1, "PARSE_CTX", err);
				coap_send_error("COAPSERVER", -1, err);
				return err;
			}
			
			err = util_string_get(parser, 2, host, &host_sz);
			if (err) {
				coap_send_error_step("COAPSERVER", ctx_id, "PARSE_HOST", err);
				coap_send_error("COAPSERVER", ctx_id, err);
				return err;
			}
			
			err = at_parser_num_get(parser, 3, &port);
			if (err) {
				coap_send_error_step("COAPSERVER", ctx_id, "PARSE_PORT", err);
				coap_send_error("COAPSERVER", ctx_id, err);
				return err;
			}
			
			if (param_count > 3) {
				const char *sec_tag_ptr = NULL;
				size_t sec_tag_len = 0;
				char sec_tag_buf[16];
				int sec_tag_int = 0;

				err = at_parser_string_ptr_get(parser, 4, &sec_tag_ptr, &sec_tag_len);
				if (err) {
					/* Treat missing/parse errors as "no sec_tag" to avoid hard failure */
					if (err != -EIO && err != -ENODATA) {
						coap_send_error_step("COAPSERVER", ctx_id, "PARSE_SECTAG", err);
					}
					err = 0;
				} else if (sec_tag_ptr && sec_tag_len > 0) {
					if (sec_tag_len >= sizeof(sec_tag_buf)) {
						err = -EINVAL;
						coap_send_error_step("COAPSERVER", ctx_id, "PARSE_SECTAG", err);
						return err;
					}
					memcpy(sec_tag_buf, sec_tag_ptr, sec_tag_len);
					sec_tag_buf[sec_tag_len] = '\0';

					err = util_str_to_int(sec_tag_buf, 10, &sec_tag_int);
					if (err) {
						coap_send_error_step("COAPSERVER", ctx_id, "PARSE_SECTAG", err);
						return err;
					}
					sec_tag = (sec_tag_t)sec_tag_int;
				}
			}
			
			err = do_coap_set_server(ctx_id, host, port, sec_tag);
			if (err) {
				coap_send_error("COAPSERVER", ctx_id, err);
			}
		} else {
			coap_send_error("COAPSERVER", -1, -EINVAL);
		}
		break;
		
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n%%COAPSERVER: <id>,<host>,<port>[,<sec_tag>]\r\n");
		err = 0;
		break;
		
	default:
		break;
	}
	
	return err;
}

SM_AT_CMD_CUSTOM(xcoapget, "AT%COAPGET", handle_at_coap_get);
static int handle_at_coap_get(enum at_parser_cmd_type cmd_type,
			      struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	
	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count >= 2) {
			uint16_t ctx_id;
			char path[COAP_MAX_PATH_LEN];
			size_t path_sz = sizeof(path);
			
			err = at_parser_num_get(parser, 1, &ctx_id);
			if (err) {
				return err;
			}
			
			err = util_string_get(parser, 2, path, &path_sz);
			if (err) {
				return err;
			}
			
			err = do_coap_request(ctx_id, COAP_METHOD_GET, path, NULL, 0);
			if (err) {
				coap_send_error("COAPGET", ctx_id, err);
			}
		}
		break;
		
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n%%COAPGET: <id>,<path>\r\n");
		err = 0;
		break;
		
	default:
		break;
	}
	
	return err;
}

SM_AT_CMD_CUSTOM(xcoappost, "AT%COAPPOST", handle_at_coap_post);
static int handle_at_coap_post(enum at_parser_cmd_type cmd_type,
			       struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	
	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count >= 3) {
			uint16_t ctx_id;
			char path[COAP_MAX_PATH_LEN];
			size_t path_sz = sizeof(path);
			const char *hex_payload = NULL;
			size_t hex_len = 0;
			uint8_t payload[COAP_MAX_PAYLOAD];
			int payload_len = 0;
			
			err = at_parser_num_get(parser, 1, &ctx_id);
			if (err) {
				return err;
			}
			
			err = util_string_get(parser, 2, path, &path_sz);
			if (err) {
				return err;
			}
			
			/* Get hex payload if present */
			if (param_count > 2) {
				err = at_parser_string_ptr_get(parser, 3, &hex_payload, &hex_len);
				if (err == 0 && hex_payload && hex_len > 0) {
					payload_len = hex_to_bin(hex_payload, hex_len,
								 payload, sizeof(payload));
					if (payload_len < 0) {
						return -EINVAL;
					}
				}
			}
			
			err = do_coap_request(ctx_id, COAP_METHOD_POST, path,
				      payload_len > 0 ? payload : NULL, payload_len);
			if (err) {
				coap_send_error("COAPPOST", ctx_id, err);
			}
		}
		break;
		
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n%%COAPPOST: <id>,<path>[,<hex_payload>]\r\n");
		err = 0;
		break;
		
	default:
		break;
	}
	
	return err;
}

SM_AT_CMD_CUSTOM(xcoapput, "AT%COAPPUT", handle_at_coap_put);
static int handle_at_coap_put(enum at_parser_cmd_type cmd_type,
			      struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	
	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count >= 3) {
			uint16_t ctx_id;
			char path[COAP_MAX_PATH_LEN];
			size_t path_sz = sizeof(path);
			const char *hex_payload = NULL;
			size_t hex_len = 0;
			uint8_t payload[COAP_MAX_PAYLOAD];
			int payload_len = 0;
			
			err = at_parser_num_get(parser, 1, &ctx_id);
			if (err) {
				return err;
			}
			
			err = util_string_get(parser, 2, path, &path_sz);
			if (err) {
				return err;
			}
			
			/* Get hex payload if present */
			if (param_count > 2) {
				err = at_parser_string_ptr_get(parser, 3, &hex_payload, &hex_len);
				if (err == 0 && hex_payload && hex_len > 0) {
					payload_len = hex_to_bin(hex_payload, hex_len,
								 payload, sizeof(payload));
					if (payload_len < 0) {
						return -EINVAL;
					}
				}
			}
			
			err = do_coap_request(ctx_id, COAP_METHOD_PUT, path,
				      payload_len > 0 ? payload : NULL, payload_len);
			if (err) {
				coap_send_error("COAPPUT", ctx_id, err);
			}
		}
		break;
		
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n%%COAPPUT: <id>,<path>[,<hex_payload>]\r\n");
		err = 0;
		break;
		
	default:
		break;
	}
	
	return err;
}

SM_AT_CMD_CUSTOM(xcoapdelete, "AT%COAPDELETE", handle_at_coap_delete);
static int handle_at_coap_delete(enum at_parser_cmd_type cmd_type,
				 struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	
	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count >= 1) {
			uint16_t ctx_id;
			
			err = at_parser_num_get(parser, 1, &ctx_id);
			if (err) {
				return err;
			}
			
			err = do_coap_delete(ctx_id);
			if (err) {
				coap_send_error("COAPDELETE", ctx_id, err);
			}
		}
		break;
		
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n%%COAPDELETE: <id>\r\n");
		err = 0;
		break;
		
	default:
		break;
	}
	
	return err;
}

static int sm_at_coap_init(void)
{
	memset(coap_contexts, 0, sizeof(coap_contexts));
	coap_thread_running = false;
	
	LOG_INF("CoAP AT commands initialized");
	return 0;
}
SYS_INIT(sm_at_coap_init, APPLICATION, 0);
