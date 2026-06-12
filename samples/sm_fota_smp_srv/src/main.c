/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>

LOG_MODULE_REGISTER(sm_fota_smp_srv, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * AT-host link: uart20 (P1.04-07) is wired to the nRF9151's serial_modem AT
 * interface (its uart0). The L15 acts as the AT host that drives the modem.
 * serial_modem uses CR ('\r') command termination by default.
 */
static const struct device *const at_uart = DEVICE_DT_GET(DT_NODELABEL(uart20));

static void at_host_send(const char *cmd)
{
	LOG_INF("AT-host TX -> nRF9151: \"%s\"", cmd);
	for (const char *p = cmd; *p != '\0'; p++) {
		uart_poll_out(at_uart, (unsigned char)*p);
	}
	uart_poll_out(at_uart, '\r');
}

static void at_host_read_response(uint32_t window_ms)
{
	uint8_t buf[256];
	size_t len = 0U;
	int64_t deadline = k_uptime_get() + window_ms;
	unsigned char c;

	while (k_uptime_get() < deadline && len < (sizeof(buf) - 1U)) {
		if (uart_poll_in(at_uart, &c) == 0) {
			buf[len++] = c;
		} else {
			k_sleep(K_MSEC(2));
		}
	}
	buf[len] = '\0';

	if (len == 0U) {
		LOG_WRN("AT-host RX <- nRF9151: nothing within %u ms", window_ms);
		return;
	}

	LOG_INF("AT-host RX <- nRF9151: %u bytes", (unsigned int)len);
	LOG_HEXDUMP_INF(buf, len, "raw response");
	if (strstr((char *)buf, "OK") != NULL) {
		LOG_INF("AT-host: got OK from nRF9151 -- L15->9151 AT link works!");
	} else {
		LOG_WRN("AT-host: response received but no \"OK\" found");
	}
}

static void at_host_test(void)
{
	if (!device_is_ready(at_uart)) {
		LOG_ERR("AT-host UART (uart20) not ready");
		return;
	}

	/* Let the nRF9151 serial_modem finish bringing up its AT host. */
	k_sleep(K_SECONDS(3));
	at_host_send("AT");
	at_host_read_response(2000U);
}

/*
 * The actual OS-group echo is serviced by the built-in mcumgr handler
 * (CONFIG_MCUMGR_GRP_OS_ECHO), which is byte-for-byte compatible with the
 * nRF9151 client's os_mgmt_client_echo(). We only hook the generic
 * "command received" notification so the server can LOG on its own console
 * that the round-trip reached it -- that on-server log is the proof the
 * client's echo physically arrived and was dispatched.
 */
static enum mgmt_cb_return smp_cmd_recv_cb(uint32_t event, enum mgmt_cb_return prev_status,
					   int32_t *rc, uint16_t *group, bool *abort_more,
					   void *data, size_t data_size)
{
	if (event == MGMT_EVT_OP_CMD_RECV && data != NULL &&
	    data_size >= sizeof(struct mgmt_evt_op_cmd_arg)) {
		const struct mgmt_evt_op_cmd_arg *arg = data;

		if (arg->group == MGMT_GROUP_ID_OS && arg->id == OS_MGMT_ID_ECHO) {
			LOG_INF("SMP echo received from client (OS group, echo command)");
		} else {
			LOG_INF("SMP command received: group=%u id=%u", arg->group, arg->id);
		}
	}

	return MGMT_CB_OK;
}

static struct mgmt_callback smp_cmd_recv_callback = {
	.callback = smp_cmd_recv_cb,
	.event_id = MGMT_EVT_OP_CMD_RECV,
};

int main(void)
{
	mgmt_callback_register(&smp_cmd_recv_callback);

	LOG_INF("SMP server ready");

	at_host_test();

	while (true) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
