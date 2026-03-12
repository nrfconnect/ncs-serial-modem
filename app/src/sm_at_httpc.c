/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
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
#define HTTP_FILE_MAX_LEN         256
#define HTTP_REQUEST_BODY_MAX_LEN 1024
#define HTTP_EXTRA_HEADERS_SIZE   512
#define HTTP_RESPONSE_TIMEOUT_MS  30000
#define HTTP_MAX_REQUESTS         2
#define INVALID_HANDLE            -1

/* Forward declaration of socket finding function */
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
	HTTP_STATE_RESOLVING,
	HTTP_STATE_CONNECTING,
	HTTP_STATE_SENDING_REQUEST,
	HTTP_STATE_SENDING_BODY,
	HTTP_STATE_RECEIVING_HEADERS,
	HTTP_STATE_RECEIVING_BODY,
	HTTP_STATE_COMPLETE,
	HTTP_STATE_ERROR,
};

/* HTTP request structure */
struct http_request {
	int handle;                                   /* Unique handle for this request */
	int fd;                                       /* Socket file descriptor (from AT socket) */
	enum http_state state;                        /* Current state */
	enum sm_http_method method;                   /* HTTP method */
	char url[HTTP_URL_MAX_LEN];                   /* Full URL */
	char hostname[HTTP_HOST_MAX_LEN];             /* Hostname */
	char path[HTTP_FILE_MAX_LEN];                 /* URL path */
	uint16_t port;                                /* Port number */
	char request_body[HTTP_REQUEST_BODY_MAX_LEN]; /* Request body */
	int request_body_len;                         /* Request body length */
	char extra_headers[HTTP_EXTRA_HEADERS_SIZE];  /* Extra HTTP headers */
	uint8_t recv_buf[HTTP_RECV_BUF_SIZE];         /* Receive buffer */
	int recv_buf_len;                             /* Bytes in receive buffer */
	char *send_ptr;                               /* Pointer to data being sent */
	int send_remaining;                           /* Bytes remaining to send */
	int status_code;                              /* HTTP status code */
	int content_length;        /* Content-Length from header (-1 if not present) */
	int total_received;        /* Total bytes received */
	bool headers_complete;     /* Headers fully received */
	bool need_rearm_pollin;    /* Flag for socket layer to re-arm POLLIN */
	int64_t timeout_timestamp; /* Idle timeout (resets on each data reception) */
	bool owns_socket;          /* Whether this request owns the socket */
};

static struct http_request http_requests[HTTP_MAX_REQUESTS];
static struct k_mutex http_mutex;
static int next_handle = 1;
static struct http_request *datamode_req; /* Request waiting for body data */

/* Forward declarations */
static void http_process_request(struct http_request *req, uint8_t events);
static void http_close_request(struct http_request *req);
static int http_start_request(struct http_request *req);
static int http_send_http_request(struct http_request *req);
static int http_build_request(struct http_request *req, char *buf, size_t buf_len);
static int parse_http_status_code(const char *buf, int *status_code);
static int parse_content_length(const char *buf, const char *header_end, int *length);

/* Find request by handle */
static struct http_request *find_request(int handle)
{
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i].handle == handle &&
		    http_requests[i].state != HTTP_STATE_IDLE) {
			return &http_requests[i];
		}
	}
	return NULL;
}

/* Allocate new request */
static struct http_request *alloc_request(void)
{
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i].state == HTTP_STATE_IDLE) {
			memset(&http_requests[i], 0, sizeof(struct http_request));
			http_requests[i].handle = next_handle++;
			http_requests[i].fd = -1;
			http_requests[i].state = HTTP_STATE_IDLE;
			http_requests[i].content_length = -1;
			http_requests[i].owns_socket = false;
			http_requests[i].extra_headers[0] = '\0';
			return &http_requests[i];
		}
	}

	return NULL;
}

