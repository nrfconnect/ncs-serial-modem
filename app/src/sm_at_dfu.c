/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <modem/nrf_modem_lib.h>
#include <nrf_modem_bootloader.h>
#include <dfu/dfu_target.h>
#include <dfu/dfu_target_modem_delta.h>
#include <dfu/dfu_target_mcuboot.h>
#include "sm_at_host.h"
#include "sm_at_dfu.h"
#include "sm_settings.h"
#include "sm_uart_handler.h"

LOG_MODULE_REGISTER(sm_dfu, CONFIG_SM_LOG_LEVEL);

#define APP_DFU_BUFFER_SIZE 1024

enum xdfu_image_type {
	DFU_TYPE_APP = 0,
	DFU_TYPE_DELTA_MFW = 1,
	DFU_TYPE_FULL_MFW = 2,
};

enum xdfu_full_mfw_segment_type {
	DFU_FULL_MFW_SEGMENT_BOOTLOADER = 0,
	DFU_FULL_MFW_SEGMENT_FIRMWARE = 1,
};

enum xdfu_operation {
	DFU_OPERATION_INITIALIZE = 0,
	DFU_OPERATION_DATA_WRITE = 1,
	DFU_OPERATION_APPLY_UPDATE = 2,
};
struct xdfu_app_datamode_context {
	size_t addr;
	size_t len;
} xdfu_app_datamode_context;

struct xdfu_delta_mfw_datamode_context {
	size_t addr;
	size_t len;
} xdfu_delta_mfw_datamode_context;

struct xdfu_full_mfw_datamode_context {
	size_t addr;
	size_t len;
} xdfu_full_mfw_datamode_context;

bool sm_bootloader_mode_requested;
bool sm_bootloader_mode_enabled;

int full_mfw_dfu_segment_type = DFU_FULL_MFW_SEGMENT_BOOTLOADER;

static uint8_t app_dfu_buffer[APP_DFU_BUFFER_SIZE] __aligned(4);
static bool app_dfu_buffer_initialized;

static enum xdfu_image_type xdfu_current_image_type;
static uint32_t xdfu_bytes_written;
static int xdfu_status;

static void delta_dfu_evt_handler(enum dfu_target_evt_id evt_id)
{
	switch (evt_id) {
	case DFU_TARGET_EVT_ERASE_PENDING:
		LOG_INF("Delta DFU erase %s", "pending");
		break;
	case DFU_TARGET_EVT_TIMEOUT:
		LOG_WRN("Delta DFU erase %s", "timeout");
		break;
	case DFU_TARGET_EVT_ERASE_DONE:
		LOG_INF("Delta DFU erase %s", "done");
		break;
	default:
		break;
	}
}

int bootloader_mode_request(bool enable)
{
	int err;

	sm_bootloader_mode_requested = enable;

	err = sm_settings_bootloader_mode_save();
	if (err) {
		LOG_ERR("Failed to set bootloader mode requested to: %s",
			enable ? "enabled" : "disabled");
		return err;
	}

	LOG_DBG("Bootloader mode request set to: %s", enable ? "enabled" : "disabled");

	return 0;
}

static int set_full_mfw_dfu_segment_type(enum xdfu_full_mfw_segment_type type)
{
	int err;

	full_mfw_dfu_segment_type = type;

	err = sm_settings_full_mfw_dfu_segment_type_save();
	if (err) {
		LOG_ERR("Failed to set full MFW DFU segment type to: %d", type);
		return err;
	}

	LOG_DBG("Full MFW DFU segment type set to: %d", type);

	return 0;
}

