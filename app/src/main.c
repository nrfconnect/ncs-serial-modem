/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <nrf_modem.h>
#include <hal/nrf_power.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/dfu/mcuboot.h>
#include <dfu/dfu_target.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>
#include <net/fota_download.h>
#include "sm_at_host.h"
#include "sm_at_dfu.h"
#include "sm_at_fota.h"
#include "sm_util.h"
#include "sm_ctrl_pin.h"
#include "sm_uart_handler.h"

LOG_MODULE_REGISTER(sm, CONFIG_SM_LOG_LEVEL);

struct k_work_q sm_work_q;
bool sm_init_failed = false;

NRF_MODEM_LIB_ON_INIT(lwm2m_init_hook, on_modem_lib_init, NULL);
NRF_MODEM_LIB_ON_DFU_RES(main_dfu_hook, on_modem_dfu_res, NULL);

static void on_modem_lib_init(int ret, void *ctx)
{
	ARG_UNUSED(ctx);

	/** ret: Zero on success, a positive value @em nrf_modem_dfu when executing
	 *  Modem firmware updates, and negative errno on other failures.
	 */
	LOG_INF("lib_modem init: %d", ret);
}

#if defined(CONFIG_NRF_MODEM_LIB_ON_FAULT_APPLICATION_SPECIFIC)
static struct nrf_modem_fault_info modem_fault_info;

static void on_modem_failure(struct k_work *)
{
	int ret;

	rsp_send("\r\n#XMODEM: FAULT,0x%x,0x%x\r\n", modem_fault_info.reason,
		 modem_fault_info.program_counter);

	ret = nrf_modem_lib_shutdown();
	rsp_send("\r\n#XMODEM: SHUTDOWN,%d\r\n", ret);

	ret = nrf_modem_lib_init();
	rsp_send("\r\n#XMODEM: INIT,%d\r\n", ret);
}
K_WORK_DEFINE(modem_failure_work, on_modem_failure);

void nrf_modem_fault_handler(struct nrf_modem_fault_info *fault_info)
{
	modem_fault_info = *fault_info;

	k_work_submit_to_queue(&sm_work_q, &modem_failure_work);
}
#endif /* CONFIG_NRF_MODEM_LIB_ON_FAULT_APPLICATION_SPECIFIC */

static void on_modem_dfu_res(int dfu_res, void *ctx)
{
	sm_fota_type = DFU_TARGET_IMAGE_TYPE_MODEM_DELTA;
	sm_fota_stage = FOTA_STAGE_COMPLETE;
	sm_fota_status = FOTA_STATUS_ERROR;
	sm_fota_info = dfu_res;

	switch (dfu_res) {
	case NRF_MODEM_DFU_RESULT_OK:
		LOG_INF("Modem update OK. Running new firmware.");
		sm_fota_status = FOTA_STATUS_OK;
		sm_fota_info = 0;
		break;
	case NRF_MODEM_DFU_RESULT_UUID_ERROR:
	case NRF_MODEM_DFU_RESULT_AUTH_ERROR:
		LOG_ERR("Modem update failed (0x%x). Running old firmware.", dfu_res);
		break;
	case NRF_MODEM_DFU_RESULT_HARDWARE_ERROR:
	case NRF_MODEM_DFU_RESULT_INTERNAL_ERROR:
		LOG_ERR("Fatal error (0x%x) encountered during modem update.", dfu_res);
		break;
	case NRF_MODEM_DFU_RESULT_VOLTAGE_LOW:
		LOG_ERR("Modem update postponed due to low voltage. "
			"Reset the modem once you have sufficient power.");
		sm_fota_stage = FOTA_STAGE_ACTIVATE;
		break;
	default:
		LOG_ERR("Unhandled nrf_modem DFU result code 0x%x.", dfu_res);
		break;
	}
}

static void check_app_fota_status(void)
{
	/** When a TEST image is swapped to primary partition and booted by MCUBOOT,
	 * the API mcuboot_swap_type() will return BOOT_SWAP_TYPE_REVERT. By this type
	 * MCUBOOT means that the TEST image is booted OK and, if it's not confirmed
	 * next, it'll be swapped back to secondary partition and original application
	 * image will be restored to the primary partition (so-called Revert).
	 */
	const int type = mcuboot_swap_type();

	switch (type) {
	/** Attempt to boot the contents of slot 0. */
	case BOOT_SWAP_TYPE_NONE:
		/* Normal reset, nothing happened, do nothing. */
		return;
	/** Swap to slot 1. Absent a confirm command, revert back on next boot. */
	case BOOT_SWAP_TYPE_TEST:
	/** Swap to slot 1, and permanently switch to booting its contents. */
	case BOOT_SWAP_TYPE_PERM:
	/** Swap failed because image to be run is not valid. */
	case BOOT_SWAP_TYPE_FAIL:
		sm_fota_status = FOTA_STATUS_ERROR;
		sm_fota_info = type;
		break;
	/** Swap back to alternate slot. A confirm changes this state to NONE. */
	case BOOT_SWAP_TYPE_REVERT:
		/* Happens on a successful application FOTA. */
		const int ret = boot_write_img_confirmed();

		sm_fota_info = ret;
		sm_fota_status = ret ? FOTA_STATUS_ERROR : FOTA_STATUS_OK;
		break;
	}
	sm_fota_type = DFU_TARGET_IMAGE_TYPE_MCUBOOT;
	sm_fota_stage = FOTA_STAGE_COMPLETE;
}