/* Parse URL into components */
static int http_parse_url_components(const char *url, struct http_request *req)
{
	struct http_parser_url parser = {0};
	int ret;

	LOG_DBG("Parsing URL: %s", url);
	ret = http_parser_parse_url(url, strlen(url), 0, &parser);
	if (ret) {
		LOG_ERR("Failed to parse URL: %d", ret);
		return -EINVAL;
	}
	LOG_DBG("URL parsed successfully, field_set=0x%x", parser.field_set);

	/* Extract hostname */
	if (parser.field_set & (1 << UF_HOST)) {
		unsigned int host_len = parser.field_data[UF_HOST].len;

		if (host_len >= sizeof(req->hostname)) {
			LOG_ERR("Hostname too long");
			return -ENOMEM;
		}
		memcpy(req->hostname, url + parser.field_data[UF_HOST].off, host_len);
		req->hostname[host_len] = '\0';
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

	/* Extract path */
	if (parser.field_set & (1 << UF_PATH)) {
		size_t path_len = parser.field_data[UF_PATH].len;

		if (path_len >= sizeof(req->path)) {
			LOG_ERR("Path too long");
			return -ENOMEM;
		}
		memcpy(req->path, url + parser.field_data[UF_PATH].off, path_len);
		req->path[path_len] = '\0';
	} else {
		strcpy(req->path, "/");
	}

	return 0;
}

/* Start HTTP request (non-blocking) */
static int http_start_request(struct http_request *req)
{
	int ret;
	char request_buf[1024];

	LOG_INF("HTTP %d %s: %s:%d%s", req->method,
		req->method == HTTP_GET      ? "GET"
		: req->method == HTTP_POST   ? "POST"
		: req->method == HTTP_PUT    ? "PUT"
		: req->method == HTTP_DELETE ? "DELETE"
					     : "HEAD",
		req->hostname, req->port, req->path);

	/* Validate socket exists */
	if (req->fd < 0) {
		LOG_ERR("Invalid socket FD");
		return -EINVAL;
	}

	/* Build HTTP request */
	ret = http_build_request(req, request_buf, sizeof(request_buf));
	if (ret < 0) {
		return ret;
	}

	req->send_ptr = request_buf;
	req->send_remaining = ret;
	req->state = HTTP_STATE_SENDING_REQUEST;
	req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;

	LOG_DBG("HTTP %d: Starting request, socket FD=%d", req->handle, req->fd);

	/* Set up XAPOLL events to monitor POLLIN and POLLOUT */
	ret = http_send_http_request(req);

	return ret;
}

/* Build HTTP request headers */
static int http_build_request(struct http_request *req, char *buf, size_t buf_len)
{
	const char *method_str;

	switch (req->method) {
	case HTTP_GET:
		method_str = "GET";
		break;
	case HTTP_POST:
		method_str = "POST";
		break;
	case HTTP_PUT:
		method_str = "PUT";
		break;
	case HTTP_DELETE:
		method_str = "DELETE";
		break;
	case HTTP_HEAD:
		method_str = "HEAD";
		break;
	default:
		return -EINVAL;
	}

	int len = snprintf(buf, buf_len,
			   "%s %s HTTP/1.1\r\n"
			   "Host: %s\r\n"
			   "User-Agent: nRF91-Serial-Modem\r\n"
			   "Accept: */*\r\n"
			   "Connection: close\r\n",
			   method_str, req->path, req->hostname);

	if (req->request_body_len > 0) {
		len += snprintf(buf + len, buf_len - len,
				"Content-Length: %d\r\n",
				req->request_body_len);
	}

	/* Add extra headers if provided */
	if (req->extra_headers[0] != '\0') {
		len += snprintf(buf + len, buf_len - len, "%s", req->extra_headers);
	}

	len += snprintf(buf + len, buf_len - len, "\r\n");

	return len;
}

/* Send HTTP request to socket */
static int http_send_http_request(struct http_request *req)
{
	int ret;
	struct sm_socket *sock;

	/* Send as much as we can immediately */
	ret = nrf_send(req->fd, req->send_ptr, req->send_remaining, NRF_MSG_DONTWAIT);

	if (ret < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* Socket not ready, set up XAPOLL for POLLOUT */
			LOG_DBG("Socket not ready for send, setting POLLOUT");
			sock = find_socket(req->fd);
			if (!sock) {
				return -EINVAL;
			}
			ret = set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
			if (ret) {
				LOG_ERR("Failed to set XAPOLL events: %d", ret);
				return ret;
			}
			return 0;
		}
		LOG_ERR("Send failed: %d", errno);
		return -errno;
	}

	req->send_ptr += ret;
	req->send_remaining -= ret;

	if (req->send_remaining == 0) {
		if (req->request_body_len > 0) {
			req->send_ptr = req->request_body;
			req->send_remaining = req->request_body_len;
			req->state = HTTP_STATE_SENDING_BODY;
			LOG_DBG("HTTP %d: Request sent, sending body", req->handle);
		} else {
			req->state = HTTP_STATE_RECEIVING_HEADERS;
			req->recv_buf_len = 0;
			LOG_DBG("HTTP %d: Request sent, receiving response", req->handle);
		}
	}

	/* Set up XAPOLL for next state */
	sock = find_socket(req->fd);
	if (!sock) {
		return -EINVAL;
	}

	if (req->send_remaining > 0) {
		/* Still need to send more */
		ret = set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
	} else {
		/* Ready to receive */
		ret = set_xapoll_events(sock, NRF_POLLIN);
	}

	if (ret) {
		LOG_ERR("Failed to set XAPOLL events: %d", ret);
		return ret;
	}

	return 0;
}