static int xdfu_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	ARG_UNUSED(flags);

	int err;

	switch (op) {
	case DATAMODE_SEND:
		if (data == NULL || len <= 0) {
			LOG_ERR("Chunk data invalid (data=%p len=%d)", (void *)data, len);
			return -EINVAL;
		}

		switch (xdfu_current_image_type) {
		case DFU_TYPE_APP:
			err = dfu_target_mcuboot_write((const void *)data, len);
			if (err) {
				LOG_ERR("Failed to write %s: %d", "app firmware", err);
				xdfu_status = err;
			}
			break;
		case DFU_TYPE_DELTA_MFW:
			err = dfu_target_modem_delta_write((const void *)data, len);
			if (err) {
				LOG_ERR("Failed to write %s: %d", "delta modem firmware", err);
				xdfu_status = err;
			}
			break;
		case DFU_TYPE_FULL_MFW:
			if (full_mfw_dfu_segment_type == DFU_FULL_MFW_SEGMENT_BOOTLOADER) {
				err = nrf_modem_bootloader_bl_write((void *)data, len);
				if (err) {
					LOG_ERR("Failed to write %s: %d", "bootloader segment", err);
					xdfu_status = err;
					break;
				}
			} else if (full_mfw_dfu_segment_type == DFU_FULL_MFW_SEGMENT_FIRMWARE) {
				err = nrf_modem_bootloader_fw_write(
					xdfu_full_mfw_datamode_context.addr,
					(void *)data, len);
				if (err) {
					LOG_ERR("Failed to write %s: %d", "firmware segment", err);
					xdfu_status = err;
					break;
				}

				xdfu_full_mfw_datamode_context.addr += len;
			} else {
				LOG_ERR("Invalid segment type: %d", full_mfw_dfu_segment_type);
				xdfu_status = -EINVAL;
				break;
			}
			break;
		default:
			LOG_ERR("Invalid image type: %d", xdfu_current_image_type);
			return -EINVAL;
		}

		if (xdfu_status == 0) {
			xdfu_bytes_written += len;
		}

		/* Return the amount of data sent. */
		return len;

	case DATAMODE_EXIT: {
		size_t expected_bytes_written;

		switch (xdfu_current_image_type) {
		case DFU_TYPE_APP:
			expected_bytes_written = xdfu_app_datamode_context.len;
			break;
		case DFU_TYPE_DELTA_MFW:
			expected_bytes_written = xdfu_delta_mfw_datamode_context.len;
			break;
		case DFU_TYPE_FULL_MFW:
			expected_bytes_written = xdfu_full_mfw_datamode_context.len;
			break;
		default:
			LOG_ERR("Invalid image type: %d", xdfu_current_image_type);
			xdfu_status = -EINVAL;
			return -EINVAL;
		}

		if (xdfu_status == 0 && xdfu_bytes_written != expected_bytes_written) {
			LOG_WRN("Wrote %u bytes, expected %zu",
				xdfu_bytes_written, expected_bytes_written);
			xdfu_status = -EIO;
		}

		urc_send("#XDFU: %u,%u,%d\r\n",
			xdfu_current_image_type, DFU_OPERATION_DATA_WRITE,
			xdfu_status ? -1 : 0);

		/* Reset for next chunk. */
		xdfu_bytes_written = 0;
		xdfu_status = 0;

		return 0;
	}
	default:
		LOG_WRN("Unexpected data mode op: %u (flags=0x%02x)", op, flags);
		return 0;
	}
}

