/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <stdint.h>
#include <stdio.h>
#include <modem/nrf_modem_lib.h>
#include <nrf_modem_delta_dfu.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/parser_url.h>
#include <strings.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>
#include <net/fota_download.h>
#include <pm_config.h>
#include <dfu/dfu_target_full_modem.h>
#include <dfu/fmfu_fdev.h>
#include "sm_util.h"
#include "sm_settings.h"
#include "sm_at_host.h"
#include "sm_at_fota.h"
#include "sm_defines.h"
#include "sm_log.h"

LOG_MODULE_REGISTER(sm_fota, CONFIG_SM_LOG_LEVEL);

#define ERASE_POLL_TIME 2

/* Some features need fota_download update */
#define FOTA_FUTURE_FEATURE	0

enum sm_fota_operation {
	SM_FOTA_STOP = 0,
	SM_FOTA_START_APP = 1,
	SM_FOTA_START_MFW = 2,
	SM_FOTA_START_FULL_FOTA = 3,
	SM_FOTA_PAUSE_RESUME = 4,
	SM_FOTA_START_MCUBOOT_BL = 5,
	SM_FOTA_MFW_READ = 7,
	SM_FOTA_ERASE_MFW = 9
};

bool sm_modem_full_fota;
bool sm_fota_bl_pending_validate;
uint32_t sm_fota_bl_version_before;

enum sm_fota_image_type sm_fota_type = SM_FOTA_TYPE_NONE;
enum fota_stage sm_fota_stage = FOTA_STAGE_INIT;
enum fota_status sm_fota_status;
int32_t sm_fota_info;

static struct modem_pipe *fota_pipe;

#if defined(CONFIG_SM_FULL_FOTA)
/* Buffer used as temporary storage when downloading the modem firmware.
 */
#define FMFU_BUF_SIZE	32
static uint8_t fmfu_buf[FMFU_BUF_SIZE];

/* External flash device for full modem firmware storage */
#if !defined(CONFIG_DFU_TARGET_FULL_MODEM_USE_EXT_PARTITION)
#define FLASH_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(jedec_spi_nor)
static const struct device *flash_dev = DEVICE_DT_GET(FLASH_NODE);
#endif

/* dfu_target specific configurations */
static struct dfu_target_fmfu_fdev fdev;
static struct dfu_target_full_modem_params full_modem_fota_params;

/* Setup full modem FOTA configuration */
static int setup_full_modem_fota_config(void)
{
	int err;

	full_modem_fota_params.buf = fmfu_buf;
	full_modem_fota_params.len = sizeof(fmfu_buf);
	full_modem_fota_params.dev = &fdev;

#if defined(CONFIG_DFU_TARGET_FULL_MODEM_USE_EXT_PARTITION)
	fdev.dev = NULL;
	fdev.offset = 0;
	fdev.size = 0;
#else
	fdev.dev = flash_dev;
	fdev.offset = 0;
	fdev.size = DT_PROP(FLASH_NODE, size) / 8;

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device %s not ready", flash_dev->name);
		return -ENXIO;
	}
#endif

	err = dfu_target_full_modem_cfg(&full_modem_fota_params);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("dfu_target_full_modem_cfg failed: %d", err);
		return err;
	}

	err = dfu_target_full_modem_fdev_get(&fdev);
	if (err != 0) {
		LOG_ERR("dfu_target_full_modem_fdev_get failed: %d", err);
		return err;
	}

	return 0;
}
#endif

static int do_fota_mfw_read(void)
{
	int err;
	size_t offset = 0;
	size_t area = 0;

	err = nrf_modem_delta_dfu_area(&area);
	if (err) {
		LOG_ERR("failed in delta dfu area: %d", err);
		return err;
	}

	err = nrf_modem_delta_dfu_offset(&offset);
	if (err) {
		LOG_ERR("failed in delta dfu offset: %d", err);
		return err;
	}

	rsp_send("\r\n#XFOTA: %d,%d\r\n", area, offset);

	return 0;
}

