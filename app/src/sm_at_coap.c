/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/util.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/socket.h>
#include "sm_at_host.h"
#include "sm_defines.h"
#include "sm_util.h"

LOG_MODULE_REGISTER(sm_coap, CONFIG_SM_LOG_LEVEL);

#define COAP_PATH_MAX_LEN (CONFIG_COAP_CLIENT_MAX_PATH_LENGTH + 1)

#define COAP_SEM_TIMEOUT K_SECONDS(30)

/* Maximum time to wait for the host to drain a response block via AT#XCOAPCDATA
 * before aborting the request and sending a #XCOAPCSTAT error URC.
 */
#define COAP_HOST_PULL_TIMEOUT COAP_SEM_TIMEOUT

/* Maximum time to wait for coap_client to consume a staging block via payload_cb
 * before aborting the upload.
 */
#define COAP_BLOCK_SEND_TIMEOUT COAP_SEM_TIMEOUT

/* Forward declaration for AT socket lookup. */
extern struct sm_socket *find_socket(int fd);

/* Single CoAP client instance, registered permanently via SYS_INIT. */
static struct coap_client sm_coap_client;

/**
 * Per-request state, dynamically allocated for the lifetime of one transaction.
 *
 * Two payload paths depending on declared size:
 *
 * Small (≤ CONFIG_COAP_CLIENT_BLOCK_SIZE bytes):
 *   All bytes are accumulated in staging[] during DATAMODE_SEND.  DATAMODE_EXIT
 *   calls coap_start_request() with coap_req.payload = staging and coap_req.len =
 *   staging_filled.  No payload_cb, no semaphores.  The library stores the pointer
 *   internally and reuses it for Block2 continuation requests, which is safe since
 *   staging[] is alive until coap_close_request().
 *
 * Large (> CONFIG_COAP_CLIENT_BLOCK_SIZE bytes):
 *   UART → data mode ring buffer → coap_datamode_callback (DATAMODE_SEND)
 *       → staging[] → coap_payload_cb (called by coap_client background thread)
 *       → coap_client internal send buffer
 *   The two threads synchronise via a pair of semaphores:
 *     staging_ready:    data mode signals when staging[] is full (or final block)
 *     staging_consumed: coap_client signals after copying staging[] to its send buffer
 */
struct coap_request {
	int fd;                         /* Socket file descriptor (from AT socket) */
	struct modem_pipe *pipe;        /* Pipe for URC delivery */
	/* Stored parameters */
	enum coap_method method;
	char path[COAP_PATH_MAX_LEN];
	bool confirmable;
	enum coap_content_format content_format;
	/* General extra CoAP options parsed from (<opt_num>,<opt_val>)* pairs. */
	struct coap_client_option extra_options[CONFIG_COAP_CLIENT_MAX_EXTRA_OPTIONS];
	uint8_t num_extra_options;
	/* Streaming payload (block-by-block, no large malloc) */
	size_t payload_len;             /* Total declared payload length */
	size_t payload_sent;            /* Bytes handed to coap_client so far */
	bool coap_started;              /* coap_client_req has been called */
	/* One-block staging buffer filled by data mode, consumed by payload_cb.
	 * Allocated (CONFIG_COAP_CLIENT_BLOCK_SIZE bytes) only for requests with a payload.
	 */
	uint8_t *staging;
	size_t staging_filled;          /* Bytes currently in staging[] */
	struct k_sem staging_ready;     /* Signaled: staging[] has data for coap_client */
	struct k_sem staging_consumed;  /* Signaled: coap_client has consumed staging[] */
	bool payload_aborted;           /* Set on data mode error; payload_cb returns -EIO */
	/* Response tracking */
	size_t bytes_sent;              /* Bytes delivered to host via URCs */
	int status_code;                /* CoAP response code (or -1 on failure) */
	/* Manual receive mode (auto_reception=0): host pulls response blocks
	 * via AT#XCOAPCDATA instead of receiving them automatically as URCs.
	 *
	 * coap_callback() fills rx_buf[], sets rx_buf_filled, then blocks on
	 * rx_consumed until the host calls AT#XCOAPCDATA.  The pull handler
	 * drains rx_buf[] to the host and gives rx_consumed to unblock the
	 * callback.  For Block2 (multi-block) responses this naturally throttles
	 * each subsequent block request to the host's pull rate.
	 */
	bool manual_rx;                 /* true: manual pull mode enabled */
	bool hex_rx;                    /* true: deliver response payload as ASCII hex string */
	uint8_t *rx_buf;                /* Block-sized buffer (CONFIG_COAP_CLIENT_BLOCK_SIZE) */
	size_t rx_buf_filled;           /* Bytes currently in rx_buf[] */
	struct k_sem rx_consumed;       /* Given by AT#XCOAPCDATA; taken by coap_callback */
};

