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
#include <stdbool.h>
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

/** Persisted modem fault details. Access only from sm_work_q after settings load. */
struct sm_modem_fault {
	uint32_t reason;
	uint32_t program_counter;
	/** UTC observation time in Unix seconds, or zero if unavailable. */
	uint32_t occurred_at;
};

/**
 * @brief Saves the first pending modem fault without overwriting an existing record.
 *
 * @return 0 if saved, -EALREADY if a fault is pending, or another negative error on failure.
 */
int sm_settings_modem_fault_save(const struct sm_modem_fault *fault);

/**
 * @brief Copies the pending modem fault into @p fault.
 *
 * @return true if a record is pending, false otherwise.
 */
bool sm_settings_modem_fault_get(struct sm_modem_fault *fault);

/**
 * @brief Deletes the pending fault after acknowledged delivery.
 *
 * @return 0 on success, or a negative error. A failed deletion leaves the record pending.
 */
int sm_settings_modem_fault_clear(void);

/** @} */
#endif
