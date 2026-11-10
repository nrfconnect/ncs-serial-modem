/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <modem/nrf_modem_lib.h>
#include "sm_fota_session.h"
#include "sm_fw_update.h"

LOG_MODULE_REGISTER(sm_fw_update, CONFIG_SM_LOG_LEVEL);

/* Reserves the modem DFU area for direct delta modem DFU, so that AT#XFOTA and AT#XNRFCLOUDFOTA
 * cannot erase or overwrite a partly written or scheduled image.
 */
static atomic_t modem_dfu_claimed;

static void on_modem_dfu_res(int dfu_res, void *ctx)
{
	switch (dfu_res) {
	case NRF_MODEM_DFU_RESULT_OK:
		LOG_INF("Modem update OK, running new firmware");
		break;
	case NRF_MODEM_DFU_RESULT_UUID_ERROR:
	case NRF_MODEM_DFU_RESULT_AUTH_ERROR:
		LOG_ERR("Modem update failed: 0x%x, running old firmware", dfu_res);
		break;
	case NRF_MODEM_DFU_RESULT_HARDWARE_ERROR:
	case NRF_MODEM_DFU_RESULT_INTERNAL_ERROR:
		LOG_ERR("Fatal modem update error: 0x%x", dfu_res);
		break;
	case NRF_MODEM_DFU_RESULT_VOLTAGE_LOW:
		LOG_ERR("Modem update postponed, low voltage");
		break;
	default:
		LOG_ERR("Unhandled DFU result: 0x%x", dfu_res);
		break;
	}

	/* Update FOTA state only if a modem delta FOTA was in progress.
	 * sm_fota_type is loaded from NVS; NONE means DFU or no update.
	 */
	if (!IS_ENABLED(CONFIG_SM_FOTA_SESSION) || sm_fota_type != SM_FOTA_TYPE_MFW) {
		return;
	}

	sm_fota_stage  = (dfu_res == NRF_MODEM_DFU_RESULT_VOLTAGE_LOW)
			? FOTA_STAGE_ACTIVATE : FOTA_STAGE_COMPLETE;
	sm_fota_status = (dfu_res == NRF_MODEM_DFU_RESULT_OK)
			? FOTA_STATUS_OK : FOTA_STATUS_ERROR;
	sm_fota_info   = (dfu_res == NRF_MODEM_DFU_RESULT_OK) ? 0 : dfu_res;
}
NRF_MODEM_LIB_ON_DFU_RES(sm_fw_update_dfu_hook, on_modem_dfu_res, NULL);

void sm_fw_update_boot_check(void)
{
	/** When a TEST image is swapped to primary partition and booted by MCUBOOT,
	 * the API mcuboot_swap_type() will return BOOT_SWAP_TYPE_REVERT. By this type
	 * MCUBOOT means that the TEST image is booted OK and, if it's not confirmed
	 * next, it'll be swapped back to secondary partition and original application
	 * image will be restored to the primary partition (so-called Revert).
	 */
	const int type = mcuboot_swap_type();
	int ret = 0;

	switch (type) {
	/** Attempt to boot the contents of slot 0. */
	case BOOT_SWAP_TYPE_NONE:
		/* Normal boot, nothing happened, do nothing. */
		return;
	/** Swap to slot 1. Absent a confirm command, revert back on next boot. */
	case BOOT_SWAP_TYPE_TEST:
	/** Swap to slot 1, and permanently switch to booting its contents. */
	case BOOT_SWAP_TYPE_PERM:
	/** Swap failed because image to be run is not valid. */
	case BOOT_SWAP_TYPE_FAIL:
		break;
	/** Swap back to alternate slot. A confirm changes this state to NONE. */
	case BOOT_SWAP_TYPE_REVERT:
		/* Happens on a successful application FOTA.
		 * boot_write_img_confirmed() resolves the flash area via
		 * slot_0_partition. It writes image_ok where MCUboot expects it.
		 */
		ret = boot_write_img_confirmed_multi(0);
		LOG_INF("Image confirmed: %d", ret);
		break;
	}
	/* Only report FOTA completion if an app FOTA was in progress.
	 * sm_fota_type is loaded from NVS; NONE means DFU or no update.
	 */
	if (!IS_ENABLED(CONFIG_SM_FOTA_SESSION) || sm_fota_type != SM_FOTA_TYPE_APP) {
		return;
	}
	sm_fota_status = (type == BOOT_SWAP_TYPE_REVERT && ret == 0)
			? FOTA_STATUS_OK : FOTA_STATUS_ERROR;
	sm_fota_info   = (type == BOOT_SWAP_TYPE_REVERT) ? ret : type;
	sm_fota_stage  = FOTA_STAGE_COMPLETE;
}

int sm_fw_update_modem_dfu_claim(void)
{
	if (IS_ENABLED(CONFIG_SM_FOTA_SESSION) && sm_fota_stage != FOTA_STAGE_INIT) {
		LOG_ERR("FOTA in progress");
		return -EBUSY;
	}
	atomic_set(&modem_dfu_claimed, 1);
	return 0;
}

bool sm_fw_update_modem_dfu_busy(void)
{
	return atomic_get(&modem_dfu_claimed) != 0;
}

static void modem_dfu_release(void *ctx)
{
	ARG_UNUSED(ctx);

	atomic_set(&modem_dfu_claimed, 0);
}
NRF_MODEM_LIB_ON_SHUTDOWN(sm_fw_update_shutdown, modem_dfu_release, NULL);