static struct coap_request *coap_pending_req;
static K_MUTEX_DEFINE(coap_mutex);

/* Forward declarations */
static void coap_close_request(struct coap_request *req);

static struct coap_request *alloc_request(void)
{
	if (coap_pending_req) {
		return NULL;
	}

	coap_pending_req = calloc(1, sizeof(struct coap_request));
	if (!coap_pending_req) {
		return NULL;
	}

	coap_pending_req->pipe = sm_at_host_get_current_pipe();
	k_sem_init(&coap_pending_req->staging_ready, 0, 1);
	k_sem_init(&coap_pending_req->staging_consumed, 0, 1);
	k_sem_init(&coap_pending_req->rx_consumed, 0, 1);

	return coap_pending_req;
}

static void coap_close_request(struct coap_request *req)
{
	if (!req) {
		return;
	}

	if (req == coap_pending_req) {
		coap_pending_req = NULL;
	}

	sm_coap_client.fd = -1;

	free(req->staging);
	free(req->rx_buf);
	free(req);
}

static void coap_send_status(struct coap_request *req)
{
	urc_send_to(req->pipe, "\r\n#XCOAPCSTAT: %d,%d,%d\r\n", req->fd, req->status_code,
		    (int)req->bytes_sent);
}

static void coap_data_send_hex(struct modem_pipe *pipe, const uint8_t *buf, size_t len)
{
	char hex_buf[257];
	size_t chunk = (sizeof(hex_buf) - 1) / 2;
	size_t done = 0;

	while (done < len) {
		size_t n = MIN(chunk, len - done);
		size_t sz = bin2hex(buf + done, n, hex_buf, sizeof(hex_buf));

		data_send(pipe, hex_buf, sz);
		done += n;
	}
}

static void coap_send_data(struct coap_request *req, const uint8_t *payload, size_t payload_len)
{
	if (!payload || payload_len == 0) {
		return;
	}

	/* Lock the AT host to prevent URCs from being injected between the notification
	 * header, the data bytes, and the trailing CRLF.
	 */
	sm_at_host_lock(req->pipe);
	rsp_send_to(req->pipe, "\r\n#XCOAPCDATA: %d,%d,%d\r\n", req->fd,
		    (int)req->bytes_sent, (int)payload_len);
	req->bytes_sent += payload_len;
	if (req->hex_rx) {
		coap_data_send_hex(req->pipe, payload, payload_len);
	} else {
		data_send(req->pipe, payload, payload_len);
	}
	/* CRLF after the data chunk, consistent with socket auto-receive behaviour. */
	rsp_send_to(req->pipe, "\r\n");
	sm_at_host_unlock(req->pipe);
}

/* Notify host that a response block is ready to pull in manual receive mode. */
static void coap_send_head(struct coap_request *req, int code, size_t block_len)
{
	urc_send_to(req->pipe, "\r\n#XCOAPCHEAD: %d,%d,%d\r\n", req->fd, code,
		    (int)block_len);
}

/* Send status URC and close the request. Caller must not access req after this. */
static void coap_finish_request(struct coap_request *req)
{
	coap_send_status(req);
	k_mutex_lock(&coap_mutex, K_FOREVER);
	coap_close_request(req);
	k_mutex_unlock(&coap_mutex);
}

/* Report failure and close the request. Caller must return immediately after. */
static void coap_fail_request(struct coap_request *req)
{
	req->status_code = -1;
	coap_finish_request(req);
}

