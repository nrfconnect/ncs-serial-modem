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

/** @brief Sets the FOTA state variables to their default values. */
void sm_fota_init_state(void);

/**
 * @brief FOTA post-process after reboot.
 */
void sm_fota_post_process(void);

/** @} */

#endif /* SM_FOTA_SESSION_H */
