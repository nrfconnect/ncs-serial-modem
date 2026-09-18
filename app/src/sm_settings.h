/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_SETTINGS_
#define SM_SETTINGS_

/** @file sm_settings.h
 *
 * @brief Utility functions for Serial Modem settings.
 * @{
 */
#include "sm_trap_macros.h"
#include <stdint.h>

/* Number of consecutive reboots due to nrf_modem_lib_init() failing with -EIO. */
extern uint8_t sm_modem_init_eio_retry_count;

/**
 * @brief Saves the FOTA settings to NVM.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_settings_fota_save(void);

/**
 * @brief Saves the bootloader mode settings to NVM.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_settings_bootloader_mode_save(void);

/**
 * @brief Saves the full MFW DFU segment type settings to NVM.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_settings_full_mfw_dfu_segment_type_save(void);

/**
 * @brief Saves the modem library -EIO retry count to NVM.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_settings_modem_eio_retried_save(void);

/** @} */
#endif
