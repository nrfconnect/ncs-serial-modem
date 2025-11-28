/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <modem/nrf_modem_lib.h>
#include <nrf_modem_bootloader.h>
#include "sm_at_host.h"
#include "sm_at_dfu.h"
#include "sm_settings.h"
#include "sm_uart_handler.h"

LOG_MODULE_REGISTER(sm_dfu, CONFIG_SM_LOG_LEVEL);

bool sm_bootloader_mode_requested;
bool sm_bootloader_mode_enabled;

enum xdfu_operation {
	DFU_BOOTLOADER_MODE_REQUEST = 0,
	DFU_DATA_WRITE = 1,
	DFU_BOOTLOADER_UPDATE = 2
};

enum xdfu_segment_type {
	DFU_SEGMENT_BOOTLOADER = 0,
	DFU_SEGMENT_FIRMWARE = 1,
};

struct xdfu_datamode_context {
	uint16_t type;
	uint32_t addr;
	uint32_t len;
};

static struct xdfu_datamode_context xdfu_datamode_context;
static uint32_t xdfu_bytes_written;
static int xdfu_status;

int request_bootloader_mode(bool enable)
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

static int xdfu_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int err;

	switch (op) {
	case DATAMODE_SEND:
		if (data == NULL || len <= 0) {
			LOG_ERR("Chunk data invalid (data=%p len=%d)", (void *)data, len);
			xdfu_status = -EINVAL;
			return -EINVAL;
		}

		if (xdfu_datamode_context.type == DFU_SEGMENT_BOOTLOADER) {
			err = nrf_modem_bootloader_bl_write((void *)data, len);
			if (err) {
				LOG_ERR("Failed to write bootloader segment: %d", err);
				xdfu_status = err;
				return 0;
			}
		} else if (xdfu_datamode_context.type == DFU_SEGMENT_FIRMWARE) {
			err = nrf_modem_bootloader_fw_write(xdfu_datamode_context.addr,
							    (void *)data, len);
			if (err) {
				LOG_ERR("Failed to write firmware segment: %d", err);
				xdfu_status = err;
				return 0;
			}
		} else {
			LOG_ERR("Invalid segment type: %d", xdfu_datamode_context.type);
			xdfu_status = -EINVAL;
			return -EINVAL;
		}

		xdfu_bytes_written += len;

		return 0;

	case DATAMODE_EXIT: {
		int status = xdfu_status;

		if (status == 0 && xdfu_bytes_written != xdfu_datamode_context.len) {
			LOG_WRN("Wrote %u bytes, expected %u", xdfu_bytes_written,
				xdfu_datamode_context.len);
			status = -EIO;
		}

		urc_send("#XDFU:1,%u,%u,%u,%d\r\n", xdfu_datamode_context.type,
			 xdfu_datamode_context.addr, xdfu_datamode_context.len, status);

		/* Reset for next chunk. */
		xdfu_bytes_written = 0;
		xdfu_status = 0;
		return 0;
	}

	default:
		LOG_WRN("Unexpected datamode op: %u (flags=0x%02x)", op, flags);
		return 0;
	}
}

SM_AT_CMD_CUSTOM(xdfu, "AT#XDFU", handle_at_xdfu);
static int handle_at_xdfu(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	int err;
	uint16_t op;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err < 0) {
			return err;
		}
		switch (op) {
		case DFU_BOOTLOADER_MODE_REQUEST:
			err = request_bootloader_mode(true);
			if (err) {
				LOG_ERR("Failed to enable bootloader mode: %d", err);
				return err;
			}

			LOG_PANIC();
			sys_reboot(SYS_REBOOT_COLD);
		case DFU_DATA_WRITE:
			if (param_count != 5) {
				LOG_ERR("Invalid number of parameters for data write");
				return -EINVAL;
			}
			err = at_parser_num_get(parser, 2, &xdfu_datamode_context.type);
			if (err) {
				LOG_ERR("Failed to get type: %d", err);
				return err;
			}

			err = at_parser_num_get(parser, 3, &xdfu_datamode_context.addr);
			if (err) {
				LOG_ERR("Failed to get address: %d", err);
				return err;
			}

			err = at_parser_num_get(parser, 4, &xdfu_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to get length: %d", err);
				return err;
			}

			if (xdfu_datamode_context.len == 0) {
				LOG_ERR("Length cannot be 0");
				return -EINVAL;
			}

			if (xdfu_datamode_context.type != DFU_SEGMENT_BOOTLOADER &&
			    xdfu_datamode_context.type != DFU_SEGMENT_FIRMWARE) {
				LOG_ERR("Invalid segment type: %d", xdfu_datamode_context.type);
				return -EINVAL;
			}

			/* Prepare per-chunk accounting for the datamode callback. */
			xdfu_bytes_written = 0;
			xdfu_status = 0;

			err = enter_datamode(xdfu_datamode_callback,
					    xdfu_datamode_context.len);
			if (err) {
				LOG_ERR("Failed to enter data write mode: %d", err);
				return err;
			}

			return 0;

		case DFU_BOOTLOADER_UPDATE:
			uint16_t type;
			int status = 0;

			if (param_count < 3) {
				LOG_ERR("Invalid number of parameters for bootloader update");
				return -EINVAL;
			}
			err = at_parser_num_get(parser, 2, &type);
			if (err) {
				LOG_ERR("Failed to get type: %d", err);
				return -EINVAL;
			}

			if (type != DFU_SEGMENT_BOOTLOADER && type != DFU_SEGMENT_FIRMWARE) {
				LOG_ERR("Invalid segment type: %d", type);
				return -EINVAL;
			}

			err = nrf_modem_bootloader_update();
			if (err) {
				LOG_ERR("Failed to update bootloader: %d", err);
				status = err;
			}

			urc_send("#XDFU:2,%u,%d\r\n", type, status);

			if (status == 0 && type == DFU_SEGMENT_FIRMWARE) {
				LOG_INF("Firmware update successful, rebooting...");
				LOG_PANIC();
				sys_reboot(SYS_REBOOT_COLD);
			}

			return 0;

		default:
			LOG_ERR("Invalid operation: %d", op);
			return -EINVAL;
		}

	default:
		LOG_ERR("Invalid command type: %d", cmd_type);
		return -EINVAL;
	}

	return 0;
}

int sm_at_dfu_handle_xdfu(char *buf, size_t len, char *at_cmd)
{
	return sm_at_cb_wrapper(buf, len, at_cmd, handle_at_xdfu);
}
