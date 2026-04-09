/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#define _POSIX_C_SOURCE 200809L /* for strdup() */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/net/http/parser_url.h>
#include <nrf_socket.h>
#include <modem/at_parser.h>
#include <stdio.h>
#include <string.h>
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_httpc.h"

LOG_MODULE_REGISTER(sm_httpc, CONFIG_SM_LOG_LEVEL);

#define HTTP_RECV_BUF_SIZE        2048
#define HTTP_URL_MAX_LEN          512
#define HTTP_HOST_MAX_LEN         256
#define HTTP_PATH_MAX_LEN         256
#define HTTP_REQUEST_BODY_MAX_LEN CONFIG_SM_DATAMODE_BUF_SIZE
#define HTTP_EXTRA_HEADERS_SIZE   512
#define HTTP_RESPONSE_TIMEOUT_MS  CONFIG_SM_HTTPC_RESPONSE_TIMEOUT_MS
#define HTTP_MAX_REQUESTS         NRF_MODEM_MAX_SOCKET_COUNT

/* Periodic scan, so idle timeout fires without a socket poll wakeup (silent server). */
#define HTTP_TIMEOUT_SCAN_MS MIN(1000U, (uint32_t)HTTP_RESPONSE_TIMEOUT_MS / 4U)

/* Fixed HTTP header lines */
#define HTTP_VERSION_LINE    " HTTP/1.1\r\n"
#define HTTP_HDR_USER_AGENT  "User-Agent: nRF91-Serial-Modem\r\n"
#define HTTP_HDR_ACCEPT      "Accept: */*\r\n"

/* Forward declarations of socket functions */
extern struct sm_socket *find_socket(int fd);
extern int set_xapoll_events(struct sm_socket *sock, uint8_t events);
extern void xapoll_stop(struct sm_socket *sock);

/* HTTP request methods */
enum sm_http_method {
	HTTP_GET,
	HTTP_POST,
	HTTP_PUT,
	HTTP_DELETE,
	HTTP_HEAD,
};

/* HTTP request states */
enum http_state {
	HTTP_STATE_IDLE,
	HTTP_STATE_SENDING_REQUEST,
	HTTP_STATE_SENDING_BODY,
	HTTP_STATE_RECEIVING_HEADERS,
	HTTP_STATE_RECEIVING_BODY,
};

/* HTTP request structure */
struct http_request {
	int fd;                     /* Socket file descriptor (from AT socket) */
	enum http_state state;      /* Current state */
	enum sm_http_method method; /* HTTP method */
	char *hostname;             /* Hostname (dynamically allocated) */
	char *path;                 /* URL path (dynamically allocated) */
	uint16_t port;              /* Port number */
	int request_body_len;       /* Content-Length for request body (POST/PUT) */
	char *extra_headers;        /* Extra HTTP headers (dynamically allocated) */
	uint8_t *recv_buf;          /* Receive buffer (dynamically allocated) */
	int recv_buf_len;           /* Bytes in receive buffer */
	char *send_ptr;             /* Pointer to data being sent */
	int send_remaining;         /* Bytes remaining to send */
	char *send_buf;             /* Buffer for HTTP request headers (dynamically allocated) */
	int status_code;            /* HTTP status code */
	int content_length;         /* Content-Length from header (-1 if not present) */
	int total_received;         /* Total bytes received */
	bool headers_complete;      /* Headers fully received */
	bool need_rearm_pollin;     /* Flag for socket layer to re-arm POLLIN */
	int64_t timeout_timestamp;  /* Idle timeout deadline; reset on each send/receive */
	struct modem_pipe *pipe;    /* AT pipe that created this request */
	bool manual_mode;           /* Manual mode: body not auto-received, host pulls chunks */
	int bytes_sent;             /* Response-body bytes sent to the host */
};

static const char * const http_method_str[] = {
	[HTTP_GET]    = "GET",
	[HTTP_POST]   = "POST",
	[HTTP_PUT]    = "PUT",
	[HTTP_DELETE] = "DELETE",
	[HTTP_HEAD]   = "HEAD",
};

static struct http_request *http_requests[HTTP_MAX_REQUESTS];
static struct http_request *datamode_req; /* Request waiting for body data */

/* Forward declarations */
static void http_process_request(struct http_request *req, uint8_t events);
static void http_close_request(struct http_request *req);
static void http_fail_request(struct http_request *req);
static void http_finish_request(struct http_request *req);
static int http_start_request(struct http_request *req);
static bool http_headers_complete(struct http_request *req, char *header_end,
				  struct sm_socket *sock, bool hup);
static int parse_http_status_code(const char *buf, int *status_code);
static int parse_content_length(const char *buf, const char *header_end, int *length);
static void http_timeout_work_fn(struct k_work *work);

static K_MUTEX_DEFINE(http_mutex);
static K_WORK_DELAYABLE_DEFINE(http_timeout_dwork,  http_timeout_work_fn);

static bool http_any_active_request_unlocked(void)
{
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i] != NULL && http_requests[i]->state != HTTP_STATE_IDLE) {
			return true;
		}
	}

	return false;
}

