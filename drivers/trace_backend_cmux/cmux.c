/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/modem/pipe.h>
#include <modem/nrf_modem_lib_trace.h>
#include <modem/trace_backend.h>

#include "sm_cmux.h"

LOG_MODULE_REGISTER(modem_trace_backend, CONFIG_MODEM_TRACE_BACKEND_LOG_LEVEL);

static K_SEM_DEFINE(tx_idle_sem, 0, 1);

static trace_backend_processed_cb trace_processed_callback;

static struct modem_pipe *pipe;

static void modem_pipe_event_handler(struct modem_pipe *pipe,
				     enum modem_pipe_event event, void *user_data)
{
	switch (event) {
	case MODEM_PIPE_EVENT_OPENED:
		LOG_DBG("Coredump DLCI opened.");
		break;

	case MODEM_PIPE_EVENT_CLOSED:
		LOG_DBG("Coredump DLCI closed.");
		break;

	case MODEM_PIPE_EVENT_TRANSMIT_IDLE:
		k_sem_give(&tx_idle_sem);
		break;

	default:
		break;
	}
}

int trace_backend_init(trace_backend_processed_cb trace_processed_cb)
{
	if (trace_processed_cb == NULL) {
		return -EFAULT;
	}

	trace_processed_callback = trace_processed_cb;

	return 0;
}

int trace_backend_deinit(void)
{
	if (pipe != NULL) {
		modem_pipe_release(pipe);
		sm_cmux_release(CMUX_COREDUMP_CHANNEL, false);
		pipe = NULL;
	}
	return 0;
}

int trace_backend_write(const void *data, size_t len)
{
	size_t sent_len = 0;
	int ret = 0;

	if (!sm_cmux_is_started()) {
		/* Drop the trace data */
		trace_processed_callback(len);
		return len;
	}

	if (pipe == NULL) {
		pipe = sm_cmux_reserve(CMUX_COREDUMP_CHANNEL);
		modem_pipe_attach(pipe, modem_pipe_event_handler, NULL);
	}

	const uint8_t *buf = (const uint8_t *)data;

	while (sent_len < len) {
		k_sem_take(&tx_idle_sem, K_FOREVER);
		ret = modem_pipe_transmit(pipe, buf, len - sent_len);
		if (ret <= 0) {
			break;
		}

		trace_processed_callback(ret);

		sent_len += ret;
		buf += ret;
	}

	if (ret == 0) {
		/* Retry by trace thread in modem lib */
		return -EAGAIN;
	} else if (ret < 0) {
		LOG_INF("Coredump: Sent %u out of %u bytes. (%d)", sent_len, len, ret);
		return ret;
	}

	return sent_len;
}

struct nrf_modem_lib_trace_backend trace_backend = {
	.init = trace_backend_init,
	.deinit = trace_backend_deinit,
	.write = trace_backend_write,
};
