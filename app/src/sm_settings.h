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

/**
 * @brief Loads the Serial Modem settings from NVM.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_settings_init(void);

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
 * @brief Saves the auto-connect settings to NVM.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_settings_auto_connect_save(void);

/** @} */
#endif