/* Arm the scan timer when a request enters a non-idle state (safe without http_mutex). */
static void http_timeout_monitor_arm(void)
{
	(void)k_work_reschedule_for_queue(&sm_work_q, &http_timeout_dwork,
					  K_MSEC(HTTP_TIMEOUT_SCAN_MS));
}

/*
 * Sole enforcement of HTTP idle timeout (SM_HTTPC_RESPONSE_TIMEOUT_MS sliding window).
 * Runs on sm_work_q; reschedules while any request remains active.
 */
static void http_timeout_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&http_mutex, K_FOREVER);

	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		struct http_request *req = http_requests[i];

		if (req == NULL || req->state == HTTP_STATE_IDLE) {
			continue;
		}

		/* Body is being streamed via data mode; data mode manages its own
		 * lifecycle. Do not apply the response idle timeout here.
		 */
		if (req->state == HTTP_STATE_SENDING_BODY) {
			continue;
		}

		if (k_uptime_get() > req->timeout_timestamp) {
			LOG_ERR("HTTP request %d idle timeout state %d", req->fd, req->state);
			http_fail_request(req);
		}
	}

	const bool any = http_any_active_request_unlocked();

	k_mutex_unlock(&http_mutex);

	if (any) {
		http_timeout_monitor_arm();
	}
}

/* Find request by socket fd */
static struct http_request *find_request(int fd)
{
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i] && http_requests[i]->fd == fd) {
			return http_requests[i];
		}
	}
	return NULL;
}

/* Allocate new request */
static struct http_request *alloc_request(void)
{
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (!http_requests[i]) {
			http_requests[i] = malloc(sizeof(struct http_request));
			if (!http_requests[i]) {
				return NULL;
			}
			memset(http_requests[i], 0, sizeof(struct http_request));
			http_requests[i]->recv_buf = malloc(HTTP_RECV_BUF_SIZE);
			if (!http_requests[i]->recv_buf) {
				free(http_requests[i]);
				http_requests[i] = NULL;
				return NULL;
			}
			http_requests[i]->fd = -1;
			http_requests[i]->state = HTTP_STATE_IDLE;
			http_requests[i]->content_length = -1;
			http_requests[i]->pipe = sm_at_host_get_current_pipe();
			return http_requests[i];
		}
	}

	return NULL;
}

/* Parse URL into components */
static int http_parse_url_components(const char *url, size_t url_len, struct http_request *req)
{
	struct http_parser_url parser = {0};
	int ret;

	ret = http_parser_parse_url(url, url_len, 0, &parser);
	if (ret) {
		LOG_ERR("Failed to parse URL: %d", ret);
		return -EINVAL;
	}

	/* Extract hostname */
	if (parser.field_set & (1 << UF_HOST)) {
		unsigned int host_len = parser.field_data[UF_HOST].len;

		if (host_len >= HTTP_HOST_MAX_LEN) {
			LOG_ERR("Hostname too long");
			return -EINVAL;
		}
		req->hostname = strndup(url + parser.field_data[UF_HOST].off, host_len);
		if (!req->hostname) {
			return -ENOMEM;
		}
	} else {
		LOG_ERR("No host in URL");
		return -EINVAL;
	}

	/* Extract port if specified */
	if (parser.field_set & (1 << UF_PORT)) {
		req->port = parser.port;
	} else {
		/* Determine port based on scheme */
		if (parser.field_set & (1 << UF_SCHEMA)) {
			unsigned int schema_len = parser.field_data[UF_SCHEMA].len;
			const char *scheme_start = url + parser.field_data[UF_SCHEMA].off;

			if (schema_len == 5 && strncmp(scheme_start, "https", 5) == 0) {
				req->port = 443;
			} else {
				req->port = 80;
			}
		} else {
			req->port = 80;
		}
	}

	/* Extract path (and query string if present, e.g. /foo?bar=1) */
	if (parser.field_set & (1 << UF_PATH)) {
		size_t path_len = parser.field_data[UF_PATH].len;
		size_t query_len = 0;

		if (parser.field_set & (1 << UF_QUERY)) {
			/* query offset immediately follows path; include '?' separator */
			query_len = 1 + parser.field_data[UF_QUERY].len;
		}

		size_t total_len = path_len + query_len;

		if (total_len >= HTTP_PATH_MAX_LEN) {
			LOG_ERR("Path+query too long");
			return -EINVAL;
		}
		req->path = strndup(url + parser.field_data[UF_PATH].off, total_len);
		if (!req->path) {
			return -ENOMEM;
		}
	} else {
		req->path = strdup("/");
		if (!req->path) {
			return -ENOMEM;
		}
	}

	return 0;
}

/* Build HTTP request headers */
static int http_build_request(struct http_request *req, char *buf, size_t buf_len)
{
	int len = snprintf(buf, buf_len,
			   "%s %s" HTTP_VERSION_LINE
			   "Host: %s\r\n"
			   HTTP_HDR_USER_AGENT
			   HTTP_HDR_ACCEPT,
			   http_method_str[req->method], req->path, req->hostname);

	if (req->request_body_len > 0) {
		len += snprintf(buf + len, buf_len - len, "Content-Length: %d\r\n",
				req->request_body_len);
	}

	/* Add extra headers if provided */
	if (req->extra_headers != NULL) {
		len += snprintf(buf + len, buf_len - len, "%s", req->extra_headers);
	}

	len += snprintf(buf + len, buf_len - len, "\r\n");

	return len;
}

