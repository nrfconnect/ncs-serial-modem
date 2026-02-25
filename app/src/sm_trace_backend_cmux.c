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
#include "sm_util.h"

LOG_MODULE_REGISTER(modem_trace_backend, CONFIG_MODEM_TRACE_BACKEND_LOG_LEVEL);

static K_SEM_DEFINE(tx_idle_sem, 0, 1);

static trace_backend_processed_cb trace_processed_callback;
static struct modem_pipe *trace_pipe;

static void modem_pipe_event_handler(struct modem_pipe *pipe,
				     enum modem_pipe_event event, void *user_data)
{
	switch (event) {

	case MODEM_PIPE_EVENT_TRANSMIT_IDLE:
		k_sem_give(&tx_idle_sem);
		break;
	case MODEM_PIPE_EVENT_CLOSED:
		LOG_INF("Trace pipe closed");
		trace_pipe = NULL;
		break;
	case MODEM_PIPE_EVENT_OPENED:
		LOG_INF("Trace pipe opened");
		trace_pipe = pipe;
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
	trace_pipe = NULL;

	return 0;
}

int trace_backend_deinit(void)
{
	if (trace_pipe) {
		modem_pipe_release(trace_pipe);
		trace_pipe = NULL;
	}
	return 0;
}

void sm_trace_backend_attach(struct modem_pipe *pipe)
{
	modem_pipe_attach(pipe, modem_pipe_event_handler, NULL);
}

void sm_trace_backend_detach(void)
{
	if (trace_pipe) {
		modem_pipe_release(trace_pipe);
		trace_pipe = NULL;
	}
}

int trace_backend_write(const void *data, size_t len)
{
	int ret = 0;

	if (!trace_pipe || !sm_pipe_is_open(trace_pipe)) {
		LOG_DBG_RATELIMIT("Pipe closed, dropped %u bytes.", len);
		trace_processed_callback(len);
		return len;
	}

	/* No need to retry here.
	 * The nrf_modem_lib_trace.c:trace_fragment_write() handles
	 * retrying if the backend returns -EAGAIN.
	 *
	 * In congested situations, the modem trace fifos will overflow and drop data.
	 */

	if (k_sem_take(&tx_idle_sem, K_MSEC(100)) != 0) {
		LOG_WRN_RATELIMIT("TX timeout.");
		return -EAGAIN;
	}

	ret = modem_pipe_transmit(trace_pipe, data, len);
	if (ret < 0) {
		LOG_WRN("TX error (%d). Dropped %u bytes.", ret, len);
		trace_processed_callback(len);
		return ret;
	} else if (ret == 0) {
		return -EAGAIN;
	}
	trace_processed_callback(ret);

	return ret;
}

struct nrf_modem_lib_trace_backend trace_backend = {
	.init = trace_backend_init,
	.deinit = trace_backend_deinit,
	.write = trace_backend_write,
};
