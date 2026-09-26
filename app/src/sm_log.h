/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_LOG_
#define SM_LOG_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**@file sm_log.h
 *
 * @brief Log functions for Serial Modem
 * @{
 */

void sm_log_flush(void);

/**
 * @defgroup xlog_verbosity_group AT#XLOG verbosity levels
 * @brief AT#XLOG verbosity levels
 * @{
 */
#define SM_LOG_MODE_OFF      0 /**< UART disabled; AT traffic at INF for nRF Cloud Obs. */
#define SM_LOG_MODE_REDACTED 1 /**< UART enabled; AT traffic at INF; sensitive payloads redacted. */
#define SM_LOG_MODE_FULL     2 /**< UART enabled; AT traffic at DBG; not for nRF Cloud Obs. */

/** @} */

/** @brief Current AT#XLOG payload verbosity. */
int sm_log_mode(void);

/** @brief Sensitive-command prefix matching an AT command's argument/response, or NULL. */
const char *sm_log_cmd_sensitive_prefix(const char *cmd, size_t len);

/** @brief Log an inbound AT command line, redacting sensitive payloads below AT#XLOG=2. */
void sm_log_rx_command(const uint8_t *buf, size_t len);

/** @brief Log an outbound buffer line by line at INF (modes 0/1), redacting sensitive lines.
 *         At AT#XLOG=2 emits a raw DBG hex dump instead (no INF output).
 */
void sm_log_tx(const uint8_t *data, size_t len);

/** @} */

#endif /* SM_LOG_ */