/* Compile-time length of a string literal (excludes null terminator) */
#define STRLIT_LEN(s) (sizeof(s) - 1)

/* Calculate the buffer size needed to hold the HTTP request headers */
static size_t http_headers_size(const struct http_request *req)
{
	return strlen(http_method_str[req->method]) + 1 +
	       strlen(req->path) + STRLIT_LEN(HTTP_VERSION_LINE) +
	       STRLIT_LEN("Host: ") + strlen(req->hostname) + STRLIT_LEN("\r\n") +
	       STRLIT_LEN(HTTP_HDR_USER_AGENT) +
	       STRLIT_LEN(HTTP_HDR_ACCEPT) +
	       (req->request_body_len > 0 ?
			STRLIT_LEN("Content-Length: 2147483647\r\n") : 0) +
	       (req->extra_headers ? strlen(req->extra_headers) : 0) +
	       STRLIT_LEN("\r\n") + 1; /* final blank line + null terminator */
}

/*
 * Allocate req->send_buf and fill it with the HTTP request header block.
 * Returns the number of bytes written, or a negative error code.
 */
static int http_alloc_build_headers(struct http_request *req)
{
	size_t send_buf_size = http_headers_size(req);

	req->send_buf = malloc(send_buf_size);
	if (!req->send_buf) {
		LOG_ERR("Failed to allocate send buffer (%zu bytes)", send_buf_size);
		return -ENOMEM;
	}

	return http_build_request(req, req->send_buf, send_buf_size);
}

/* Start HTTP request (non-blocking) */
static int http_start_request(struct http_request *req)
{
	int ret;
	struct sm_socket *sock;

	LOG_INF("HTTP %d %s: %s:%d%s", req->method, http_method_str[req->method],
		req->hostname, req->port, req->path);

	ret = http_alloc_build_headers(req);
	if (ret < 0) {
		return ret;
	}

	req->send_ptr = req->send_buf;
	req->send_remaining = ret;
	req->state = HTTP_STATE_SENDING_REQUEST;
	req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;

	sock = find_socket(req->fd);
	if (!sock) {
		return -EINVAL;
	}

	ret = set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
	if (ret) {
		LOG_ERR("Failed to set XAPOLL events: %d", ret);
		return ret;
	}

	http_timeout_monitor_arm();

	return 0;
}

/* Close and cleanup request */
static void http_close_request(struct http_request *req)
{
	if (req->hostname != NULL) {
		free(req->hostname);
		req->hostname = NULL;
	}

	if (req->path != NULL) {
		free(req->path);
		req->path = NULL;
	}

	if (req->extra_headers != NULL) {
		free(req->extra_headers);
		req->extra_headers = NULL;
	}

	if (req->recv_buf != NULL) {
		free(req->recv_buf);
		req->recv_buf = NULL;
	}

	if (req->send_buf != NULL) {
		free(req->send_buf);
		req->send_buf = NULL;
	}

	if (req->fd >= 0) {
		/* Stop XAPOLL events for this socket */
		struct sm_socket *sock = find_socket(req->fd);

		if (sock) {
			xapoll_stop(sock);
		}

		req->fd = -1;
	}
	/* Clear datamode_req if it points to this request */
	if (datamode_req == req) {
		datamode_req = NULL;
	}

	/* Free this request and clear its slot */
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i] == req) {
			http_requests[i] = NULL;
			break;
		}
	}
	free(req);
}

/* Send error via XHTTPCSTAT with -1 status code */
static void http_send_error(struct http_request *req)
{
	urc_send_to(req->pipe, "\r\n#XHTTPCSTAT: %d,-1,%d\r\n", req->fd, req->total_received);
}

/* Send status URC */
static void http_send_status(struct http_request *req)
{
	urc_send_to(req->pipe, "\r\n#XHTTPCSTAT: %d,%d,%d\r\n", req->fd, req->status_code,
		 req->total_received);
}

/* Send error and close request */
static void http_fail_request(struct http_request *req)
{
	http_send_error(req);
	http_close_request(req);
}

/* Send status URC and close request (successful completion) */
static void http_finish_request(struct http_request *req)
{
	http_send_status(req);
	http_close_request(req);
}

/* Send headers complete URC */
static void http_send_headers_complete(struct http_request *req)
{
	urc_send_to(req->pipe, "\r\n#XHTTPCHEAD: %d,%d,%d\r\n", req->fd, req->status_code,
		 req->content_length);
}

/* Send data URC followed by raw bytes */
static void http_send_data(struct http_request *req, const uint8_t *data, int len)
{
	if (len <= 0) {
		return;
	}

	urc_send_to(req->pipe, "\r\n#XHTTPCDATA: %d,%d,%d\r\n", req->fd, req->bytes_sent, len);
	req->bytes_sent += len;
	data_send(req->pipe, data, len);
}

/* Parse HTTP status code from response buffer */
static int parse_http_status_code(const char *buf, int *status_code)
{
	char *status_line;
	int ret;

	status_line = strstr(buf, "HTTP/1.");
	if (!status_line) {
		return -ENOENT;
	}

	ret = sscanf(status_line, "HTTP/1.%*d %d", status_code);
	if (ret != 1) {
		LOG_WRN("Failed to parse status code");
		return -EINVAL;
	}

	return 0;
}

