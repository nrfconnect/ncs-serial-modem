/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file mqtt_stubs.c
 * Manual stubs for MQTT library functions that must not be mocked.
 *
 * mqtt_client_init() is invoked from the sm_at_mqtt_init() SYS_INIT hook, which
 * runs before CMock is initialised in setUp(). Calling a CMock mock at that
 * point would fail, so it is excluded from mocking (see CMakeLists.txt) and
 * stubbed here as a no-op.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>

void mqtt_client_init(struct mqtt_client *client)
{
	ARG_UNUSED(client);
}

/* sm_at_host.c wires these socket poll-work handlers into every AT host ctx.
 * They live in sm_at_socket.c, which this test does not compile, so stub them.
 */
void sm_at_socket_poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
}

void sm_at_socket_poll_idle_handler(struct k_work *work)
{
	ARG_UNUSED(work);
}
