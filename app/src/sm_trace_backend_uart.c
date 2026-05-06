/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Custom modem trace backend that shares the UART log backend UART (zephyr,console).
 *
 * The log backend uses uart_poll_out (polling, no async callback).
 * This backend uses uart_tx (async DMA).  The two are mutually exclusive in
 * time: AT#XLOG and AT#XTRACE each refuse to enable if the other is active.
 *
 * The UART is suspended when backends are off and resumed when either
 * is enabled.  No baud-rate switching is performed; both sides run at the
 * baud rate configured in the devicetree (1 000 000 baud on the nRF9151 DK).
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <modem/trace_backend.h>

#include <modem/nrf_modem_lib.h>
#include <modem/nrf_modem_lib_trace.h>

#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_trace_backend_uart, CONFIG_MODEM_TRACE_BACKEND_LOG_LEVEL);

/* Maximum DMA transfer length (nRF91 UARTE). */
#define CHUNK_SZ 8191

/* Timeout per individual UART TX attempt. */
#define UART_TX_WAIT_TIME_MS 1000

/* Zephyr console UART is used both for application logs and modem traces. */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* Synchronizes trace_backend_write() with activate/deactivate. */
static K_SEM_DEFINE(tx_sem, 0, 1);

/* Signaled by the UART callback on TX_DONE / TX_ABORTED. */
static K_SEM_DEFINE(tx_done_sem, 0, 1);

/* Actual bytes transferred in the last async tx, set by the callback. */
static volatile size_t tx_bytes;

static trace_backend_processed_cb trace_processed_callback;

static bool trace_active;

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	switch (evt->type) {
	case UART_TX_ABORTED:
		LOG_WRN_RATELIMIT("UART_TX_ABORTED: %zu bytes", evt->data.tx.len);
		/* fallthrough */
	case UART_TX_DONE:
		tx_bytes = evt->data.tx.len;
		k_sem_give(&tx_done_sem);
		break;
	default:
		break;
	}
}

static int trace_backend_init(trace_backend_processed_cb trace_processed_cb)
{
	if (trace_processed_cb == NULL) {
		return -EFAULT;
	}

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	trace_processed_callback = trace_processed_cb;

	return 0;
}

static int trace_backend_deinit(void)
{
	return 0;
}

static int trace_backend_write(const void *data, size_t len)
{
	int ret;
	size_t chunk = MIN(len, CHUNK_SZ);

	if (!trace_active) {
		LOG_DBG_RATELIMIT("Inactive, dropped %u bytes.", len);
		trace_processed_callback(len);
		return len;
	}

	k_sem_take(&tx_sem, K_FOREVER);

	ret = uart_tx(uart_dev, (const uint8_t *)data, chunk,
		      UART_TX_WAIT_TIME_MS * USEC_PER_MSEC);
	if (ret) {
		LOG_ERR("uart_tx failed: %d", ret);
		goto out;
	}

	/* Wait for the UART TX to complete. */
	k_sem_take(&tx_done_sem, K_FOREVER);
	if (tx_bytes == 0) {
		ret = -EAGAIN;
		goto out;
	}

	ret = trace_processed_callback(tx_bytes);
	if (ret) {
		goto out;
	}

	ret = tx_bytes;

out:
	k_sem_give(&tx_sem);

	return ret;
}

struct nrf_modem_lib_trace_backend trace_backend = {
	.init = trace_backend_init,
	.deinit = trace_backend_deinit,
	.write = trace_backend_write,
};

static int trace_backend_activate(void)
{
	int ret;

	/* Register the async callback. The log backend never calls
	 * uart_callback_set (it uses polling), so this is the sole owner.
	 */
	ret = uart_callback_set(uart_dev, uart_callback, NULL);
	if (ret) {
		LOG_ERR("Failed to set UART callback: %d", ret);
		return ret;
	}

	/* Release the tx_sem so trace_backend_write() can proceed. */
	k_sem_give(&tx_sem);

	/* Tell the modem to begin generating traces. */
	ret = nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_FULL);
	if (ret) {
		LOG_ERR("Failed to set modem trace level: %d", ret);
	}

	return ret;
}

static int trace_backend_deactivate(void)
{
	int ret;

	/* Stop the modem from generating new trace data. */
	ret = nrf_modem_lib_trace_level_set(NRF_MODEM_LIB_TRACE_LEVEL_OFF);
	if (ret) {
		LOG_ERR("Failed to set modem trace level: %d", ret);
	}

	/* Wait for any in-flight write to release tx_sem. */
	if (k_sem_take(&tx_sem, K_MSEC(UART_TX_WAIT_TIME_MS)) != 0) {
		uart_tx_abort(uart_dev);
		k_sem_take(&tx_sem, K_FOREVER);
	}

	/* Reset semaphores so the next activate() starts clean. */
	k_sem_reset(&tx_sem);
	k_sem_reset(&tx_done_sem);

	return ret;
}

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

SM_AT_CMD_CUSTOM(xtrace, "AT#XTRACE", handle_at_trace);
STATIC int handle_at_trace(enum at_parser_cmd_type cmd_type, struct at_parser *parser, uint32_t)
{
	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		int mode;
		int ret = at_parser_num_get(parser, 1, &mode);

		if (ret || (mode < 0) || (mode > 1)) {
			return -EINVAL;
		}

		if (mode == (int)trace_active) {
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
			ret = trace_backend_activate();
			if (ret) {
				return ret;
			}
			trace_active = true;
		} else {
			ret = trace_backend_deactivate();
			if (ret) {
				return ret;
			}
			ret = uart_suspend();
			if (ret) {
				return ret;
			}
			trace_active = false;
		}
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		rsp_send("\r\n#XTRACE: %d\r\n", (int)trace_active);
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XTRACE: (0,1)\r\n");
		return 0;
	}

	return -EINVAL;
}

static int sm_trace_backend_uart_init(void)
{
	/* Allow UART to be active if CONFIG_LOG_BACKEND_UART_AUTOSTART=y */
	if (!IS_ENABLED(CONFIG_LOG_BACKEND_UART_AUTOSTART)) {
		/* Start the trace backend disabled. */
		if (uart_suspend()) {
			LOG_ERR("Failed to suspend UART trace backend");
			sm_init_failed = true;
			return -EFAULT;
		}
	}

	return 0;
}

SYS_INIT(sm_trace_backend_uart_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