/* Parse Content-Length header from response buffer */
static int parse_content_length(const char *buf, const char *header_end, int *length)
{
	char *cl_header;
	char *value_start;
	int ret;

	cl_header = strstr(buf, "Content-Length:");
	if (!cl_header) {
		cl_header = strstr(buf, "content-length:");
	}

	if (!cl_header || cl_header >= header_end) {
		return -ENOENT;
	}

	value_start = cl_header + STRLIT_LEN("Content-Length:"); /* Skip "Content-Length:" */
	while (*value_start == ' ') {
		value_start++;
	}

	ret = sscanf(value_start, "%d", length);
	if (ret != 1) {
		return -EINVAL;
	}

	return 0;
}

/* Called when the complete HTTP response header has been received.
 * Parses status/content-length, handles piggybacked body data, and notifies the host.
 * Returns true if the http request was finished (prematurely).
 */
static bool http_headers_complete(struct http_request *req, char *header_end,
				  struct sm_socket *sock, bool hup)
{
	int ret;
	int body_offset = header_end - (char *)req->recv_buf + STRLIT_LEN("\r\n\r\n");
	int body_len = req->recv_buf_len - body_offset;

	ret = parse_http_status_code((char *)req->recv_buf, &req->status_code);
	if (ret < 0) {
		LOG_WRN("HTTP %d: Failed to parse status code: %d", req->fd, ret);
	}

	ret = parse_content_length((char *)req->recv_buf, header_end, &req->content_length);
	if (ret == 0) {
		LOG_INF("HTTP %d: Content-Length=%d", req->fd, req->content_length);
	} else {
		LOG_DBG("HTTP %d: No Content-Length header", req->fd);
	}

	req->headers_complete = true;
	req->state = HTTP_STATE_RECEIVING_BODY;

	http_send_headers_complete(req);

	/* HEAD responses never carry a body (RFC 9110 §9.3.2). Finish now. */
	if (req->method == HTTP_HEAD) {
		http_finish_request(req);
		return true;
	}

	/* 1xx, 204 No Content and 304 Not Modified have no body (RFC 9110 §6.3). */
	if (req->status_code / 100 == 1 ||
	    req->status_code == 204 ||
	    req->status_code == 304) {
		http_finish_request(req);
		return true;
	}

	/* Adjust total_received to only count body bytes */
	req->total_received -= body_offset;

	if (req->manual_mode) {
		/*
		 * Keep piggybacked body bytes (if any) for the first pull.
		 * Stop XAPOLL so only the host drives reception via
		 * AT#XHTTPCDATA=<socket_fd>.
		 */
		if (body_len > 0)
			memmove(req->recv_buf, req->recv_buf + body_offset, body_len);
		req->recv_buf_len = body_len;
		LOG_DBG("HTTP %d: Headers complete (manual), status %d, piggybacked=%d",
			req->fd, req->status_code, req->recv_buf_len);
		xapoll_stop(sock);
		/* need_rearm_pollin stays false */
		return true;
	}

	/* Auto mode: send any piggybacked body bytes immediately */
	if (body_len > 0)
		http_send_data(req, req->recv_buf + body_offset, body_len);

	/* Clear buffer for next recv */
	req->recv_buf_len = 0;
	LOG_DBG("HTTP %d: Headers complete, status %d", req->fd, req->status_code);

	/*
	 * If POLLHUP co-fired the connection is already closing; finish now
	 * rather than re-arming POLLIN on a socket that will never fire again.
	 */
	if (hup) {
		if (req->content_length > 0 && req->bytes_sent < req->content_length)
			LOG_WRN("HTTP %d: Incomplete transfer - received %d/%d bytes",
				req->fd, req->bytes_sent, req->content_length);
		http_finish_request(req);
		return true;
	}

	/*
	 * All body bytes arrived piggybacked with the headers and
	 * content-length is satisfied.  With keep-alive connections
	 * there is no subsequent EOF to trigger the completion check
	 * in the RECEIVING_BODY path, so finish here instead.
	 */
	if (req->content_length >= 0 && req->bytes_sent >= req->content_length) {
		http_finish_request(req);
		return true;
	}

	return false;
}

