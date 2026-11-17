/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_FOTA_SESSION_H
#define SM_FOTA_SESSION_H

/** @file sm_fota_session.h
 *
 * @brief State of the FOTA session, shared by AT#XFOTA and AT#XNRFCLOUDFOTA.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/storage/flash_map.h>

/* MCUboot bootloader FOTA needs the two MCUboot slots. */
#if FIXED_PARTITION_EXISTS(s1_partition)
#define SM_FOTA_BL_SUPPORTED 1
#endif

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

extern enum sm_fota_image_type sm_fota_type; /* FOTA image type. */
extern enum fota_stage sm_fota_stage; /* Current stage of FOTA process. */
extern enum fota_status sm_fota_status; /* FOTA process status. */
extern int32_t sm_fota_info; /* FOTA download percentage or failure cause in case of error. */
extern bool sm_fota_nrfcloud; /* FOTA was started via AT#XNRFCLOUDFOTA. */
extern struct modem_pipe *sm_fota_pipe; /* Pipe of the AT#XFOTA command that started the FOTA. */

/** MCUboot bootloader FOTA: Active slot fw_info version before a bootloader update was started. */
extern uint32_t sm_fota_bl_version_before;

/** @brief Sets the FOTA state variables to their default values. */
void sm_fota_init_state(void);

/**
 * @brief Record the running MCUboot version before an MCUboot bootloader update is started.
 *
 * The version is saved to NVM, so that the update can be validated after the reset that
 * activates it.
 *
 * @retval 0 on success.
 * @retval -ENOTSUP if the device has no MCUboot bootloader slots.
 * @return A negative error code if the running version could not be read or saved.
 */
int sm_fota_mcuboot_bl_state_record(void);

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

/** @} */

#endif /* SM_FOTA_SESSION_H */
