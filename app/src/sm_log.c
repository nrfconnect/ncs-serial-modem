/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * AT#XLOG command — enable/disable the Zephyr UART log backend at runtime.
 *
 * The UART (zephyr,console) is shared with the modem-trace backend
 * (sm_trace_backend_uart.c).  AT#XLOG and AT#XTRACE are mutually exclusive:
 * each refuses to activate while the other is in use.
 *
 */

#include <zephyr/devicetree.h>

/* Only compiled if zephyr,console is present and active in the devicetree. */
#if DT_HAS_CHOSEN(zephyr_console) && DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/pm/device.h>

#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_log, CONFIG_SM_LOG_LEVEL);

/* Zephyr console UART is used both for application logs and modem traces. */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static bool log_active;

static int uart_suspend(void)
{
	int ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);

	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to %s UART device: %d", "suspend", ret);
		return ret;
	}

	return 0;
}

static int uart_resume(void)
{
	int ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);

	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to %s UART device: %d", "resume", ret);
		return ret;
	}
	return 0;
}

static bool uart_is_active(void)
{
	enum pm_device_state state = PM_DEVICE_STATE_OFF;
	int err = pm_device_state_get(uart_dev, &state);

	if (err) {
		LOG_ERR("Failed to get UART device state (%d).", err);
		return false;
	}
	return state == PM_DEVICE_STATE_ACTIVE;
}

void sm_log_flush(void)
{
	const struct log_backend *log_be = log_backend_get_by_name("log_backend_uart");

	if (log_be && log_be->cb && log_be->cb->initialized) {
		log_flush();
	}
}

SM_AT_CMD_CUSTOM(xlog, "AT#XLOG", handle_at_log);
STATIC int handle_at_log(enum at_parser_cmd_type cmd_type, struct at_parser *parser, uint32_t)
{
	const struct log_backend *log_be = log_backend_get_by_name("log_backend_uart");

	if (!log_be) {
		return -ENODEV;
	}

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		int mode;
		int ret = at_parser_num_get(parser, 1, &mode);

		if (ret || (mode < 0) || (mode > 1)) {
			return -EINVAL;
		}

		if (mode == (int)log_active) {
			return 0;
		}

		if (mode == 1 && uart_is_active()) {
			return -EBUSY;
		}

		if (mode == 1) {
			ret = uart_resume();
			if (ret) {
				return ret;
			}
			if (!log_be->cb->initialized) {
				log_backend_init(log_be);
			}
			log_backend_enable(log_be, log_be->cb->ctx, CONFIG_LOG_DEFAULT_LEVEL);
			log_active = true;
		} else {
			log_backend_disable(log_be);
			ret = uart_suspend();
			if (ret) {
				return ret;
			}
			log_active = false;
		}
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		rsp_send("\r\n#XLOG: %d\r\n", (int)log_active);
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XLOG: (0,1)\r\n");
		return 0;
	}

	return -EINVAL;
}

static int sm_log_init(void)
{
	if (!IS_ENABLED(CONFIG_LOG_BACKEND_UART) ||
	    !IS_ENABLED(CONFIG_LOG_BACKEND_UART_AUTOSTART)) {
		/* Start with UART log backend disabled. */
		if (uart_suspend()) {
			LOG_ERR("Failed to suspend UART log backend");
			sm_init_failed = true;
			return -EFAULT;
		}
	} else {
		log_active = true;
	}

	return 0;
}

SYS_INIT(sm_log_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* DT_HAS_CHOSEN(zephyr_console) */
