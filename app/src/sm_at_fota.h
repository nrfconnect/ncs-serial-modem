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

enum fota_stage {
	FOTA_STAGE_INIT,
	FOTA_STAGE_DOWNLOAD,
	FOTA_STAGE_DOWNLOAD_ERASE_PENDING,
	FOTA_STAGE_DOWNLOAD_ERASED,
	FOTA_STAGE_ACTIVATE,
	FOTA_STAGE_COMPLETE
};

enum fota_status {
	FOTA_STATUS_OK,
	FOTA_STATUS_ERROR,
	FOTA_STATUS_CANCELLED
};

enum sm_fota_image_type {
	SM_FOTA_TYPE_NONE = 0,
	SM_FOTA_TYPE_APP,        /* MCUBoot application FOTA */
	SM_FOTA_TYPE_MCUBOOT_BL, /* MCUboot bootloader FOTA */
	SM_FOTA_TYPE_MFW,        /* Modem delta FOTA */
	SM_FOTA_TYPE_FULL_MFW,   /* Full modem FOTA */
};

/* Whether a modem full firmware update is to be activated. */
extern bool sm_modem_full_fota;
/** MCUboot bootloader FOTA: validate version increase after reboot. */
extern bool sm_fota_bl_pending_validate;
/** MCUboot bootloader FOTA: Active slot fw_info version before a bootloader update was started. */
extern uint32_t sm_fota_bl_version_before;

extern enum sm_fota_image_type sm_fota_type; /* FOTA image type. */
extern enum fota_stage sm_fota_stage; /* Current stage of FOTA process. */
extern enum fota_status sm_fota_status; /* FOTA process status. */
extern int32_t sm_fota_info; /* FOTA download percentage or failure cause in case of error. */

/** @brief Sets the FOTA state variables to their default values. */
void sm_fota_init_state(void);

/**
 * @brief After reboot, validate an MCUboot bootloader update by checking that the
 * active image fw_info version (monotonic counter) increased from the value recorded
 * before the update started.
 */
void sm_fota_mcuboot_bl_boot_check(void);

/**
 * @brief FOTA post-process after reboot.
 */
void sm_fota_post_process(void);

/**
 * @brief Finishes the full modem firmware update.
 *
 * This is to be called after the application or modem
 * has been rebooted and a full modem firmware update is ongoing.
 */
#if defined(CONFIG_SM_FULL_FOTA)
void sm_finish_modem_full_fota(void);
#endif

/** @} */
#endif /* SM_AT_FOTA_ */