/* Close and cleanup request */
static void http_close_request(struct http_request *req)
{
	if (req->fd >= 0) {
		/* Stop XAPOLL events for this socket */
		struct sm_socket *sock = find_socket(req->fd);

		if (sock) {
			xapoll_stop(sock);
		}

		/* Only close socket if we own it */
		if (req->owns_socket) {
			nrf_close(req->fd);
		}
		req->fd = -1;
	}
	req->state = HTTP_STATE_IDLE;
	req->handle = INVALID_HANDLE;
	req->owns_socket = false;
}

/* Send error URC */
static void http_send_error(struct http_request *req, int error_code)
{
	rsp_send("\r\n#XHTTPCERR: %d,%d\r\n", req->handle, error_code);
}

/* Send status URC */
static void http_send_status(struct http_request *req)
{
	rsp_send("\r\n#XHTTPCSTAT: %d,%d,%d\r\n", req->handle, req->status_code,
		 req->total_received);
}

/* Send data URC */
static void http_send_data(struct http_request *req, const uint8_t *data, int len)
{
	if (len <= 0) {
		return;
	}

	rsp_send("\r\n#XHTTPCDATA: %d,%d\r\n", req->handle, len);
	data_send(data, len);
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

	value_start = cl_header + 15; /* Skip "Content-Length:" */
	while (*value_start == ':' || *value_start == ' ') {
		value_start++;
	}

	ret = sscanf(value_start, "%d", length);
	if (ret != 1) {
		return -EINVAL;
	}

	return 0;
}

