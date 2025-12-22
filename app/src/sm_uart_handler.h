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
#include <zephyr/modem/pipe.h>

#define UART_RX_MARGIN_MS	10

extern const struct device *const sm_uart_dev;
extern uint32_t sm_uart_baudrate;

/**
 * @brief UART pipe transmit callback type.
 *
 * @retval Amount of bytes written on success, otherwise a negative error code.
 */
typedef int (*sm_pipe_tx_t)(const uint8_t *data, size_t len, bool urc);

/**
 * @brief Get the UART pipe instance.
 *
 * @retval Pointer to the UART modem_pipe, or NULL if not initialized.
 */
struct modem_pipe *sm_uart_pipe_get(void);

/** @} */

#endif /* SM_UART_HANDLER_ */