static void coap_callback(const struct coap_client_response_data *data, void *user_data)
{
	struct coap_request *req = user_data;

	if (!req) {
		return;
	}

	if (!data || data->result_code < 0) {
		int err = data ? (int)data->result_code : -EINVAL;

		LOG_ERR("CoAP request failed: %d", err);
		coap_fail_request(req);
		return;
	}

	LOG_DBG("CoAP response callback: result_code=%d, payload_len=%d, last_block=%d",
		data->result_code, (int)data->payload_len, data->last_block);

	req->status_code = data->result_code;

	if (data->payload && data->payload_len > 0) {
		if (req->manual_rx) {
			/* Buffer the block and notify the host via #XCOAPCHEAD. The
			 * host must call AT#XCOAPCDATA to drain rx_buf[] and unblock
			 * this callback via rx_consumed.  For Block2 this naturally
			 * throttles the next block request to the host's pull rate.
			 */
			if (data->payload_len > CONFIG_COAP_CLIENT_BLOCK_SIZE) {
				LOG_ERR("CoAP block too large: %zu > %d",
					data->payload_len, CONFIG_COAP_CLIENT_BLOCK_SIZE);
				coap_fail_request(req);
				return;
			}
			k_mutex_lock(&coap_mutex, K_FOREVER);
			memcpy(req->rx_buf, data->payload, data->payload_len);
			req->rx_buf_filled = data->payload_len;
			k_mutex_unlock(&coap_mutex);
			coap_send_head(req, data->result_code, data->payload_len);
			/* Wait for the host to drain rx_buf[] via AT#XCOAPCDATA.
			 * Use a bounded timeout to avoid stalling the coap_client
			 * background thread indefinitely if the host stops pulling.
			 */
			if (k_sem_take(&req->rx_consumed, COAP_HOST_PULL_TIMEOUT) != 0) {
				LOG_ERR("Timed out waiting for AT#XCOAPCDATA (handle=%d)",
					req->fd);
				coap_fail_request(req);
				return;
			}
		} else {
			coap_send_data(req, data->payload, data->payload_len);
		}
	}

	if (data->last_block) {
		coap_finish_request(req);
	}
}

/**
 * coap_payload_cb() - called by coap_client to supply the request body, one block at a time.
 *
 * Called synchronously from coap_client_req() for block 0 (on the AT handler thread),
 * then from the coap_client background thread for each subsequent Block1 upload block.
 * For Block2 response-continuation requests the library also re-invokes this callback
 * (with offset=0) to reconstruct the request packet; in that case payload_sent >= payload_len
 * and the already-cached staging[] contents are returned immediately without blocking.
 *
 * The library copies staging[] into its own send buffer before returning, so staging[]
 * is safe to reuse for the next block as soon as staging_consumed is signalled.
 */
static int coap_payload_cb(size_t offset, const uint8_t **payload, size_t *len,
			   bool *last_block, void *user_data)
{
	struct coap_request *req = user_data;

	ARG_UNUSED(offset);

	/* For Block2 response continuation, coap_client re-invokes payload_cb for every
	 * response block request with offset=0.  Data mode has already exited at that point,
	 * so we must not block on staging_ready.  staging[] is never overwritten after the
	 * last block, so return it directly.
	 */
	if (req->payload_sent >= req->payload_len) {
		*payload = req->staging;
		*len = req->staging_filled;
		*last_block = true;
		return 0;
	}

	/* Wait until data mode has filled a block (or the final partial block). */
	if (k_sem_take(&req->staging_ready, COAP_SEM_TIMEOUT) != 0) {
		return -EIO;
	}

	if (req->payload_aborted) {
		return -EIO;
	}

	*payload = req->staging;
	*len = req->staging_filled;
	*last_block = (req->payload_sent + req->staging_filled >= req->payload_len);

	req->payload_sent += req->staging_filled;

	/* Signal data mode to fill the next block.  Not needed for the last block
	 * since DATAMODE_SEND does not wait for staging_consumed after handing over
	 * the final block.
	 */
	if (!*last_block) {
		k_sem_give(&req->staging_consumed);
	}

	LOG_DBG("CoAP payload_cb: offset=%zu, len=%zu, last=%d",
		offset, *len, (int)*last_block);
	return 0;
}