/* Process HTTP request state machine (event-driven via XAPOLL) */
static void http_process_request(struct http_request *req, uint8_t events)
{
	int ret;
	struct sm_socket *sock;

	LOG_DBG("HTTP %d: process_request state=%d events=0x%x time=%lld timeout=%lld", req->handle,
		req->state, events, k_uptime_get(), req->timeout_timestamp);

	/* Check for idle timeout (no activity for HTTP_RESPONSE_TIMEOUT_MS) */
	if (k_uptime_get() > req->timeout_timestamp) {
		LOG_ERR("HTTP request %d timed out in state %d", req->handle, req->state);
		http_send_error(req, -ETIMEDOUT);
		http_close_request(req);
		return;
	}

	/* Handle critical error conditions (but not POLLHUP - it's normal for Connection: close) */
	if (events & (NRF_POLLERR | NRF_POLLNVAL)) {
		LOG_ERR("HTTP %d: Socket error (events=0x%x)", req->handle, events);
		http_send_error(req, -ECONNRESET);
		http_close_request(req);
		return;
	}

	/* POLLHUP during sending is an error, but during receiving it's normal EOF */
	if ((events & NRF_POLLHUP) &&
	    (req->state == HTTP_STATE_SENDING_REQUEST || req->state == HTTP_STATE_SENDING_BODY)) {
		LOG_ERR("HTTP %d: Connection closed during send (events=0x%x)", req->handle,
			events);
		http_send_error(req, -ECONNRESET);
		http_close_request(req);
		return;
	}

	switch (req->state) {
	case HTTP_STATE_SENDING_REQUEST:
	case HTTP_STATE_SENDING_BODY:
		/* Handle writable socket */
		if (events & NRF_POLLOUT) {
			ret = nrf_send(req->fd, req->send_ptr, req->send_remaining,
				       NRF_MSG_DONTWAIT);

			if (ret < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* Need to wait for next POLLOUT */
					sock = find_socket(req->fd);
					if (sock) {
						set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
					}
					return;
				}
				LOG_ERR("Send failed: %d", errno);
				http_send_error(req, -errno);
				http_close_request(req);
				return;
			}

			req->send_ptr += ret;
			req->send_remaining -= ret;

			if (req->send_remaining == 0) {
				if (req->state == HTTP_STATE_SENDING_REQUEST &&
				    req->request_body_len > 0) {
					/* Move to sending body */
					req->send_ptr = req->request_body;
					req->send_remaining = req->request_body_len;
					req->state = HTTP_STATE_SENDING_BODY;
					LOG_DBG("HTTP %d: Request sent, sending body", req->handle);

					/* Continue with POLLOUT to send body */
					sock = find_socket(req->fd);
					if (sock) {
						set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
					}
				} else {
					/* All sent, now wait for response */
					req->state = HTTP_STATE_RECEIVING_HEADERS;
					req->recv_buf_len = 0;
					LOG_DBG("HTTP %d: Request complete, receiving response",
						req->handle);

					/* Set up for receiving */
					sock = find_socket(req->fd);
					if (sock) {
						set_xapoll_events(sock, NRF_POLLIN);
					}
				}
			} else {
				/* More to send */
				sock = find_socket(req->fd);
				if (sock) {
					set_xapoll_events(sock, NRF_POLLOUT | NRF_POLLIN);
				}
			}
		}
		break;

	case HTTP_STATE_RECEIVING_HEADERS:
	case HTTP_STATE_RECEIVING_BODY:
		/* Handle readable socket */
		if (events & NRF_POLLIN) {
			ret = nrf_recv(req->fd, req->recv_buf + req->recv_buf_len,
				       sizeof(req->recv_buf) - req->recv_buf_len - 1,
				       NRF_MSG_DONTWAIT);

			if (ret < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					LOG_DBG("Recv would block (normal for non-blocking I/O)");
					/* Socket buffer empty - re-arm POLLIN to wait for more data
					 */
					sock = find_socket(req->fd);
					if (sock) {
						set_xapoll_events(sock, NRF_POLLIN);
					}
					return;
				}
				LOG_ERR("Recv failed: %d", errno);
				http_send_error(req, -errno);
				http_close_request(req);
				return;
			} else if (ret == ETIMEDOUT) {
				LOG_ERR("Recv timed out");
				http_send_error(req, -ETIMEDOUT);
				http_close_request(req);
				return;
			}

			if (ret == 0) {
				/* Connection closed */
				LOG_DBG("HTTP %d: Connection closed (EOF)", req->handle);

				/* Check if we received all expected data */
				if (req->content_length > 0 &&
				    req->total_received < req->content_length) {
					LOG_WRN("HTTP %d: Incomplete transfer - received %d/%d "
						"bytes",
						req->handle, req->total_received,
						req->content_length);
				}

				http_send_status(req);
				http_close_request(req);
				return;
			}

			/* Data received - update idle timeout (resets on each recv) */
			req->recv_buf_len += ret;
			req->recv_buf[req->recv_buf_len] = '\0';
			req->total_received += ret;
			req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;

			if (req->state == HTTP_STATE_RECEIVING_HEADERS) {
				/* Look for end of headers */
				char *header_end = strstr((char *)req->recv_buf, "\r\n\r\n");

				if (header_end) {
					int ret;

					/* Parse status code from first line */
					ret = parse_http_status_code((char *)req->recv_buf,
								     &req->status_code);
					if (ret < 0) {
						LOG_DBG("Status code not found or invalid");
					}

					/* Parse Content-Length header (case-insensitive) */
					ret = parse_content_length((char *)req->recv_buf,
								   header_end,
								   &req->content_length);
					if (ret == 0) {
						LOG_INF("HTTP %d: Content-Length=%d", req->handle,
							req->content_length);
					}

					req->headers_complete = true;
					req->state = HTTP_STATE_RECEIVING_BODY;

					/* Send body data after headers */
					int body_offset = header_end - (char *)req->recv_buf + 4;
					int body_len = req->recv_buf_len - body_offset;

					if (body_len > 0) {
						http_send_data(req, req->recv_buf + body_offset,
							       body_len);
					}

					req->recv_buf_len = 0;
					LOG_DBG("HTTP %d: Headers complete, status %d", req->handle,
						req->status_code);
				}

				/* Check if buffer is full */
				if (req->recv_buf_len >= sizeof(req->recv_buf) - 1) {
					LOG_ERR("HTTP headers too large");
					http_send_error(req, -ENOMEM);
					http_close_request(req);
					return;
				}
			} else {
				/* Receiving body - send data immediately */
				http_send_data(req, req->recv_buf, req->recv_buf_len);
				req->recv_buf_len = 0;
			}

			/* Set flag for socket layer to re-arm POLLIN for continuous reception */
			LOG_DBG("HTTP %d: Setting need_rearm_pollin flag (state=%d)", req->handle,
				req->state);
			req->need_rearm_pollin = true;
		}

		/* Handle POLLHUP (connection closed by server) */
		if (events & NRF_POLLHUP) {
			LOG_DBG("HTTP %d: Connection closed by server (POLLHUP)", req->handle);

			/* Drain any remaining data from socket buffer before closing */
			while (req->state == HTTP_STATE_RECEIVING_BODY) {
				ret = nrf_recv(req->fd, req->recv_buf, sizeof(req->recv_buf) - 1,
					       NRF_MSG_DONTWAIT);

				if (ret <= 0) {
					/* No more data available */
					break;
				}

				/* Data available - send it and reset idle timeout */
				req->recv_buf[ret] = '\0';
				req->total_received += ret;
				req->timeout_timestamp = k_uptime_get() + HTTP_RESPONSE_TIMEOUT_MS;
				http_send_data(req, req->recv_buf, ret);
				LOG_DBG("HTTP %d: Drained %d bytes on POLLHUP", req->handle, ret);
			}

			/* If we received headers, this is a normal completion */
			if (req->headers_complete) {
				/* Check if we received all expected data */
				if (req->content_length > 0 &&
				    req->total_received < req->content_length) {
					LOG_WRN("HTTP %d: Incomplete transfer on POLLHUP - "
						"received %d/%d bytes",
						req->handle, req->total_received,
						req->content_length);
				}

				http_send_status(req);
			} else {
				/* Closed before receiving complete headers */
				http_send_error(req, -ECONNRESET);
			}
			http_close_request(req);
			return;
		}
		break;

	case HTTP_STATE_COMPLETE:
	case HTTP_STATE_ERROR:
	case HTTP_STATE_IDLE:
		/* Nothing to do */
		break;

	default:
		LOG_ERR("Invalid state: %d", req->state);
		break;
	}
}

