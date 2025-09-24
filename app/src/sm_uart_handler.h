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
typedef int (*sm_pipe_tx_t)(const uint8_t *data, size_t len);

/**
 * @brief Enable the UART handler.
 *
 * @retval 0 on success. Otherwise, a negative error code.
 */
int sm_uart_handler_enable(void);

/** @brief Disable the UART handler.
 *
 * @retval 0 on success. Otherwise, a negative error code.
 */
int sm_uart_handler_disable(void);

/**
 * @brief Write data to UART or to a modem pipe.
 *
 * @param data Data to write.
 * @param len Length of data to write.
 *
 * @retval 0 on success. Otherwise, a negative error code.
 */
int sm_tx_write(const uint8_t *data, size_t len);

/**
 * @brief Initialize UART pipe for Serial Modem.
 *
 * @param pipe_tx Transmit callback for the pipe.
 *
 * @retval Pointer to the initialized pipe on success, NULL otherwise.
 */
struct modem_pipe *sm_uart_pipe_init(sm_pipe_tx_t pipe_tx);

/** @} */

#endif /* SM_UART_HANDLER_ */
