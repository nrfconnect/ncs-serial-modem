/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_UART_HANDLER_
#define SM_UART_HANDLER_

/** @file sm_uart_handler.h
 *
 * @brief pure UART handler for Serial Modem
 * @{
 */
#include "sm_trap_macros.h"
#include <zephyr/device.h>

#define UART_RX_MARGIN_MS	10

extern const struct device *const sm_uart_dev;
extern uint32_t sm_uart_baudrate;

/** @retval 0 on success. Otherwise, the error code is returned. */
int sm_uart_handler_enable(void);

/** @} */

#endif /* SM_UART_HANDLER_ */