/* XAPOLL callback - called when socket has events */
static bool http_xapoll_callback(int fd, uint8_t events)
{
	struct http_request *req = NULL;

	LOG_DBG("XAPOLL callback fd=%d events=0x%x", fd, events);

	k_mutex_lock(&http_mutex, K_FOREVER);

	/* Find request with this socket FD */
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i].fd == fd && http_requests[i].state != HTTP_STATE_IDLE) {
			req = &http_requests[i];
			break;
		}
	}

	if (req) {
		/* Clear flag before processing */
		req->need_rearm_pollin = false;
		http_process_request(req, events);
	}

	k_mutex_unlock(&http_mutex);

	/* Return true if HTTP client needs POLLIN re-armed */
	return (req && req->need_rearm_pollin);
}

/* Public function called by socket layer when poll events occur */
bool sm_at_httpc_poll_event(int fd, uint8_t events)
{
	return http_xapoll_callback(fd, events);
}

/* Data mode callback for receiving POST/PUT body */
static int http_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int err = 0;

	if (op == DATAMODE_SEND) {
		if (!datamode_req) {
			LOG_ERR("No request for data mode");
			exit_datamode_handler(-EINVAL);
			return -EINVAL;
		}

		/* Store received body data */
		if (datamode_req->request_body_len + len > sizeof(datamode_req->request_body)) {
			LOG_ERR("Body data overflow: %d + %d > %d", datamode_req->request_body_len,
				len, sizeof(datamode_req->request_body));
			exit_datamode_handler(-EOVERFLOW);
			return -EOVERFLOW;
		}

		memcpy(datamode_req->request_body + datamode_req->request_body_len, data, len);
		datamode_req->request_body_len += len;
		LOG_DBG("Received %d bytes, total %d", len, datamode_req->request_body_len);
		return len; /* Return number of bytes accepted */
	} else if (op == DATAMODE_EXIT) {
		LOG_DBG("Data mode exit, body_len=%d",
			datamode_req ? datamode_req->request_body_len : 0);

		if (datamode_req) {
			/* Start the HTTP request now that we have the body */
			err = http_start_request(datamode_req);
			if (err) {
				LOG_ERR("Failed to start request: %d", err);
				http_close_request(datamode_req);
				rsp_send("\r\nERROR\r\n");
			} else {
				rsp_send("\r\n#XHTTPCREQ: %d\r\n", datamode_req->handle);
				rsp_send("\r\nOK\r\n");
			}
			datamode_req = NULL;
		}

		if ((flags & SM_DATAMODE_FLAGS_EXIT_HANDLER) != 0) {
			/* Datamode exited unexpectedly */
			rsp_send(CONFIG_SM_DATAMODE_TERMINATOR);
		}
	}

	return 0;
}