static int do_fota_erase_mfw(void)
{
	int err;
	size_t offset = 0;
	bool in_progress = false;

	err = nrf_modem_delta_dfu_offset(&offset);
	if (err) {
		if (err == NRF_MODEM_DELTA_DFU_ERASE_PENDING) {
			in_progress = true;
		} else {
			LOG_ERR("failed in delta dfu offset: %d", err);
			return err;
		}
	}

	if (offset != NRF_MODEM_DELTA_DFU_OFFSET_DIRTY && !in_progress) {
		/* No need to erase. */
		return 0;
	}

	if (!in_progress) {
		err = nrf_modem_delta_dfu_erase();
		if (err) {
			LOG_ERR("failed in delta dfu erase: %d", err);
			return err;
		}
	}

	int time_elapsed = 0;

	do {
		k_sleep(K_SECONDS(ERASE_POLL_TIME));
		err = nrf_modem_delta_dfu_offset(&offset);
		if (err != 0 && err != NRF_MODEM_DELTA_DFU_ERASE_PENDING) {
			LOG_ERR("failed in delta dfu offset: %d", err);
			return err;
		}
		if (err == 0 && offset == 0) {
			LOG_INF("Erase completed");
			break;
		}
		time_elapsed += ERASE_POLL_TIME;
	} while (time_elapsed < CONFIG_DFU_TARGET_MODEM_TIMEOUT);

	if (time_elapsed >= CONFIG_DFU_TARGET_MODEM_TIMEOUT) {
		LOG_WRN("Erase timeout");
		return -ETIME;
	}

	return 0;
}

static int do_fota_start(const char *file_uri, size_t file_uri_len, int sec_tag,
			 uint8_t pdn_id, enum dfu_target_image_type type)
{
	int ret;
	struct http_parser_url parser = {
		.field_set = 0
	};
	size_t path_off;
	char *hostname = NULL;
	char *path = NULL;

	http_parser_url_init(&parser);
	ret = http_parser_parse_url(file_uri, file_uri_len, 0, &parser);
	if (ret) {
		LOG_ERR("Parse URL error");
		return -EINVAL;
	}

	if (!(parser.field_set & (1 << UF_PATH))) {
		LOG_ERR("Parse path error");
		return -EINVAL;
	}

	if (!(parser.field_set & (1 << UF_HOST))) {
		LOG_ERR("Parse host error");
		return -EINVAL;
	}

	path_off = parser.field_data[UF_PATH].off;

	/* host: scheme + authority (everything before the path), e.g. "https://host:port" */
	hostname = k_malloc(path_off + 1);
	if (!hostname) {
		return -ENOMEM;
	}
	memcpy(hostname, file_uri, path_off);
	hostname[path_off] = '\0';

	/* path: URI content after the leading '/' */
	path = k_malloc(file_uri_len - path_off);
	if (!path) {
		k_free(hostname);
		return -ENOMEM;
	}
	memcpy(path, file_uri + path_off + 1, file_uri_len - path_off - 1);
	path[file_uri_len - path_off - 1] = '\0';

	/* start HTTP(S) FOTA (hostname buffer includes scheme and port, see http_parser) */
	if (strncasecmp(hostname, "https://", strlen("https://")) == 0) {
		if (sec_tag == SEC_TAG_TLS_INVALID) {
			LOG_ERR("Missing sec_tag");
			ret = -EINVAL;
		} else {
			ret = fota_download_start_with_image_type(hostname, path, sec_tag, pdn_id,
								  0, type);
		}
	} else if (strncasecmp(hostname, "http://", strlen("http://")) == 0) {
		ret = fota_download_start_with_image_type(hostname, path, -1, pdn_id, 0, type);
	} else {
		ret = -EINVAL;
	}

	k_free(hostname);
	k_free(path);

	/* Send an URC if failed to start */
	if (ret) {
		rsp_send("\r\n#XFOTA: %d,%d,%d\r\n", FOTA_STAGE_DOWNLOAD,
			FOTA_STATUS_ERROR, ret);
	}

	return ret;
}