static int coap_start_request(struct coap_request *req)
{
	struct coap_client_request coap_req = {0};
	int ret;

	req->coap_started = true;

	LOG_INF("Starting CoAP request: method=%d, path=%s, confirmable=%d, content_format=%d, "
		"payload_len=%d, num_options=%d",
		req->method, req->path, req->confirmable, req->content_format,
		(int)req->payload_len, (int)req->num_extra_options);

	coap_req.method = req->method;
	coap_req.confirmable = req->confirmable;
	coap_req.fmt = req->content_format;
	coap_req.cb = coap_callback;
	coap_req.user_data = req;
	memcpy(coap_req.path, req->path, strlen(req->path) + 1);

	memcpy(coap_req.options, req->extra_options,
	       req->num_extra_options * sizeof(struct coap_client_option));
	coap_req.num_options = req->num_extra_options;

	if (req->payload_len > 0 && req->payload_len <= CONFIG_COAP_CLIENT_BLOCK_SIZE) {
		/* Small payload already fully buffered in staging[].  The library copies
		 * it before coap_client_req() returns and reuses the same pointer for any
		 * Block2 continuation requests, which is safe since staging[] remains
		 * allocated until coap_close_request().
		 */
		coap_req.payload = req->staging;
		coap_req.len = req->payload_len;
	} else if (req->payload_len > 0) {
		/* Large payload: stream to the library block-by-block via payload_cb.
		 * Pass the known total length so the block context is initialised
		 * correctly from the first block (the library only reads req->len on
		 * the first call, when send_blk_ctx.total_size == 0).
		 */
		coap_req.payload_cb = coap_payload_cb;
		coap_req.len = req->payload_len;
	}

	ret = coap_client_req(&sm_coap_client, req->fd, NULL, &coap_req, NULL);
	if (ret) {
		LOG_ERR("coap_client_req failed: %d", ret);
		coap_fail_request(req);
	}

	return ret;
}

/*
 * parse_option_value() - decode an option value string into raw bytes.
 *
 * If 'str' starts with "0x" or "0X", the remaining characters are decoded
 * as a hex byte string (each pair of hex digits becomes one byte).  The hex
 * part must be non-empty and even-length, otherwise -EINVAL is returned.
 * Any other string is copied verbatim (e.g. Proxy-Uri, Uri-Query).
 *
 * Examples:
 *   "0x3c"         → 1 byte: 0x3c  (e.g. Accept: application/cbor)
 *   "0xdeadbeef"   → 4 bytes: 0xde 0xad 0xbe 0xef
 *   "abcd"         → 4 bytes: 0x61 0x62 0x63 0x64 (verbatim ASCII)
 *   "https://..."  → copied as-is
 *   ""             → empty option (zero bytes)
 *
 * Returns 0 on success, -EINVAL on malformed hex or value exceeding out_max.
 */
static int parse_option_value(const char *str, size_t str_len,
			      uint8_t *out, size_t out_max, size_t *out_len)
{
	if (str_len >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
		const char *hex = str + 2;
		size_t hex_len = str_len - 2;
		size_t n;

		if (hex_len == 0 || (hex_len & 1U)) {
			return -EINVAL;
		}
		n = hex2bin(hex, hex_len, out, out_max);
		if (n == 0) {
			return -EINVAL;
		}
		*out_len = n;
	} else {
		if (str_len > out_max) {
			return -EINVAL;
		}
		memcpy(out, str, str_len);
		*out_len = str_len;
	}
	return 0;
}

