/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file uart_stubs.c
 * Stub implementations for UART handler functions
 */

#include <zephyr/kernel.h>
#include "sm_uart_handler.h"
#include <zephyr/modem/pipe.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_stubs, LOG_LEVEL_DBG);

const struct device *const sm_uart_dev;
uint32_t sm_uart_baudrate = 115200;
RING_BUF_DECLARE(uart_rx_ring_buf, 1024);
static K_SEM_DEFINE(tx_done, 0, 1);

static void tx_done_fn(struct k_work *work)
{
	k_sem_give(&tx_done);
}
static K_WORK_DELAYABLE_DEFINE(tx_done_work, tx_done_fn);

/* Forward declaration to capture response data */
extern void capture_response_data(const uint8_t *data, size_t len);

void uart_stub_rx(const uint8_t *data, size_t len)
{
	ring_buf_put(&uart_rx_ring_buf, data, len);
	modem_pipe_notify_receive_ready(sm_uart_pipe_get());
	k_sem_take(&tx_done, K_SECONDS(1));
}

static int pipe_open(void *data)
{
	struct modem_pipe *pipe = (struct modem_pipe *)data;

	modem_pipe_notify_opened(pipe);
	return 0;
}

static int pipe_transmit(void *data, const uint8_t *buf, size_t size)
{
	capture_response_data(buf, size);
	k_work_reschedule(&tx_done_work, K_MSEC(10));
	return size;
}

static int pipe_receive(void *data, uint8_t *buf, size_t size)
{
	struct modem_pipe *pipe = (struct modem_pipe *)data;

	if (ring_buf_size_get(&uart_rx_ring_buf) > size) {
		modem_pipe_notify_receive_ready(pipe);
	}
	return ring_buf_get(&uart_rx_ring_buf, buf, size);
}

static int pipe_close(void *data)
{
	struct modem_pipe *pipe = (struct modem_pipe *)data;

	modem_pipe_notify_closed(pipe);
	return 0;
}

static const struct modem_pipe_api modem_pipe_api = {
	.open = pipe_open,
	.transmit = pipe_transmit,
	.receive = pipe_receive,
	.close = pipe_close,
};

struct modem_pipe *sm_uart_pipe_get(void)
{
	static bool initialized;
	static struct modem_pipe pipe;

	if (!initialized) {
		initialized = true;
		modem_pipe_init(&pipe, &pipe, &modem_pipe_api);
	}
	return &pipe;
}