/* AT#XHTTPCREQ - Start HTTP/HTTPS request (async) */
SM_AT_CMD_CUSTOM(xhttpcreq, "AT#XHTTPCREQ", handle_at_httpcreq);
STATIC int handle_at_httpcreq(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	int err;
	int socket_fd;
	char url[HTTP_URL_MAX_LEN];
	int method;
	size_t len;
	struct http_request *req;
	struct sm_socket *sock;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		if (param_count < 3) {
			LOG_ERR("Insufficient parameters: %d", param_count);
			return -EINVAL;
		}

		/* Get socket FD */
		err = at_parser_num_get(parser, 1, &socket_fd);
		if (err) {
			LOG_ERR("Failed to parse socket FD: %d", err);
			return err;
		}
		LOG_DBG("Socket FD: %d", socket_fd);

		/* Validate socket exists */
		sock = find_socket(socket_fd);
		if (!sock) {
			LOG_ERR("Invalid socket FD: %d", socket_fd);
			return -EINVAL;
		}

		/* Get URL */
		len = sizeof(url);
		err = util_string_get(parser, 2, url, &len);
		if (err) {
			LOG_ERR("Failed to parse URL: %d", err);
			return err;
		}
		LOG_DBG("URL: %s", url);

		/* Get method */
		err = at_parser_num_get(parser, 3, &method);
		if (err) {
			LOG_ERR("Failed to parse method: %d", err);
			return err;
		}
		LOG_DBG("Method: %d", method);

		if (method < HTTP_GET || method > HTTP_HEAD) {
			LOG_ERR("Invalid method: %d", method);
			return -EINVAL;
		}

		LOG_DBG("Total param_count: %d", param_count);

		/* Get optional body length for POST/PUT (data mode) */
		int body_len = 0;
		int next_param_idx = 4; /* Next parameter index after method */

		if (param_count > 3 && (method == HTTP_POST || method == HTTP_PUT)) {
			err = at_parser_num_get(parser, next_param_idx, &body_len);
			if (err && err != -ENOENT) {
				LOG_ERR("Failed to parse body_len: %d", err);
				return err;
			}
			if (err != -ENOENT) {
				if (body_len < 0 || body_len > HTTP_REQUEST_BODY_MAX_LEN) {
					LOG_ERR("Invalid body_len: %d (max %d)", body_len,
						HTTP_REQUEST_BODY_MAX_LEN);
					return -EINVAL;
				}
				LOG_DBG("Body length: %d", body_len);
			}
			/* Move to next parameter after body_len slot */
			next_param_idx++;
		}

		/* Allocate request */
		req = alloc_request();
		if (!req) {
			LOG_ERR("No free request slots");
			return -ENOMEM;
		}
		LOG_DBG("Allocated request handle=%d", req->handle);

		/* Setup request */
		req->fd = socket_fd;
		req->owns_socket = false; /* Socket is managed by AT socket commands */
		strncpy(req->url, url, sizeof(req->url) - 1);
		req->method = method;
		req->request_body_len = 0;
		req->extra_headers[0] = '\0';

		/* Parse extra headers (parameters 5 onwards) */
		LOG_DBG("Parsing extra headers from index %d, param_count=%d", next_param_idx,
			param_count);
		for (int i = next_param_idx; i <= param_count; i++) {
			const char *header_ptr;
			size_t header_len;

			err = at_parser_string_ptr_get(parser, i, &header_ptr, &header_len);
			if (err) {
				/* Could be empty parameter or non-existent - skip it */
				LOG_DBG("Skipping parameter %d (err=%d)", i, err);
				continue;
			}

			/* Skip empty strings */
			if (header_len == 0 || header_ptr[0] == '\0') {
				LOG_DBG("Skipping empty header at param %d", i);
				continue;
			}

			/* Append header with \r\n */
			size_t current_len = strlen(req->extra_headers);
			int ret = snprintf(req->extra_headers + current_len,
					   sizeof(req->extra_headers) - current_len,
					   "%.*s\r\n", (int)header_len, header_ptr);

			if (ret >= (int)(sizeof(req->extra_headers) - current_len)) {
				LOG_ERR("Extra headers buffer overflow");
				http_close_request(req);
				return -ENOMEM;
			}
			LOG_DBG("Added extra header: %.*s", (int)header_len, header_ptr);
		}

		/* Parse URL */
		LOG_DBG("Parsing URL: %s", url);
		err = http_parse_url_components(url, req);
		if (err) {
			LOG_ERR("URL parse failed: %d", err);
			http_close_request(req);
			return err;
		}
		LOG_DBG("URL parsed: host=%s port=%d path=%s", req->hostname, req->port, req->path);

		/* For POST/PUT with body, enter data mode */
		if ((method == HTTP_POST || method == HTTP_PUT) && body_len > 0) {
			LOG_INF("Entering data mode for %d bytes", body_len);
			datamode_req = req;
			err = enter_datamode(http_datamode_callback, body_len);
			if (err) {
				LOG_ERR("Failed to enter data mode: %d", err);
				datamode_req = NULL;
				http_close_request(req);
				return err;
			}
			/* OK is sent by AT framework before entering data mode */
			/* Response (#XHTTPCREQ) will be sent when data mode exits */
			return 0;
		} else {
			/* Start request immediately (no body or GET/DELETE/HEAD) */
			LOG_DBG("Starting HTTP request");
			err = http_start_request(req);
			if (err) {
				LOG_ERR("Start request failed: %d", err);
				http_close_request(req);
				return err;
			}

			/* Return handle to user */
			rsp_send("\r\n#XHTTPCREQ: %d\r\n", req->handle);
		}
		err = 0;
		break;
	}

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XHTTPCREQ: <socket_fd>,<url>,<method>"
			 "[,<body_len>[,<header>]...]\r\n");
		rsp_send("Methods: 0=GET, 1=POST, 2=PUT, 3=DELETE, 4=HEAD\r\n");
		rsp_send("body_len: Length of body data (POST/PUT enter data mode)\r\n");
		rsp_send("header: Extra HTTP header (multiple allowed, e.g. \"Authorization: "
			 "Bearer token\")\r\n");
		err = 0;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