static int coap_datamode_send_block(struct coap_request *req, const uint8_t *data, size_t len)
{
	size_t remaining = len;
	const uint8_t *src = data;

	while (remaining > 0) {
		size_t space = CONFIG_COAP_CLIENT_BLOCK_SIZE - req->staging_filled;
		size_t copy = MIN(remaining, space);
		size_t total_received = req->payload_sent + req->staging_filled + copy;
		bool is_last_block;

		memcpy(req->staging + req->staging_filled, src, copy);
		req->staging_filled += copy;
		src += copy;
		remaining -= copy;

		is_last_block = (total_received >= req->payload_len);

		/* Signal payload_cb when staging is full or final block. */
		if (req->staging_filled == CONFIG_COAP_CLIENT_BLOCK_SIZE || is_last_block) {
			int ret;

			k_sem_give(&req->staging_ready);

			if (!req->coap_started) {
				/* First block ready: start the request now.
				 * coap_client_req() calls payload_cb synchronously
				 * for block 0, taking staging_ready immediately.
				 */
				ret = coap_start_request(req);
				if (ret) {
					LOG_ERR("CoAP start failed: %d", ret);
					return ret;
				}
			}

			if (!is_last_block) {
				if (k_sem_take(&req->staging_consumed, COAP_BLOCK_SEND_TIMEOUT) !=
				    0) {
					LOG_ERR("Timed out waiting for coap_client to consume "
						"block");
					req->payload_aborted = true;
					return -ETIMEDOUT;
				}
				req->staging_filled = 0;
			}
		}
	}

	return 0;
}

static int coap_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	LOG_DBG("CoAP data mode callback: op=%d, len=%d, flags=0x%02x", op, len, flags);

	if (op == DATAMODE_SEND) {
		struct coap_request *req = coap_pending_req;

		if (!req) {
			LOG_ERR("No request for data mode");
			exit_datamode_handler(sm_at_host_get_current(), -EINVAL);
			return -EINVAL;
		}

		if (req->payload_len <= CONFIG_COAP_CLIENT_BLOCK_SIZE) {
			/* Small payload: accumulate in staging[]; coap_start_request()
			 * is called from DATAMODE_EXIT once all bytes are received.
			 */
			size_t space = CONFIG_COAP_CLIENT_BLOCK_SIZE - req->staging_filled;
			size_t copy = MIN((size_t)len, space);

			memcpy(req->staging + req->staging_filled, data, copy);
			req->staging_filled += copy;
		} else {
			/* Large payload: stream to coap_client one block at a time.
			 * A single DATAMODE_SEND call may carry more than one block.
			 */
			int ret = coap_datamode_send_block(req, data, (size_t)len);

			if (ret) {
				return ret;
			}
		}

		LOG_DBG("CoAP payload: %zu / %zu bytes buffered",
			req->payload_sent + req->staging_filled, req->payload_len);
		return len;

	} else if (op == DATAMODE_EXIT) {
		struct coap_request *req = coap_pending_req;

		if (req && req->payload_len <= CONFIG_COAP_CLIENT_BLOCK_SIZE) {
			/* Small payload: staging[] holds all received bytes; start the
			 * request now using coap_req.payload (no payload_cb involved).
			 */
			if (req->staging_filled > 0) {
				int ret;

				req->payload_len = req->staging_filled;
				ret = coap_start_request(req);
				if (ret) {
					LOG_ERR("CoAP start failed on exit: %d", ret);
				}
			} else {
				LOG_ERR("CoAP data mode exited with zero bytes");
				coap_fail_request(req);
			}
		} else if (req && !req->coap_started) {
			/* Large payload, early truncation before first full block:
			 * start with partial data, or abort if nothing was received.
			 */
			int ret;

			if (req->staging_filled > 0) {
				req->payload_len = req->staging_filled;
			} else {
				req->payload_aborted = true;
			}
			k_sem_give(&req->staging_ready);
			ret = coap_start_request(req);
			if (ret) {
				LOG_ERR("CoAP start failed on exit: %d", ret);
			}
		} else if (req && req->payload_sent + req->staging_filled < req->payload_len) {
			/* Large payload, truncation after first block was sent: unblock
			 * payload_cb with whatever partial data remains.
			 */
			LOG_WRN("CoAP payload truncated: %zu / %zu bytes received",
				req->payload_sent + req->staging_filled, req->payload_len);
			if (req->staging_filled > 0) {
				req->payload_len = req->payload_sent + req->staging_filled;
			} else {
				req->payload_aborted = true;
			}
			k_sem_give(&req->staging_ready);
		}

		if (flags & SM_DATAMODE_FLAGS_EXIT_HANDLER) {
			rsp_send(CONFIG_SM_DATAMODE_TERMINATOR);
		}
	}

	return 0;
}

