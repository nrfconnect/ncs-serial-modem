/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
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

static void modem_pipe_event_handler(struct modem_pipe *pipe,
				     enum modem_pipe_event event, void *user_data)
{
	switch (event) {

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
	sm_cmux_release(CMUX_MODEM_TRACE_CHANNEL);

	return 0;
}

int trace_backend_write(const void *data, size_t len)
{
	struct modem_pipe *pipe = sm_cmux_reserve(CMUX_MODEM_TRACE_CHANNEL);
	const uint8_t *buf = (const uint8_t *)data;
	size_t sent_len = 0;
	int ret = 0;

	if (!sm_cmux_dlci_is_open(CMUX_MODEM_TRACE_CHANNEL)) {
		LOG_DBG("Dropped %u bytes.", len);
		trace_processed_callback(len);
		return len;
	}

	modem_pipe_attach(pipe, modem_pipe_event_handler, NULL);

	while (sent_len < len) {
		ret = modem_pipe_transmit(pipe, buf, len - sent_len);
		if (ret < 0) {
			LOG_WRN("TX error (%d). Dropped %u bytes.", ret, len - sent_len);
			trace_processed_callback(len);
			return ret;
		} else if (ret == 0) {
			if (k_sem_take(&tx_idle_sem, K_SECONDS(1)) != 0) {
				LOG_WRN("TX timeout.");
				break;
			}
		} else {
			sent_len += ret;
			buf += ret;
		}
	}

	if (sent_len) {
		trace_processed_callback(sent_len);
	}

	if (sent_len < len) {
		LOG_DBG("Sent %u out of %u bytes.", sent_len, len);
	}

	return sent_len;
}

struct nrf_modem_lib_trace_backend trace_backend = {
	.init = trace_backend_init,
	.deinit = trace_backend_deinit,
	.write = trace_backend_write,
};