static void fota_dl_handler(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		sm_fota_stage = FOTA_STAGE_DOWNLOAD;
		sm_fota_status = FOTA_STATUS_OK;
		sm_fota_info = evt->progress;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d,%d\r\n",
			sm_fota_stage, sm_fota_status, sm_fota_info);
		break;
	case FOTA_DOWNLOAD_EVT_FINISHED:
		sm_fota_stage = FOTA_STAGE_ACTIVATE;
		sm_fota_info = 0;
		sm_modem_full_fota = (sm_fota_type == SM_FOTA_TYPE_FULL_MFW);
#if defined(PM_S1_ADDRESS)
		sm_fota_bl_pending_validate = (sm_fota_type == SM_FOTA_TYPE_MCUBOOT_BL);
#endif
		/* Save, in case reboot by reset */
		sm_settings_fota_save();
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		break;
	case FOTA_DOWNLOAD_EVT_ERASE_TIMEOUT:
		LOG_INF("Erasure timeout reached. Erasure continues.");
		break;
	case FOTA_DOWNLOAD_EVT_ERASE_PENDING:
		sm_fota_stage = FOTA_STAGE_DOWNLOAD_ERASE_PENDING;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		break;
	case FOTA_DOWNLOAD_EVT_ERASE_DONE:
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", FOTA_STAGE_DOWNLOAD_ERASED,
			    sm_fota_status);
		/* Back to init now that the erasure is complete so that potential pre-start
		 * error codes are printed with the same stage than if there had been no erasure.
		 */
		sm_fota_stage = FOTA_STAGE_INIT;
		break;
	case FOTA_DOWNLOAD_EVT_ERROR:
		sm_fota_status = FOTA_STATUS_ERROR;
		sm_fota_info = evt->cause;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d,%d\r\n",
			sm_fota_stage, sm_fota_status, sm_fota_info);
		/* FOTA session terminated */
		sm_fota_init_state();
		break;
	case FOTA_DOWNLOAD_EVT_CANCELLED:
		sm_fota_status = FOTA_STATUS_CANCELLED;
		sm_fota_info = 0;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		/* FOTA session terminated */
		sm_fota_init_state();
		break;

	default:
		return;
	}
}