/**
 * Send a CoAP request over the AT socket identified by <handle>.
 * The socket must already be created with AT#XSOCKET and connected with AT#XCONNECT.
 * Returns OK immediately; the response is delivered asynchronously.
 */
SM_AT_CMD_CUSTOM(xcoapreq, "AT#XCOAPCREQ", handle_at_coap_req);
STATIC int handle_at_coap_req(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	int ret = -EINVAL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		int handle;
		int method;
		const char *path;
		size_t path_len;
		int auto_reception = 1;
		int format = 0;
		int confirmable = 0;
		int content_format = COAP_CONTENT_FORMAT_TEXT_PLAIN;
		int payload_len = 0;
		struct coap_request *req;

		if (param_count < 4) {
			return -EINVAL;
		}

		ret = at_parser_num_get(parser, 1, &handle);
		if (ret) {
			return ret;
		}

		if (!find_socket(handle)) {
			LOG_ERR("Invalid socket handle: %d", handle);
			return -EINVAL;
		}

		ret = at_parser_string_ptr_get(parser, 2, &path, &path_len);
		if (ret) {
			return ret;
		}
		if (path_len >= COAP_PATH_MAX_LEN) {
			return -EINVAL;
		}

		ret = at_parser_num_get(parser, 3, &method);
		if (ret) {
			return ret;
		}

		if (method < COAP_METHOD_GET || method > COAP_METHOD_IPATCH) {
			LOG_ERR("Invalid CoAP method: %d", method);
			return -EINVAL;
		}

		if (param_count > 4) {
			ret = at_parser_num_get(parser, 4, &auto_reception);
			if (ret) {
				return ret;
			}
			if (auto_reception != 0 && auto_reception != 1) {
				LOG_ERR("Invalid auto_reception value: %d", auto_reception);
				return -EINVAL;
			}
		}

		if (param_count > 5) {
			ret = at_parser_num_get(parser, 5, &format);
			if (ret) {
				return ret;
			}
			if (format != 0 && format != 1) {
				LOG_ERR("Invalid format value: %d", format);
				return -EINVAL;
			}
		}

		if (param_count > 6) {
			ret = at_parser_num_get(parser, 6, &confirmable);
			if (ret) {
				return ret;
			}
			if (confirmable != 0 && confirmable != 1) {
				LOG_ERR("Invalid confirmable value: %d", confirmable);
				return -EINVAL;
			}
		}

		if (param_count > 7) {
			ret = at_parser_num_get(parser, 7, &content_format);
			if (ret) {
				return ret;
			}
		}

		if (param_count > 8) {
			ret = at_parser_num_get(parser, 8, &payload_len);
			if (ret) {
				return ret;
			}
			if (payload_len < 0) {
				return -EINVAL;
			}
		}

		k_mutex_lock(&coap_mutex, K_FOREVER);
		req = alloc_request();
		k_mutex_unlock(&coap_mutex);
		if (!req) {
			LOG_ERR("Request already active or out of memory");
			return -EBUSY;
		}

		req->fd = handle;
		req->method = (enum coap_method)method;
		req->confirmable = (bool)confirmable;
		req->content_format = (enum coap_content_format)content_format;
		memcpy(req->path, path, path_len);
		req->path[path_len] = '\0';
		req->manual_rx = !auto_reception;
		req->hex_rx = (bool)format;

		if (req->manual_rx) {
			req->rx_buf = malloc(CONFIG_COAP_CLIENT_BLOCK_SIZE);
			if (!req->rx_buf) {
				ret = -ENOMEM;
				goto cleanup_req;
			}
		}

		/* Parse option pairs (<opt_num>, <opt_val>) starting at param 9.
		 * Param 8 is <payload_len>; it must be present (explicitly 0) when options
		 * follow.  Omitting <payload_len> shifts every option one position left, which
		 * always produces an odd option-param count and is caught below.
		 *
		 * <opt_val> is decoded as raw bytes when prefixed with "0x"/"0X"
		 * (e.g. "0x3c" → 1 byte 0x3c); otherwise used verbatim as a UTF-8 string.
		 * Examples:
		 *   ...,0,0,35,"coap://host/path"   payload_len=0, Proxy-Uri (text)
		 *   ...,0,0,17,"0x3c"               payload_len=0, Accept: application/cbor
		 *   ...,0,0,6,"0x00",17,"0x3c"      payload_len=0, Observe + Accept
		 */
		if (param_count > 9 && ((param_count - 9) & 1U)) {
			LOG_ERR("Odd number of option params after payload_len "
				"(missing payload_len=0 before options, or unpaired opt_num/opt_val)");
			ret = -EINVAL;
			goto cleanup_req;
		}
		for (uint32_t i = 9; (i + 1) < param_count; i += 2) {
			const char *val_str;
			size_t val_len;
			size_t decoded_len;
			uint16_t opt_num;
			struct coap_client_option *opt;

			if (req->num_extra_options >= CONFIG_COAP_CLIENT_MAX_EXTRA_OPTIONS) {
				LOG_ERR("Too many CoAP options (max %d)",
					CONFIG_COAP_CLIENT_MAX_EXTRA_OPTIONS);
				ret = -EINVAL;
				goto cleanup_req;
			}

			ret = at_parser_num_get(parser, i, &opt_num);
			if (ret) {
				LOG_ERR("CoAP option param %u: invalid number", i);
				goto cleanup_req;
			}

			ret = at_parser_string_ptr_get(parser, i + 1, &val_str, &val_len);
			if (ret) {
				LOG_ERR("CoAP option %d: missing or invalid value", opt_num);
				goto cleanup_req;
			}

			opt = &req->extra_options[req->num_extra_options];
			opt->code = opt_num;
			ret = parse_option_value(val_str, val_len, opt->value,
						 sizeof(opt->value), &decoded_len);
			if (ret) {
				LOG_ERR("CoAP option %d value too long (%zu bytes, max %zu)",
					opt_num, val_len, sizeof(opt->value));
				goto cleanup_req;
			}
			opt->len = (uint16_t)decoded_len;
			req->num_extra_options++;
		}

		if (payload_len > 0) {
			req->staging = malloc(CONFIG_COAP_CLIENT_BLOCK_SIZE);
			if (!req->staging) {
				ret = -ENOMEM;
				goto cleanup_req;
			}
			req->payload_len = (size_t)payload_len;

			/* Enter data mode first. coap_start_request() is deferred until
			 * coap_datamode_callback has data ready in staging[]:
			 * - Small payloads: called from DATAMODE_EXIT once all bytes arrived.
			 * - Large payloads: called from coap_datamode_send_block on the first
			 *   full block, because coap_client_req() invokes payload_cb
			 *   synchronously to build the first CoAP packet.
			 */
			ret = enter_datamode(coap_datamode_callback, payload_len);
			if (ret) {
				goto cleanup_req;
			}
		} else {
			ret = coap_start_request(req);
			/* coap_start_request handles its own cleanup on failure */
		}

		break;
cleanup_req:
		k_mutex_lock(&coap_mutex, K_FOREVER);
		coap_close_request(req);
		k_mutex_unlock(&coap_mutex);
		break;
	}

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XCOAPCREQ: <handle>,<path>,<method>"
			 "[,<auto_reception>[,<format>[,<confirmable>[,<content_format>"
			 "[,<payload_len>[,<opt_num>,<opt_val>...]]]]]]]\r\n");
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