/* Process HTTP request state machine (event-driven via XAPOLL) */
static void http_process_request(struct http_request *req, uint8_t events)
{
	int ret;
	struct sm_socket *sock = find_socket(req->fd);

	if (!sock) {
		LOG_ERR("HTTP %d: Socket not found", req->fd);
		http_fail_request(req);
		return;
	}

	LOG_DBG("HTTP %d: process_request state=%d events=0x%x time=%lld timeout=%lld", req->fd,
		req->state, events, k_uptime_get(), req->timeout_timestamp);

	/* POLLERR/POLLNVAL are always fatal; POLLHUP is handled per-state below. */
	if (events & (NRF_POLLERR | NRF_POLLNVAL)) {
		LOG_ERR("HTTP %d: Socket error (events=0x%x)", req->fd, events);
		http_fail_request(req);
		return;
	}

	/* POLLHUP during sending means the server closed the connection unexpectedly. */
	if ((events & NRF_POLLHUP) && req->state == HTTP_STATE_SENDING_REQUEST) {
		LOG_ERR("HTTP %d: Connection closed during send (events=0x%x)", req->fd,
			events);
		http_fail_request(req);
		return;
	}

	switch (req->state) {
	case HTTP_STATE_SENDING_REQUEST:
		/* Handle writable socket */
		if (events & NRF_POLLOUT) {
			ret = nrf_send(req->fd, req->send_ptr, req->send_remaining,
				       NRF_MSG_DONTWAIT);

			if (ret < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* Need to wait for next POLLOUT */
					set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
					return;
				}
				LOG_ERR("Send failed: %d", errno);
				http_fail_request(req);
				return;
			}

			req->send_ptr += ret;
			req->send_remaining -= ret;

			if (req->send_remaining == 0) {
				/* All headers sent, now wait for response */
				req->state = HTTP_STATE_RECEIVING_HEADERS;
				req->recv_buf_len = 0;
				set_xapoll_events(sock, NRF_POLLIN);
			} else {
				/* More to send */
				set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
			}
		}
		break;

	case HTTP_STATE_RECEIVING_HEADERS:
	case HTTP_STATE_RECEIVING_BODY:
		/* POLLHUP without POLLIN before headers are received is an error:
		 * the server closed the connection before sending a valid response.
		 */
		if ((events & NRF_POLLHUP) && !(events & NRF_POLLIN) &&
		    !req->headers_complete) {
			LOG_ERR("HTTP %d: Connection closed before headers (POLLHUP)", req->fd);
			http_fail_request(req);
			return;
		}

		/* Handle POLLIN */
		if (events & (NRF_POLLIN)) {
			ret = nrf_recv(req->fd, req->recv_buf + req->recv_buf_len,
				       HTTP_RECV_BUF_SIZE - req->recv_buf_len - 1,
				       NRF_MSG_DONTWAIT);

			if (ret < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* Socket buffer empty - re-arm POLLIN to wait for more data
					 */
					set_xapoll_events(sock, NRF_POLLIN);
					return;
				}
				if (errno == ETIMEDOUT) {
					LOG_ERR("Recv timed out");
					http_fail_request(req);
					return;
				}
				LOG_ERR("Recv failed: %d", errno);
				http_fail_request(req);
				return;
			}

			if (ret == 0) {
				/* Connection closed by server (EOF) */
				if (!req->headers_complete) {
					/* Closed before headers arrived */
					http_fail_request(req);
					return;
				}

				/* Check if we received all expected data */
				if (req->content_length > 0 &&
				    req->total_received < req->content_length) {
					LOG_WRN("HTTP %d: Incomplete transfer - received %d/%d "
						"bytes",
						req->fd, req->total_received,
						req->content_length);
				}

				http_finish_request(req);
				return;
			}

			/* Data received - update idle timeout */
			req->recv_buf_len += ret;
			req->recv_buf[req->recv_buf_len] = '\0';
			req->total_received += ret;
			req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;

			if (req->state == HTTP_STATE_RECEIVING_HEADERS) {
				/* Look for end of headers */
				char *header_end = strstr((char *)req->recv_buf, "\r\n\r\n");

				if (header_end) {
					if (http_headers_complete(req, header_end, sock,
								  events & NRF_POLLHUP)) {
						return;
					}
					req->need_rearm_pollin = true;
					return;
				}

				/* Check if buffer is full */
				if (req->recv_buf_len >= HTTP_RECV_BUF_SIZE - 1) {
					LOG_ERR("HTTP headers too large");
					http_fail_request(req);
					return;
				}
			} else {
				/* Receiving body */
				if (req->manual_mode) {
					/*
					 * POLLIN fired in body state after xapoll_stop (race).
					 * nrf_recv already consumed bytes from the socket buffer
					 * into recv_buf and incremented total_received. Keep
					 * recv_buf intact so the host can pull it; do NOT reset
					 * recv_buf_len or the data is silently lost.
					 */
					xapoll_stop(sock);
					return;
				}
				http_send_data(req, req->recv_buf, req->recv_buf_len);
				req->recv_buf_len = 0;

				/* Finish if content-length satisfied, or if the connection
				 * is already closing (POLLHUP co-fired) — no point re-arming
				 * POLLIN on a closing socket; nrf_recv would return EAGAIN.
				 */
				if ((req->content_length > 0 &&
				     req->bytes_sent >= req->content_length) ||
				    (events & NRF_POLLHUP)) {
					http_finish_request(req);
					return;
				}
			}

			/* Set flag for socket layer to re-arm POLLIN for continuous reception */
			req->need_rearm_pollin = true;
		}
		break;

	case HTTP_STATE_IDLE:
		/* Nothing to do */
		break;

	default:
		LOG_ERR("Invalid state: %d", req->state);
		break;
	}
}

/* Public function called by socket layer when poll events occur */
bool sm_at_httpc_poll_event(int fd, uint8_t events)
{
	struct http_request *req = NULL;

	k_mutex_lock(&http_mutex, K_FOREVER);

	req = find_request(fd);

	if (req) {
		req->need_rearm_pollin = false;
		http_process_request(req, events);
		/* req may have been freed by http_process_request; re-find safely */
		req = find_request(fd);
	}

	k_mutex_unlock(&http_mutex);

	return (req && req->need_rearm_pollin);
}

