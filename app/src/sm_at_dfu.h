/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_DFU_H
#define SM_AT_DFU_H

/** @file sm_at_dfu.h
 *
 * @brief Vendor-specific AT command for DFU service.
 * @{
 */

#include <stdbool.h>
#include <stddef.h>

/* Whether bootloader mode should be enabled. */
extern bool sm_bootloader_mode_requested;

/* Whether bootloader mode is enabled. */
extern bool sm_bootloader_mode_enabled;

/**
 * @brief Set bootloader mode to enabled or disabled.
 *
 * @param enable True to enable bootloader mode, false to disable it.
 *
 * @return 0 on success, negative error code on failure.
 */
int request_bootloader_mode(bool enable);

/**
 * @brief Handle bootloader AT command.
 *
 * @param buf Response buffer.
 * @param len Response buffer size.
 * @param at_cmd AT command.
 *
 * @return 0 on success, negative error code on failure.
 */
int sm_at_dfu_handle_xdfu(char *buf, size_t len, char *at_cmd);
/** @} */

#endif /* SM_AT_DFU_H */
