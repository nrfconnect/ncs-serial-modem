/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <sm_at_client.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

SM_MONITOR(network, "\r\n+CEREG:", cereg_mon);

static void cereg_mon(const char *notif)
{
	int status = atoi(notif + strlen("\r\n+CEREG: "));

	if (status == 1 || status == 5) {
		LOG_INF("LTE connected");
	}
}

void sm_at_client_shell_data_indication(const uint8_t *data, size_t datalen)
{
	ARG_UNUSED(data);
	ARG_UNUSED(datalen);

	/* Add data handler implementation in here. */
}

void sm_at_client_shell_ri_handler(void)
{
	LOG_INF("Ring Indicate (RI) triggered");
}

int main(void)
{
	int err;

	LOG_INF("Serial Modem AT Client Shell starts on %s", CONFIG_BOARD);

	err = sm_at_client_init(sm_at_client_shell_data_indication, true, K_MSEC(100));
	if (err) {
		LOG_ERR("Failed to initialize Serial Modem: %d", err);
	}

	err = sm_at_client_register_ri_handler(sm_at_client_shell_ri_handler);
	if (err) {
		LOG_ERR("Failed to register RI handler (%d).", err);
	}

	return 0;
}