/* Build HTTP request headers and send them synchronously for streaming POST/PUT */
static int http_send_request_headers(struct http_request *req)
{
	int ret;
	int sent;
	int n;

	LOG_INF("HTTP %d %s (streaming): %s:%d%s", req->method,
		http_method_str[req->method], req->hostname, req->port, req->path);

	ret = http_alloc_build_headers(req);
	if (ret < 0) {
		return ret;
	}

	req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;

	/* Send headers synchronously (blocking) */
	sent = 0;
	while (sent < ret) {
		n = nrf_send(req->fd, req->send_buf + sent, ret - sent, 0);
		if (n < 0) {
			return -errno;
		}
		sent += n;
		req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;
	}

	free(req->send_buf);
	req->send_buf = NULL;

	/* Headers sent; body will now be streamed via data mode. */
	req->state = HTTP_STATE_SENDING_BODY;

	http_timeout_monitor_arm();

	return 0;
}

/* Data mode callback: streams POST/PUT body directly to socket */
static int http_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int err = 0;

	if (op == DATAMODE_SEND) {
		if ((flags & SM_DATAMODE_FLAGS_MORE_DATA) != 0) {
			LOG_ERR("Data mode buffer overflow");
			exit_datamode_handler(sm_at_host_get_current(), -EOVERFLOW);
			return -EOVERFLOW;
		}

		if (!datamode_req) {
			LOG_ERR("No request for data mode");
			exit_datamode_handler(sm_at_host_get_current(), -EINVAL);
			return -EINVAL;
		}

		/* Stream body chunk directly to the socket */
		int sent = 0;

		while (sent < len) {
			int ret = nrf_send(datamode_req->fd, data + sent, len - sent, 0);

			if (ret < 0) {
				int err_code = -errno;
				struct http_request *req_failed = datamode_req;

				datamode_req = NULL;
				LOG_ERR("Failed to stream request body: %d", err_code);
				exit_datamode_handler(sm_at_host_get_current(), err_code);
				http_fail_request(req_failed);
				return err_code;
			}
			sent += ret;
		}

		/* Slide response idle window: slow UART upload is activity, not server wait. */
		if (len > 0) {
			k_mutex_lock(&http_mutex, K_FOREVER);
			if (datamode_req != NULL) {
				datamode_req->timeout_timestamp =
					k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;
			}
			k_mutex_unlock(&http_mutex);
		}

		return len;
	} else if (op == DATAMODE_EXIT) {
		if (datamode_req) {
			if (flags & SM_DATAMODE_FLAGS_EXIT_HANDLER) {
				/* Data mode exited unexpectedly - body not fully sent */
				rsp_send(CONFIG_SM_DATAMODE_TERMINATOR);
				LOG_WRN("HTTP %d: Data mode exited unexpectedly",
					datamode_req->fd);
				http_fail_request(datamode_req);
			} else {
				/* Body fully sent; arm XAPOLL to receive the response */
				struct sm_socket *sock = find_socket(datamode_req->fd);

				if (sock) {
					datamode_req->state = HTTP_STATE_RECEIVING_HEADERS;
					datamode_req->recv_buf_len = 0;
					datamode_req->timeout_timestamp =
						k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;
					err = set_xapoll_events(sock, NRF_POLLIN);
					if (err) {
						LOG_ERR("Failed to arm XAPOLL: %d", err);
						http_fail_request(datamode_req);
					}
				} else {
					LOG_ERR("HTTP %d: Socket not found after body send",
						datamode_req->fd);
					http_fail_request(datamode_req);
				}
			}
			datamode_req = NULL;
		}

		if (flags & SM_DATAMODE_FLAGS_EXIT_HANDLER) {
			rsp_send(CONFIG_SM_DATAMODE_TERMINATOR);
		}
	}

	return 0;
}

