/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_FOTA_
#define SM_AT_FOTA_

/** @file sm_at_fota.h
 *
 * @brief Vendor-specific AT command for FOTA service.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief (Re-)registers this module's fota_download callback.
 *
 * AT#XNRFCLOUDFOTA temporarily takes over the shared fota_download singleton callback for its
 * own session; call this after that session ends to restore AT#XFOTA's handler.
 */
int sm_at_fota_register_callback(void);

/**
 * @brief Finishes the full modem firmware update.
 *
 * This is to be called after the application or modem
 * has been rebooted and a full modem firmware update is ongoing.
 */
void sm_finish_modem_full_fota(void);

/** @} */
#endif /* SM_AT_FOTA_ */
