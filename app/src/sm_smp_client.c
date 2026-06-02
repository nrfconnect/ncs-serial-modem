/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp_client.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt_client.h>

LOG_MODULE_REGISTER(sm_smp_client, CONFIG_SM_LOG_LEVEL);

static const char smp_echo_str[] = "SMP echo";

static struct smp_client_object smp_client;
static struct os_mgmt_client os_client;

static int sm_smp_client_echo(void)
{
	int ret;

	for (int attempt = 1; attempt <= 3; attempt++) {
		ret = os_mgmt_client_echo(&os_client, smp_echo_str, sizeof(smp_echo_str));
		if (ret == MGMT_ERR_EOK) {
			LOG_INF("SMP echo round-trip ok (attempt %d)", attempt);
			return 0;
		}

		LOG_WRN("SMP echo attempt %d failed: %d", attempt, ret);
		k_msleep(500);
	}

	LOG_ERR("SMP echo failed after retries");
	return -EIO;
}

static int sm_smp_client_init(void)
{
	int ret;

	ret = smp_client_object_init(&smp_client, SMP_SERIAL_TRANSPORT);
	if (ret) {
		LOG_ERR("SMP client init failed: %d", ret);
		return ret;
	}

	os_mgmt_client_init(&os_client, &smp_client);

	LOG_INF("SMP echo request: %s", smp_echo_str);
	return sm_smp_client_echo();
}

SYS_INIT(sm_smp_client_init, APPLICATION, 95);