/* AT#XHTTPCREQ - Start an HTTP/HTTPS request (async) */
SM_AT_CMD_CUSTOM(xhttpcreq, "AT#XHTTPCREQ", handle_at_httpcreq);
STATIC int handle_at_httpcreq(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	int err;
	int socket_fd;
	const char *url;
	size_t url_len;
	int method;
	struct http_request *req;
	struct sm_socket *sock;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		if (param_count < 4) {
			return -EINVAL;
		}

		/* Get socket FD */
		err = at_parser_num_get(parser, 1, &socket_fd);
		if (err) {
			return err;
		}

		/* Validate socket exists */
		sock = find_socket(socket_fd);
		if (!sock) {
			LOG_ERR("Invalid socket FD: %d", socket_fd);
			return -EINVAL;
		}

		/* Reject if a request is already active on this socket, and allocate
		 * the new request slot atomically.  Both find_request() and
		 * alloc_request() touch http_requests[], so they must be performed
		 * under the same lock to prevent races with http_process_request()
		 * running on the XAPOLL thread.
		 */
		k_mutex_lock(&http_mutex, K_FOREVER);
		if (find_request(socket_fd)) {
			k_mutex_unlock(&http_mutex);
			LOG_ERR("Request already active on socket %d", socket_fd);
			return -EBUSY;
		}
		req = alloc_request();
		k_mutex_unlock(&http_mutex);
		if (!req) {
			LOG_ERR("No free request slots");
			return -ENOMEM;
		}

		/* Get URL */
		err = at_parser_string_ptr_get(parser, 2, &url, &url_len);
		if (err) {
			http_close_request(req);
			return err;
		}
		if (url_len == 0 || url_len >= HTTP_URL_MAX_LEN) {
			LOG_ERR("URL length invalid: %zu", url_len);
			http_close_request(req);
			return -EINVAL;
		}

		/* Get method */
		err = at_parser_num_get(parser, 3, &method);
		if (err) {
			http_close_request(req);
			return err;
		}

		if (method < HTTP_GET || method > HTTP_HEAD) {
			http_close_request(req);
			return -EINVAL;
		}

		int body_len = 0;
		int next_param_idx = 4; /* Next parameter index after method */

		/* Parse optional <auto_reception> flag */
		int auto_reception = 1; /* default: auto */

		if (param_count > next_param_idx) {
			if (at_parser_num_get(parser, next_param_idx, &auto_reception) == 0) {
				if (auto_reception != 0 && auto_reception != 1) {
					http_close_request(req);
					return -EINVAL;
				}
				next_param_idx++;
			}
			/* If parse fails (string param), leave next_param_idx unchanged. */
		}

		/* Parse optional body length (data mode for POST/PUT).
		 * The parameter slot is always consumed when an integer is present
		 * so that extra headers start at a consistent index regardless of method.
		 */
		if (param_count > next_param_idx) {
			int tmp = 0;

			if (at_parser_num_get(parser, next_param_idx, &tmp) == 0) {
				if (method == HTTP_POST || method == HTTP_PUT) {
					body_len = tmp;
					if (body_len < 0 || body_len > HTTP_REQUEST_BODY_MAX_LEN) {
						LOG_ERR("Invalid body_len: %d (max %d)", body_len,
							HTTP_REQUEST_BODY_MAX_LEN);
						http_close_request(req);
						return -EINVAL;
					}
				} else if (tmp != 0) {
					LOG_ERR("body_len must be 0 for method %d", method);
					http_close_request(req);
					return -EINVAL;
				}
				next_param_idx++;
			}
		}

		/* Setup request */
		req->fd = socket_fd;
		req->method = method;

		/* Parse extra headers (remaining string params) */
		size_t headers_total_len = 0;

		for (int i = next_param_idx; i <= param_count; i++) {
			const char *header_ptr;
			size_t header_len;

			err = at_parser_string_ptr_get(parser, i, &header_ptr, &header_len);
			if (err || header_len == 0 || header_ptr[0] == '\0') {
				continue;
			}

			headers_total_len += header_len + 2;
			if (headers_total_len + 1 > HTTP_EXTRA_HEADERS_SIZE) {
				LOG_ERR("Extra headers too large (%zu > %d)", headers_total_len + 1,
					HTTP_EXTRA_HEADERS_SIZE);
				http_close_request(req);
				return -ENOMEM;
			}
		}

		if (headers_total_len > 0) {
			char *hdr;
			size_t pos = 0;

			hdr = malloc(headers_total_len + 1);
			if (!hdr) {
				LOG_ERR("Failed to allocate extra headers (%zu bytes)",
					headers_total_len + 1);
				http_close_request(req);
				return -ENOMEM;
			}

			for (int i = next_param_idx; i <= param_count; i++) {
				const char *header_ptr;
				size_t header_len;

				err = at_parser_string_ptr_get(parser, i, &header_ptr, &header_len);
				if (err || header_len == 0 || header_ptr[0] == '\0') {
					continue;
				}

				memcpy(&hdr[pos], header_ptr, header_len);
				pos += header_len;
				hdr[pos++] = '\r';
				hdr[pos++] = '\n';
			}

			hdr[pos] = '\0';
			req->extra_headers = hdr;
		}

		/* Enable manual mode when auto reception is disabled */
		if (!auto_reception) {
			req->manual_mode = true;
			LOG_INF("HTTP %d: Manual mode enabled", req->fd);
		}

		/* Parse URL */
		err = http_parse_url_components(url, url_len, req);
		if (err) {
			http_close_request(req);
			return err;
		}

		/* For POST/PUT with body, send headers now then stream body via data mode */
		if ((method == HTTP_POST || method == HTTP_PUT) && body_len > 0) {
			LOG_INF("Streaming %d bytes body", body_len);
			req->request_body_len = body_len;
			err = http_send_request_headers(req);
			if (err) {
				LOG_ERR("Failed to send request headers: %d", err);
				http_close_request(req);
				return err;
			}
			datamode_req = req;
			/* Send #XHTTPCREQ before the OK so the host gets the socket fd
			 * before data mode is entered.
			 */
			rsp_send("\r\n#XHTTPCREQ: %d\r\n", req->fd);
			err = enter_datamode(http_datamode_callback, body_len);
			if (err) {
				LOG_ERR("Failed to enter data mode: %d", err);
				datamode_req = NULL;
				http_close_request(req);
				return err;
			}
			/* AT framework sends OK; #XDATAMODE: 0 follows when data mode exits */
			return 0;
		}

		/* Start request immediately (no body or GET/DELETE/HEAD) */
		err = http_start_request(req);
		if (err) {
			http_close_request(req);
			return err;
		}

		/* Return socket fd to user */
		rsp_send("\r\n#XHTTPCREQ: %d\r\n", req->fd);

		err = 0;
		break;
	}

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XHTTPCREQ: <socket_fd>,<url>,<method>"
			 "[,<auto_reception>[,<body_len>[,<header>]...]]\r\n");

		err = 0;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/* Pull one chunk of body data for a manual-mode request */