/**
 * Cancel the active CoAP request.  <handle> must match the socket handle used
 * when starting the request.  This acts as an ownership assertion: it prevents
 * accidentally cancelling a request that belongs to a different socket.
 */
SM_AT_CMD_CUSTOM(xcoapcancel, "AT#XCOAPCCANCEL", handle_at_coap_cancel);
STATIC int handle_at_coap_cancel(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				 uint32_t param_count)
{
	ARG_UNUSED(param_count);

	int ret = -EINVAL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		int handle;
		struct coap_request *req;

		ret = at_parser_num_get(parser, 1, &handle);
		if (ret) {
			return ret;
		}

		k_mutex_lock(&coap_mutex, K_FOREVER);
		/* Ownership check: reject cancel if the active request belongs to a
		 * different handle, or if no request is active at all.
		 */
		req = (coap_pending_req && coap_pending_req->fd == handle)
			? coap_pending_req : NULL;
		k_mutex_unlock(&coap_mutex);

		if (!req) {
			LOG_ERR("No active CoAP request for handle %d", handle);
			return -EINVAL;
		}

		LOG_INF("Cancelling CoAP request (handle=%d)", handle);
		coap_client_cancel_requests(&sm_coap_client);
		ret = 0;
		break;
	}

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XCOAPCCANCEL: <handle>\r\n");
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

