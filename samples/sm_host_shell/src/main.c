/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <sm_host.h>
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

void sm_host_shell_data_indication(const uint8_t *data, size_t datalen)
{
	LOG_INF("Data received (len=%d): %.*s", datalen, datalen, (const char *)data);
}

#if (CONFIG_SM_HOST_INDICATE_PIN >= 0)
void sm_host_shell_indication_handler(void)
{
	int err;

	LOG_INF("Serial Modem indicate pin triggered");
	err = sm_host_power_pin_toggle();
	if (err) {
		LOG_ERR("Failed to toggle power pin");
	}
}
#endif /* CONFIG_SM_HOST_INDICATE_PIN */

int main(void)
{
	int err;

	LOG_INF("Serial Modem Host Shell starts on %s", CONFIG_BOARD);

	err = sm_host_init(sm_host_shell_data_indication);
	if (err) {
		LOG_ERR("Failed to initialize Serial Modem: %d", err);
	}

#if (CONFIG_SM_HOST_INDICATE_PIN >= 0)
	err = sm_host_register_ind(sm_host_shell_indication_handler, true);
	if (err) {
		LOG_ERR("Failed to register indication: %d", err);
	}
#endif /* CONFIG_SM_HOST_INDICATE_PIN */

	return 0;
}