static int pull_data(int socket_fd, int pull_len)
{
	struct http_request *req;
	int ret;

	k_mutex_lock(&http_mutex, K_FOREVER);
	req = find_request(socket_fd);
	if (!req) {
		k_mutex_unlock(&http_mutex);
		LOG_ERR("Socket fd %d not found", socket_fd);
		return -EINVAL;
	}

	if (!req->manual_mode) {
		k_mutex_unlock(&http_mutex);
		LOG_ERR("Socket fd %d not in manual mode", socket_fd);
		return -EINVAL;
	}

	if (!req->headers_complete) {
		k_mutex_unlock(&http_mutex);
		LOG_ERR("Headers not yet received on socket %d", socket_fd);
		return -EAGAIN;
	}

	/*
	 * Drain any bytes already in recv_buf first (piggybacked
	 * body from header reception, or from a POLLIN race).
	 * Must be checked before fd < 0: POLLHUP may have fired
	 * and stopped XAPOLL, but recv_buf can still hold data.
	 * Honour pull_len: if more bytes are buffered than requested,
	 * send only pull_len and keep the rest for the next pull.
	 */
	if (req->recv_buf_len > 0) {
		int send_len = MIN(req->recv_buf_len, pull_len);

		req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;
		rsp_send("\r\n#XHTTPCDATA: %d,%d,%d\r\n", req->fd, req->bytes_sent, send_len);
		req->bytes_sent += send_len;
		data_send(req->pipe, req->recv_buf, send_len);
		req->recv_buf_len -= send_len;
		if (req->recv_buf_len > 0) {
			memmove(req->recv_buf, req->recv_buf + send_len, req->recv_buf_len);
		}
		goto check_complete;
	}

	/* Do one non-blocking recv and send to host */
	ret = nrf_recv(req->fd, req->recv_buf, pull_len, NRF_MSG_DONTWAIT);

	if (ret < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* No data ready yet */
			k_mutex_unlock(&http_mutex);
			rsp_send("\r\n#XHTTPCDATA: %d,%d,0\r\n", req->fd, req->bytes_sent);
			return 0;
		}
		LOG_ERR("Recv failed: %d", errno);
		http_fail_request(req);
		k_mutex_unlock(&http_mutex);
		return -errno;
	}

	if (ret == 0) {
		/* EOF - connection closed by server */
		http_finish_request(req);
		k_mutex_unlock(&http_mutex);
		return 0;
	}

	req->total_received += ret;
	req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;
	rsp_send("\r\n#XHTTPCDATA: %d,%d,%d\r\n", req->fd, req->bytes_sent, ret);
	req->bytes_sent += ret;
	data_send(req->pipe, req->recv_buf, ret);

check_complete:
	if (req->content_length > 0 && req->bytes_sent >= req->content_length) {
		http_finish_request(req);
		k_mutex_unlock(&http_mutex);
		return 0;
	}

	k_mutex_unlock(&http_mutex);
	return 0;
}

/* AT#XHTTPCDATA - Pull body chunk in manual mode */
SM_AT_CMD_CUSTOM(xhttpcdata, "AT#XHTTPCDATA", handle_at_httpcdata);
STATIC int handle_at_httpcdata(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	int err;
	int socket_fd;
	int pull_len = HTTP_RECV_BUF_SIZE - 1;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		if (param_count < 2) {
			return -EINVAL;
		}

		err = at_parser_num_get(parser, 1, &socket_fd);
		if (err) {
			return err;
		}

		if (param_count >= 3) {
			int tmp;

			if (at_parser_num_get(parser, 2, &tmp) == 0 && tmp > 0)
				pull_len = MIN(tmp, HTTP_RECV_BUF_SIZE - 1);
		}

		err = pull_data(socket_fd, pull_len);
		break;
	}

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XHTTPCDATA: <socket_fd>[,<length>]\r\n");

		err = 0;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/* AT#XHTTPCCANCEL - cancel an active HTTP request */
SM_AT_CMD_CUSTOM(xhttpccancel, "AT#XHTTPCCANCEL", handle_at_httpccancel);
STATIC int handle_at_httpccancel(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				 uint32_t param_count)
{
	int err = 0;
	int socket_fd;
	struct http_request *req;

	ARG_UNUSED(param_count);

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &socket_fd);
		if (err) {
			return err;
		}

		k_mutex_lock(&http_mutex, K_FOREVER);
		req = find_request(socket_fd);
		if (req) {
			LOG_INF("Cancelling HTTP request fd=%d", socket_fd);
			http_fail_request(req);
			k_mutex_unlock(&http_mutex);
		} else {
			k_mutex_unlock(&http_mutex);
			LOG_ERR("Failed to find request for fd: %d", socket_fd);
			return -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XHTTPCCANCEL: <socket_fd>\r\n");
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}