/* AT#XHTTPCCON - HTTP client connection control/status */
SM_AT_CMD_CUSTOM(xhttpccon, "AT#XHTTPCCON", handle_at_httpccon);
STATIC int handle_at_httpccon(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	int err = 0;
	int handle;
	int operation;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &handle);
		if (err) {
			LOG_ERR("Failed to parse handle: %d", err);
			return err;
		}

		LOG_INF("Handle: %d, param_count: %d", handle, param_count);
		if (param_count > 2) {
			err = at_parser_num_get(parser, 2, &operation);

			if (err) {
				LOG_ERR("Failed to parse operation: %d", err);
				return err;
			}
			LOG_INF("Operation: %d", operation);
			/* Both handle and operation provided */
			if (operation == 0) {
				/* Cancel/close request */
				struct http_request *req = find_request(handle);

				if (req) {
					LOG_INF("Cancelling HTTP request %d", handle);
					http_send_error(req, -ECANCELED);
					http_close_request(req);
				} else {
					LOG_ERR("Failed to find request for handle: %d", handle);
					return -EINVAL;
				}
			} else {
				return -EINVAL;
			}

		} else if (param_count == 2) {
			/* Only handle provided - query mode */
			struct http_request *req = find_request(handle);

			if (req) {
				rsp_send("\r\n#XHTTPCCON: %d,%d\r\n", req->handle, req->state);
			} else {
				/* Handle not found - completed or never existed */
				/* Return IDLE state to indicate not active */
				rsp_send("\r\n#XHTTPCCON: %d,%d\r\n", handle, HTTP_STATE_IDLE);
			}
		} else {
			return -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_READ:
		/* List all active requests */
		LOG_INF("Listing active HTTP requests:");
		k_mutex_lock(&http_mutex, K_FOREVER);
		for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
			if (http_requests[i].state != HTTP_STATE_IDLE) {
				rsp_send("\r\n#XHTTPCCON: %d,%d\r\n", http_requests[i].handle,
					 http_requests[i].state);
			}
		}
		k_mutex_unlock(&http_mutex);
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XHTTPCCON: <handle>[,<operation>]\r\n");
		rsp_send("Query: <handle> only - returns state\r\n");
		rsp_send("Operations: 0=Cancel\r\n");
		err = 0;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

int sm_at_httpc_init(void)
{
	/* Initialize mutex */
	k_mutex_init(&http_mutex);

	/* Initialize all requests to idle */
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		http_requests[i].state = HTTP_STATE_IDLE;
		http_requests[i].handle = INVALID_HANDLE;
		http_requests[i].fd = -1;
		http_requests[i].owns_socket = false;
	}

	LOG_INF("HTTP client initialized (XAPOLL-based)");
	return 0;
}

int sm_at_httpc_uninit(void)
{
	k_mutex_lock(&http_mutex, K_FOREVER);

	/* Close all active requests */
	for (int i = 0; i < HTTP_MAX_REQUESTS; i++) {
		if (http_requests[i].state != HTTP_STATE_IDLE) {
			http_close_request(&http_requests[i]);
		}
	}

	k_mutex_unlock(&http_mutex);

	return 0;
}

#if defined(CONFIG_SM_HTTPC)
SYS_INIT(sm_at_httpc_init, APPLICATION, 10);
#endif
