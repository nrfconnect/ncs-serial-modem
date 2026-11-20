/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sm_at_host.h"
#include "sm_settings.h"
#include "sm_fota_session.h"

LOG_MODULE_REGISTER(sm_fota_session, CONFIG_SM_LOG_LEVEL);

enum sm_fota_image_type sm_fota_type = SM_FOTA_TYPE_NONE;
enum fota_stage sm_fota_stage = FOTA_STAGE_INIT;
enum fota_status sm_fota_status;
int32_t sm_fota_info;
bool sm_fota_nrfcloud;
struct modem_pipe *sm_fota_pipe;

void sm_fota_init_state(void)
{
	sm_fota_type = SM_FOTA_TYPE_NONE;
	sm_fota_stage = FOTA_STAGE_INIT;
	sm_fota_status = FOTA_STATUS_OK;
	sm_fota_info = 0;
	sm_fota_nrfcloud = false;
}

#if defined(CONFIG_LWM2M_CARRIER)
static K_SEM_DEFINE(carrier_app_fota_status, 0, 1);
static bool carrier_app_fota_success;

bool lwm2m_os_dfu_application_update_validate(void)
{
	/* Wait for the application FOTA status to be checked by the main thread. This can
	 * trigger an AT notification, so the UART backend must also be initialized.
	 */
	if (k_sem_take(&carrier_app_fota_status, K_SECONDS(10)) != 0) {
		return false;
	}

	return carrier_app_fota_success;
}
#endif /* CONFIG_LWM2M_CARRIER */

void sm_fota_post_process(void)
{
#if defined(CONFIG_LWM2M_CARRIER)
	if ((sm_fota_type == SM_FOTA_TYPE_APP || sm_fota_type == SM_FOTA_TYPE_MCUBOOT_BL) &&
	    (sm_fota_status == FOTA_STATUS_OK) &&
	    (sm_fota_stage == FOTA_STAGE_COMPLETE)) {
		carrier_app_fota_success = true;
	}

	k_sem_give(&carrier_app_fota_status);
#endif /* CONFIG_LWM2M_CARRIER */
	if (sm_fota_stage != FOTA_STAGE_COMPLETE && sm_fota_stage != FOTA_STAGE_ACTIVATE) {
		return;
	}
	LOG_INF("FOTA result stage=%d status=%d info=%d", sm_fota_stage, sm_fota_status,
		sm_fota_info);

	struct modem_pipe *pipe = sm_fota_pipe ? sm_fota_pipe : sm_at_host_get_urc_pipe();
	const char *urc_name = sm_fota_nrfcloud ? "XNRFCLOUDFOTA" : "XFOTA";

	if (sm_fota_status == FOTA_STATUS_OK) {
		urc_send_to(pipe, "\r\n#%s: %d,%d\r\n", urc_name, sm_fota_stage, sm_fota_status);
	} else {
		urc_send_to(pipe, "\r\n#%s: %d,%d,%d\r\n", urc_name, sm_fota_stage, sm_fota_status,
			sm_fota_info);
	}

	sm_fota_init_state();
	sm_settings_fota_save();
}