SM_AT_CMD_CUSTOM(xfota, "AT#XFOTA", handle_at_fota);
static int handle_at_fota(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t op;
#if FOTA_FUTURE_FEATURE
	static bool paused;
#endif

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		fota_pipe = sm_at_host_get_current_pipe();
		err = at_parser_num_get(parser, 1, &op);
		if (err < 0) {
			return err;
		}
		switch (op) {
		case SM_FOTA_STOP:
			err = fota_download_cancel();
			break;
		case SM_FOTA_START_APP:
		case SM_FOTA_START_MFW:
#if defined(PM_S1_ADDRESS)
		case SM_FOTA_START_MCUBOOT_BL:
#endif
#if defined(CONFIG_SM_FULL_FOTA)
		case SM_FOTA_START_FULL_FOTA:
#endif
		{
			/* We cannot handle multiple simultaneous FOTA activations. */
			if (sm_fota_stage == FOTA_STAGE_ACTIVATE) {
				err = -EBUSY;
				break;
			}

			uint16_t pdn_id = 0;
			sec_tag_t sec_tag = SEC_TAG_TLS_INVALID;
			enum dfu_target_image_type type;
			const char *uri_ptr;
			size_t uri_len;

			err = at_parser_string_ptr_get(parser, 2, &uri_ptr, &uri_len);
			if (err) {
				break;
			}
			if (param_count > 3) {
				at_parser_num_get(parser, 3, &sec_tag);
			}
			if (param_count > 4) {
				at_parser_num_get(parser, 4, &pdn_id);
			}
			if (op == SM_FOTA_START_APP || op == SM_FOTA_START_MCUBOOT_BL) {
				type = DFU_TARGET_IMAGE_TYPE_MCUBOOT;
			}
#if defined(CONFIG_SM_FULL_FOTA)
			else if (op == SM_FOTA_START_FULL_FOTA) {
				err = setup_full_modem_fota_config();
				if (err != 0) {
					break;
				}
				type = DFU_TARGET_IMAGE_TYPE_FULL_MODEM;
			}
#endif
			else {
				type = DFU_TARGET_IMAGE_TYPE_MODEM_DELTA;
			}
			err = do_fota_start(uri_ptr, uri_len, sec_tag, pdn_id, type);
			sm_fota_init_state();
			if (op == SM_FOTA_START_MFW) {
				sm_fota_type = SM_FOTA_TYPE_MFW;
			}
#if defined(CONFIG_SM_FULL_FOTA)
			else if (op == SM_FOTA_START_FULL_FOTA) {
				sm_fota_type = SM_FOTA_TYPE_FULL_MFW;
			}
#endif
#if defined(PM_S1_ADDRESS)
			else if (op == SM_FOTA_START_MCUBOOT_BL) {
				sm_fota_type = SM_FOTA_TYPE_MCUBOOT_BL;

				/* Save the MCUboot version before FOTA. */
				sm_util_mcuboot_active_version(&sm_fota_bl_version_before);
				sm_settings_fota_save();
			}
#endif
			else {
				sm_fota_type = SM_FOTA_TYPE_APP;
			}
			break;
		}
#if FOTA_FUTURE_FEATURE
		case SM_FOTA_PAUSE_RESUME:
			if (paused) {
				fota_download_resume();
				paused = false;
			} else {
				fota_download_pause();
				paused = true;
			}
			err = 0;
			break;
#endif
		case SM_FOTA_MFW_READ:
			err = do_fota_mfw_read();
			break;
		case SM_FOTA_ERASE_MFW:
			err = do_fota_erase_mfw();
			break;
		default:
			err = -EINVAL;
			break;
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
#if defined(PM_S1_ADDRESS) && defined(CONFIG_SM_FULL_FOTA)
		rsp_send("\r\n#XFOTA: "
			 "(%d,%d,%d,%d,%d,%d,%d)[,<file_url>[,<sec_tag>[,<pdn_id>]]]\r\n",
			 SM_FOTA_STOP, SM_FOTA_START_APP, SM_FOTA_START_MFW,
			 SM_FOTA_START_FULL_FOTA, SM_FOTA_START_MCUBOOT_BL, SM_FOTA_MFW_READ,
			 SM_FOTA_ERASE_MFW);
#elif defined(PM_S1_ADDRESS)
		rsp_send("\r\n#XFOTA: (%d,%d,%d,%d,%d,%d)[,<file_url>[,<sec_tag>[,<pdn_id>]]]\r\n",
			 SM_FOTA_STOP, SM_FOTA_START_APP, SM_FOTA_START_MFW,
			 SM_FOTA_START_MCUBOOT_BL, SM_FOTA_MFW_READ, SM_FOTA_ERASE_MFW);
#elif defined(CONFIG_SM_FULL_FOTA)
		rsp_send("\r\n#XFOTA: (%d,%d,%d,%d,%d,%d)[,<file_url>[,<sec_tag>[,<pdn_id>]]]\r\n",
			 SM_FOTA_STOP, SM_FOTA_START_APP, SM_FOTA_START_MFW,
			 SM_FOTA_START_FULL_FOTA, SM_FOTA_MFW_READ, SM_FOTA_ERASE_MFW);
#else
		rsp_send("\r\n#XFOTA: (%d,%d,%d,%d,%d)[,<file_url>[,<sec_tag>[,<pdn_id>]]]\r\n",
			SM_FOTA_STOP, SM_FOTA_START_APP, SM_FOTA_START_MFW,
			SM_FOTA_MFW_READ, SM_FOTA_ERASE_MFW);
#endif
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

static int sm_at_fota_init(void)
{
	int ret = fota_download_init(fota_dl_handler);

	if (ret) {
		LOG_ERR("fota_download_init failed: %d", ret);
		sm_init_failed = true;
		return ret;
	}

	return 0;
}
SYS_INIT(sm_at_fota_init, APPLICATION, 0);

void sm_fota_init_state(void)
{
	sm_modem_full_fota = false;
	sm_fota_type = SM_FOTA_TYPE_NONE;
	sm_fota_stage = FOTA_STAGE_INIT;
	sm_fota_status = FOTA_STATUS_OK;
	sm_fota_info = 0;
}

void sm_fota_mcuboot_bl_boot_check(void)
{
#if defined(PM_S1_ADDRESS)
	if (!sm_fota_bl_pending_validate) {
		return;
	}

	uint32_t current_version = 0;
	int err = sm_util_mcuboot_active_version(&current_version);

	if (err != 0) {
		sm_fota_status = FOTA_STATUS_ERROR;
		sm_fota_info = err;
		LOG_ERR("MCUboot validate: FAIL (fw_info read err %d)", err);
	} else if (current_version <= sm_fota_bl_version_before) {
		sm_fota_status = FOTA_STATUS_ERROR;
		sm_fota_info = 1;
		LOG_WRN("MCUboot validate: FAIL (version unchanged: %u -> %u)",
			sm_fota_bl_version_before, current_version);
	} else {
		sm_fota_status = FOTA_STATUS_OK;
		sm_fota_info = 0;
	}

	sm_fota_type = SM_FOTA_TYPE_MCUBOOT_BL;
	sm_fota_stage = FOTA_STAGE_COMPLETE;
	sm_fota_bl_pending_validate = false;
#endif
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
	LOG_INF("FOTA result %d,%d,%d", sm_fota_stage, sm_fota_status, sm_fota_info);

	struct modem_pipe *pipe = fota_pipe ? fota_pipe : sm_at_host_get_urc_pipe();

	if (sm_fota_status == FOTA_STATUS_OK) {
		urc_send_to(pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
	} else {
		urc_send_to(pipe, "\r\n#XFOTA: %d,%d,%d\r\n", sm_fota_stage, sm_fota_status,
			sm_fota_info);
	}

	sm_fota_init_state();
	sm_settings_fota_save();
}

#if defined(CONFIG_SM_FULL_FOTA)

FUNC_NORETURN static void handle_full_fota_activation_fail(int ret)
{
	int err;

	/* Send the result notification and terminate the FOTA session. */
	sm_fota_status = FOTA_STATUS_ERROR;
	sm_fota_info = ret;
	sm_fota_post_process();

	LOG_ERR("Modem firmware activation failed, error: %d", ret);

	/* Extenal flash needs to be erased and internal counters cleared */
	err = dfu_target_reset();
	if (err != 0)
		LOG_ERR("dfu_target_reset() failed: %d", err);
	else
		LOG_INF("External flash erase succeeded");

	LOG_WRN("Rebooting...");
	sm_log_flush();
	sys_reboot(SYS_REBOOT_COLD);
}

void sm_finish_modem_full_fota(void)
{
	int err;

	/* All erroneous steps in activation stage are
	 * considered fatal and the device is reset.
	 */
	sm_fota_stage = FOTA_STAGE_COMPLETE;
	LOG_INF("Applying full modem firmware update from external flash");

	err = nrf_modem_lib_bootloader_init();
	if (err != 0) {
		LOG_ERR("nrf_modem_lib_bootloader_init() failed: %d", err);
		handle_full_fota_activation_fail(err);
	}

	/* Re-establish dfu_target configuration after reboot */
	err = setup_full_modem_fota_config();
	if (err != 0) {
		handle_full_fota_activation_fail(err);
	}

	err = fmfu_fdev_load(fmfu_buf, sizeof(fmfu_buf), fdev.dev, fdev.offset);
	if (err != 0) {
		LOG_ERR("fmfu_fdev_load failed: %d", err);
		handle_full_fota_activation_fail(err);
	}

	err = nrf_modem_lib_shutdown();
	if (err != 0) {
		LOG_ERR("nrf_modem_lib_shutdown() failed: %d", err);
		handle_full_fota_activation_fail(err);
	}

	sm_fota_status = FOTA_STATUS_OK;
	sm_fota_info = 0;

	LOG_INF("Full modem firmware update complete.");
}

#endif /* CONFIG_SM_FULL_FOTA */