static int bootloader_mode_init(void)
{
	int ret;

	ret = nrf_modem_lib_bootloader_init();
	if (ret) {
		LOG_ERR("Failed to initialize bootloader mode: %d", ret);
		return ret;
	}
	LOG_INF("Bootloader mode initiated successfully");

	ret = sm_at_host_bootloader_init();
	if (ret) {
		LOG_ERR("Failed to init at_host: %d", ret);
		return ret;
	}

	ret = sm_at_send_str("Bootloader mode ready\r\n");
	if (ret) {
		LOG_ERR("Failed to send bootloader mode ready string: %d", ret);
		return ret;
	}

	sm_bootloader_mode_enabled = true;

	return 0;
}

void lte_auto_connect(void)
{
#if defined(CONFIG_SM_AUTO_CONNECT)
	int err;
	int n;
	int stat;

	err = sm_util_at_scanf("AT+CEREG?", "+CEREG: %d,%d", &n, &stat);
	if (err != 2 || (stat == 1 || stat == 5)) {
		return;
	}

	LOG_INF("LTE auto connect");
	LOG_DBG("Configuring system mode: %s", CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE);
	err = sm_util_at_printf("AT%%XSYSTEMMODE=%s", CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE);
	if (err) {
		LOG_ERR("Failed to configure system mode \"%s\": %d",
			CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE, err);
		return;
	}

#if defined(CONFIG_SM_AUTO_CONNECT_PDN_CONFIG)
	err = sm_util_at_printf("AT+CGDCONT=0,%s,%s", CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_STRING,
				CONFIG_SM_AUTO_CONNECT_PDN_APN);
	if (err) {
		LOG_ERR("Failed to configure PDN: %d", err);
		return;
	}
	LOG_DBG("PDN configured: APN=\"%s\", PDN type=\"%s\"", CONFIG_SM_AUTO_CONNECT_PDN_APN,
		CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_STRING);

	if (CONFIG_SM_AUTO_CONNECT_PDN_AUTH != 0) {
		err = sm_util_at_printf("AT+CGAUTH=0,%d,%s,%s", CONFIG_SM_AUTO_CONNECT_PDN_AUTH,
					CONFIG_SM_AUTO_CONNECT_PDN_USERNAME,
					CONFIG_SM_AUTO_CONNECT_PDN_PASSWORD);
		if (err) {
			LOG_ERR("Failed to configure AUTH: %d", err);
			return;
		}
		LOG_DBG("PDN AUTH configured: protocol=%d, username=\"%s\"",
			CONFIG_SM_AUTO_CONNECT_PDN_AUTH, CONFIG_SM_AUTO_CONNECT_PDN_USERNAME);
	}
#endif /* CONFIG_SM_AUTO_CONNECT_PDN_CONFIG */

	err = sm_util_at_printf("AT+CFUN=1");
	if (err) {
		LOG_ERR("Failed to turn on radio: %d", err);
		return;
	}

#endif /* CONFIG_SM_AUTO_CONNECT */
}

static int init_sm_work_q(void)
{
	k_work_queue_init(&sm_work_q);
	return 0;
}
SYS_INIT(init_sm_work_q, PRE_KERNEL_1, 0);

int main(void)
{
	static const struct k_work_queue_config cfg = {
		.name = "sm_work_q",
		.essential = true,
	};

	k_thread_priority_set(k_current_get(), K_LOWEST_APPLICATION_THREAD_PRIO);
	k_work_queue_run(&sm_work_q, &cfg);
	return 0;
}

static int sm_main(void)
{
	int ret;

	const uint32_t rr = nrf_power_resetreas_get(NRF_POWER_NS);

	nrf_power_resetreas_clear(NRF_POWER_NS, 0x70017);
	LOG_DBG("RR: 0x%08x", rr);

	if (sm_bootloader_mode_requested) {
		/* Clear bootloader mode flag */
		ret = bootloader_mode_request(false);
		if (ret) {
			LOG_ERR("Failed to clear bootloader mode flag, starting SM in normal mode");
		} else {
			ret = bootloader_mode_init();
			if (ret) {
				LOG_ERR("Failed to initialize bootloader mode: %d", ret);
				goto exit_reboot;
			}
			return ret;
		}
	}

	ret = sm_uart_handler_enable();
	if (ret) {
		LOG_ERR("Failed to enable UART handler (%d).", ret);
		return ret;
	}

#if defined(CONFIG_SM_FULL_FOTA)
	if (sm_modem_full_fota) {
		sm_finish_modem_full_fota();
		sm_fota_type = DFU_TARGET_IMAGE_TYPE_FULL_MODEM;
	}
#endif

	ret = nrf_modem_lib_init();

	if (ret) {
		LOG_ERR("Modem library init failed, err: %d", ret);
		if (ret != -EAGAIN && ret != -EIO) {
			return ret;
		} else if (ret == -EIO) {
			LOG_ERR("Please program full modem firmware with the bootloader or "
				"external tools");
			(void)bootloader_mode_request(true);
			goto exit_reboot;
		}
	}

	check_app_fota_status();

	if (!IS_ENABLED(CONFIG_SM_SKIP_READY_MSG)) {
		if (sm_init_failed) {
			ret = sm_at_send_str(SM_SYNC_ERR_STR);
		} else {
			ret = sm_at_send_str(SM_SYNC_STR);
		}

		if (ret) {
			return ret;
		}
	}

	/* This is here and not earlier because in case of firmware
	 * update it will send an AT response so the UART must be up.
	 */
	sm_fota_post_process();

	lte_auto_connect();

	LOG_INF("Serial Modem");

	return ret;

exit_reboot:
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}
SYS_INIT(sm_main, APPLICATION, 100);