SM_AT_CMD_CUSTOM(xdfu_init, "AT#XDFUINIT", handle_at_xdfu_init);
static int handle_at_xdfu_init(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				  uint32_t param_count)
{
	ARG_UNUSED(param_count);

	int err;
	uint16_t type;
	size_t size;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &type);
		if (err) {
			LOG_ERR("Failed to get type: %d", err);
			return err;
		}

		if (sm_bootloader_mode_enabled && type != DFU_TYPE_FULL_MFW) {
			LOG_ERR("DFU type %d is not supported in bootloader mode", type);
			return -EOPNOTSUPP;
		}

		switch (type) {
		case DFU_TYPE_APP:
			err = at_parser_num_get(parser, 2, &size);
			if (err) {
				LOG_ERR("Failed to get size: %d", err);
				return err;
			}

			if (!app_dfu_buffer_initialized) {
				err = dfu_target_mcuboot_set_buf(app_dfu_buffer,
								 APP_DFU_BUFFER_SIZE);
				if (err) {
					LOG_ERR("Failed to set app firmware buffer: %d", err);
					return err;
				}
				app_dfu_buffer_initialized = true;
			}

			err = dfu_target_mcuboot_init(size, 0, NULL);
			if (err == -EFAULT) {
				/* Already initialized - abort and retry */
				LOG_WRN("MCUBoot DFU already initialized, aborting and retrying");
				(void)dfu_target_mcuboot_done(false);
				err = dfu_target_mcuboot_init(size, 0, NULL);
			}
			if (err) {
				LOG_ERR("Failed to initialize MCUBoot DFU target: %d", err);
				return err;
			}

			LOG_INF("MCUBoot DFU initialized successfully");
			return 0;
		case DFU_TYPE_DELTA_MFW:
			err = at_parser_num_get(parser, 2, &size);
			if (err) {
				LOG_ERR("Failed to get size: %d", err);
				return err;
			}

			err = dfu_target_modem_delta_init(size, 0, delta_dfu_evt_handler);
			if (err) {
				LOG_ERR("Failed to initialize delta modem firmware: %d", err);
				return err;
			}

			LOG_INF("Delta modem firmware initialized successfully");
			return 0;
		case DFU_TYPE_FULL_MFW:
			if (!IS_ENABLED(CONFIG_SM_DFU_MODEM_FULL)) {
				LOG_ERR("Full modem DFU is not enabled");
				return -EOPNOTSUPP;
			}

			LOG_WRN("WARNING! After the first FW write, the modem will "
				"corrupt if the update is not successfully completed.");
			err = bootloader_mode_request(true);
			if (err) {
				LOG_ERR("Failed to enable bootloader mode: %d", err);
				return err;
			}

			(void)set_full_mfw_dfu_segment_type(DFU_FULL_MFW_SEGMENT_BOOTLOADER);

			LOG_PANIC();
			sys_reboot(SYS_REBOOT_COLD);
		default:
			LOG_ERR("Invalid target type: %d", type);
			return -EINVAL;
		}
	case AT_PARSER_CMD_TYPE_TEST:
#if defined(CONFIG_SM_DFU_MODEM_FULL)
		rsp_send("\r\n#XDFUINIT: (%d,%d,%d),<size>\r\n",
			DFU_TYPE_APP, DFU_TYPE_DELTA_MFW, DFU_TYPE_FULL_MFW);
#else
		rsp_send("\r\n#XDFUINIT: (%d,%d),<size>\r\n",
			DFU_TYPE_APP, DFU_TYPE_DELTA_MFW);
#endif
		return 0;
	default:
		LOG_ERR("Invalid command type: %d", cmd_type);
		return -EINVAL;
	}
}