/**
 * Pull one block of response body in manual receive mode (auto_reception=0).
 * Must be called after receiving a #XCOAPCHEAD URC.
 *
 * After the last block is drained, the coap_callback unblocks, sends #XCOAPCSTAT,
 * and closes the request.
 */
SM_AT_CMD_CUSTOM(xcoapdata, "AT#XCOAPCDATA", handle_at_coap_data);
STATIC int handle_at_coap_data(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	int ret = -EINVAL;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		int handle;
		int pull_len = CONFIG_COAP_CLIENT_BLOCK_SIZE;
		struct coap_request *req;
		size_t send_len;

		ret = at_parser_num_get(parser, 1, &handle);
		if (ret) {
			return ret;
		}

		if (param_count > 2) {
			int tmp;

			if (at_parser_num_get(parser, 2, &tmp) == 0 && tmp > 0) {
				pull_len = MIN(tmp, CONFIG_COAP_CLIENT_BLOCK_SIZE);
			}
		}

		k_mutex_lock(&coap_mutex, K_FOREVER);
		req = (coap_pending_req && coap_pending_req->fd == handle)
			? coap_pending_req : NULL;
		if (!req) {
			k_mutex_unlock(&coap_mutex);
			LOG_ERR("No active CoAP request for handle %d", handle);
			return -EINVAL;
		}

		if (!req->manual_rx) {
			k_mutex_unlock(&coap_mutex);
			LOG_ERR("Request on handle %d not in manual receive mode", handle);
			return -EINVAL;
		}

		if (req->rx_buf_filled == 0) {
			int bytes_sent = (int)req->bytes_sent;

			/* No block ready yet: return zero-length response */
			k_mutex_unlock(&coap_mutex);
			rsp_send("\r\n#XCOAPCDATA: %d,%d,0\r\n", handle, bytes_sent);
			return 0;
		}

		send_len = MIN((size_t)pull_len, req->rx_buf_filled);
		rsp_send("\r\n#XCOAPCDATA: %d,%d,%d\r\n", handle, (int)req->bytes_sent,
			 (int)send_len);
		req->bytes_sent += send_len;
		if (req->hex_rx) {
			coap_data_send_hex(req->pipe, req->rx_buf, send_len);
		} else {
			data_send(req->pipe, req->rx_buf, send_len);
		}
		req->rx_buf_filled -= send_len;

		if (req->rx_buf_filled > 0) {
			/* Partial drain: shift remaining bytes to the front of the buffer
			 * so the next call reads the correct data, then return without
			 * unblocking the callback.
			 */
			memmove(req->rx_buf, req->rx_buf + send_len, req->rx_buf_filled);
			k_mutex_unlock(&coap_mutex);
			return 0;
		}

		/* Buffer fully drained: unblock coap_callback. Unlock before giving
		 * rx_consumed so coap_close_request() in the callback cannot deadlock.
		 * req must not be accessed after k_sem_give().
		 */
		k_mutex_unlock(&coap_mutex);
		k_sem_give(&req->rx_consumed);
		ret = 0;
		break;
	}

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XCOAPCDATA: <handle>[,<length>]\r\n");
		ret = 0;
		break;

	default:
		break;
	}

	return ret;
}

static int sm_at_coap_init(void)
{
	sm_coap_client.fd = -1;
	return coap_client_init(&sm_coap_client, "sm_coap");
}

SYS_INIT(sm_at_coap_init, APPLICATION, 10);
