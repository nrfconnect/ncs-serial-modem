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

const struct device *const sm_uart_dev;
uint32_t sm_uart_baudrate = 115200;

/* Stub for sm_uart_handler_enable */
int sm_uart_handler_enable(void)
{
	/* Stub - return success */
	return 0;
}

/* Stub for sm_uart_handler_disable */
int sm_uart_handler_disable(void)
{
	/* Stub - return success */
	return 0;
}

/* Forward declaration to capture response data */
extern void capture_response_data(const uint8_t *data, size_t len);

/* Stub for sm_tx_write - capture output for testing */
int sm_tx_write(const uint8_t *data, size_t len, bool flush, bool urc)
{
	/* Capture responses for testing - forward to sm_at_host_stubs.c */
	capture_response_data(data, len);
	(void)flush;
	(void)urc;
	return 0;
}