SM_AT_CMD_CUSTOM(xdfu_write, "AT#XDFUWRITE", handle_at_xdfu_write);
static int handle_at_xdfu_write(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				  uint32_t param_count)
{
	int err;
	uint16_t type;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &type);
		if (err) {
			LOG_ERR("Failed to get type: %d", err);
			return err;
		}

		if (sm_bootloader_mode_enabled && type != DFU_TYPE_FULL_MFW) {
			LOG_ERR("DFU type %d is not supported in bootloader mode", type);
			return -EOPNOTSUPP;
		}

		switch (type) {
		case DFU_TYPE_APP:
			if (param_count != 4) {
				LOG_ERR("Invalid number of parameters for data write");
				return -EINVAL;
			}
			err = at_parser_num_get(parser, 2, &xdfu_app_datamode_context.addr);
			if (err) {
				LOG_ERR("Failed to get address: %d", err);
				return err;
			}
			err = at_parser_num_get(parser, 3, &xdfu_app_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to get length: %d", err);
				return err;
			}

			if (xdfu_app_datamode_context.len == 0) {
				LOG_ERR("Length cannot be 0");
				return -EINVAL;
			}

			/* Prepare per-chunk accounting for the datamode callback. */
			xdfu_current_image_type = DFU_TYPE_APP;
			xdfu_bytes_written = 0;
			xdfu_status = 0;

			err = enter_datamode(xdfu_datamode_callback,
						xdfu_app_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to enter data write mode: %d", err);
				return err;
			}

			return 0;
		case DFU_TYPE_DELTA_MFW:
			if (param_count != 4) {
				LOG_ERR("Invalid number of parameters for data write");
				return -EINVAL;
			}
			err = at_parser_num_get(parser, 2, &xdfu_delta_mfw_datamode_context.addr);
			if (err) {
				LOG_ERR("Failed to get address: %d", err);
				return err;
			}
			err = at_parser_num_get(parser, 3, &xdfu_delta_mfw_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to get length: %d", err);
				return err;
			}

			if (xdfu_delta_mfw_datamode_context.len == 0) {
				LOG_ERR("Length cannot be 0");
				return -EINVAL;
			}

			/* Prepare per-chunk accounting for the datamode callback. */
			xdfu_current_image_type = DFU_TYPE_DELTA_MFW;
			xdfu_bytes_written = 0;
			xdfu_status = 0;

			err = enter_datamode(xdfu_datamode_callback,
						xdfu_delta_mfw_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to enter data write mode: %d", err);
				return err;
			}

			return 0;
		case DFU_TYPE_FULL_MFW:
			if (!IS_ENABLED(CONFIG_SM_DFU_MODEM_FULL)) {
				LOG_ERR("Full modem DFU is not enabled");
				return -EOPNOTSUPP;
			}

			/*
			 * POINT OF NO RETURN: After the first firmware segment write,
			 * the modem will be corrupted if the update is not completed.
			 * Bootloader segment writes can still be rolled back.
			 */
			if (param_count != 4) {
				LOG_ERR("Invalid number of parameters for data write");
				return -EINVAL;
			}

			err = at_parser_num_get(parser, 2, &xdfu_full_mfw_datamode_context.addr);
			if (err) {
				LOG_ERR("Failed to get address: %d", err);
				return err;
			}

			err = at_parser_num_get(parser, 3, &xdfu_full_mfw_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to get length: %d", err);
				return err;
			}

			if (xdfu_full_mfw_datamode_context.len == 0) {
				LOG_ERR("Length cannot be 0");
				return -EINVAL;
			}

			/* Prepare per-chunk accounting for the datamode callback. */
			xdfu_current_image_type = DFU_TYPE_FULL_MFW;
			xdfu_bytes_written = 0;
			xdfu_status = 0;

			err = enter_datamode(xdfu_datamode_callback,
						xdfu_full_mfw_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to enter data write mode: %d", err);
				return err;
			}

			return 0;
		default:
			LOG_ERR("Invalid target type: %d", type);
			return -EINVAL;
		}
	case AT_PARSER_CMD_TYPE_TEST:
#if defined(CONFIG_SM_DFU_MODEM_FULL)
		rsp_send("\r\n#XDFUWRITE: (%d,%d,%d),<addr>,<len>\r\n",
			DFU_TYPE_APP, DFU_TYPE_DELTA_MFW, DFU_TYPE_FULL_MFW);
#else
		rsp_send("\r\n#XDFUWRITE: (%d,%d),<addr>,<len>\r\n",
			DFU_TYPE_APP, DFU_TYPE_DELTA_MFW);
#endif
		return 0;
	default:
		LOG_ERR("Invalid command type: %d", cmd_type);
		return -EINVAL;
	}
}

