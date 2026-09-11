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

/** AT#XLOG payload verbosity, also the AT#XLOG=<n> argument. */
#define SM_LOG_OFF         0 /* Logging disabled. */
#define SM_LOG_ON_REDACTED 1 /* Logging on; sensitive command payloads redacted. */
#define SM_LOG_ON_FULL     2 /* Logging on; all payloads shown. */

/** @brief Current AT#XLOG payload verbosity (SM_LOG_OFF / ON_REDACTED / ON_FULL). */
int sm_log_level(void);

/** @brief Sensitive-command prefix matching an AT command's argument/response, or NULL. */
const char *sm_log_cmd_sensitive_prefix(const char *cmd, size_t len);

/** @brief Hexdump an inbound AT command line, redacting sensitive payloads below AT#XLOG=2. */
void sm_log_rx_command(const uint8_t *buf, size_t len);

/** @brief Log a URC, redacting sensitive content (e.g. raw SMS PDUs) below AT#XLOG=2. */
void sm_log_urc(const char *dest, const void *pipe, const uint8_t *data, size_t len);

/** @} */

#endif /* SM_LOG_ */