SM_AT_CMD_CUSTOM(xdfu_apply, "AT#XDFUAPPLY", handle_at_xdfu_apply);
static int handle_at_xdfu_apply(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				  uint32_t param_count)
{
	ARG_UNUSED(param_count);

	int err;
	uint16_t type;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &type);
		if (err) {
			LOG_ERR("Failed to get type: %d", err);
			return err;
		}

		if (sm_bootloader_mode_enabled && type != DFU_TYPE_FULL_MFW) {
			LOG_ERR("DFU type %d is not supported in bootloader mode", type);
			return -EOPNOTSUPP;
		}

		switch (type) {
		case DFU_TYPE_APP:
			err = dfu_target_mcuboot_done(true);
			if (err) {
				LOG_ERR("App firmware update failed: %d", err);
			} else {
				err = dfu_target_mcuboot_schedule_update(0);
				if (err) {
					LOG_ERR("Failed to schedule app firmware update: %d", err);
				}
			}

			urc_send("#XDFU: %u,%u,%d\r\n",
				DFU_TYPE_APP, DFU_OPERATION_APPLY_UPDATE, err ? -1 : 0);

			LOG_INF("App firmware update scheduled");

			return 0;
		case DFU_TYPE_DELTA_MFW:
			err = dfu_target_modem_delta_done(true);
			if (err) {
				LOG_ERR("Delta modem firmware update failed: %d", err);
			} else {
				err = dfu_target_modem_delta_schedule_update(0);
				if (err) {
					LOG_ERR("Failed to schedule delta MFW update: %d", err);
				}
			}

			urc_send("#XDFU: %u,%u,%d\r\n",
				DFU_TYPE_DELTA_MFW, DFU_OPERATION_APPLY_UPDATE, err ? -1 : 0);

			LOG_INF("Delta modem firmware update scheduled");

			return 0;
		case DFU_TYPE_FULL_MFW:
			if (!IS_ENABLED(CONFIG_SM_DFU_MODEM_FULL)) {
				LOG_ERR("Full modem DFU is not enabled");
				return -EOPNOTSUPP;
			}

			err = nrf_modem_bootloader_update();
			if (err) {
				LOG_ERR("Failed to update bootloader: %d", err);
			} else {
				if (full_mfw_dfu_segment_type ==
					DFU_FULL_MFW_SEGMENT_BOOTLOADER) {
					LOG_INF("Bootloader segment update successful");
					LOG_WRN("After first FW write, modem will corrupt "
						"if update is not completed");
					(void)set_full_mfw_dfu_segment_type(
						DFU_FULL_MFW_SEGMENT_FIRMWARE);
				} else if (full_mfw_dfu_segment_type ==
					   DFU_FULL_MFW_SEGMENT_FIRMWARE) {
					(void)set_full_mfw_dfu_segment_type(
						DFU_FULL_MFW_SEGMENT_BOOTLOADER);
					LOG_INF("Firmware update successful, rebooting...");
					LOG_PANIC();
					sys_reboot(SYS_REBOOT_COLD);
				}
			}

			urc_send("#XDFU: %u,%u,%d\r\n",
				DFU_TYPE_FULL_MFW, DFU_OPERATION_APPLY_UPDATE, err ? -1 : 0);

			return 0;
		default:
			LOG_ERR("Invalid target type: %d", type);
			return -EINVAL;
		}
	case AT_PARSER_CMD_TYPE_TEST:
#if defined(CONFIG_SM_DFU_MODEM_FULL)
		rsp_send("\r\n#XDFUAPPLY: (%d,%d,%d)\r\n",
			DFU_TYPE_APP, DFU_TYPE_DELTA_MFW, DFU_TYPE_FULL_MFW);
#else
		rsp_send("\r\n#XDFUAPPLY: (%d,%d)\r\n",
			DFU_TYPE_APP, DFU_TYPE_DELTA_MFW);
#endif
		return 0;
	default:
		LOG_ERR("Invalid command type: %d", cmd_type);
		return -EINVAL;
	}
}

int sm_at_handle_xdfu_init(char *buf, size_t len, char *at_cmd)
{
	return sm_at_cb_wrapper(buf, len, at_cmd, handle_at_xdfu_init);
}

int sm_at_handle_xdfu_write(char *buf, size_t len, char *at_cmd)
{
	return sm_at_cb_wrapper(buf, len, at_cmd, handle_at_xdfu_write);
}

int sm_at_handle_xdfu_apply(char *buf, size_t len, char *at_cmd)
{
	return sm_at_cb_wrapper(buf, len, at_cmd, handle_at_xdfu_apply);
}
