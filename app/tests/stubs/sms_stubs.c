/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file sms_stubs.c
 * Minimal stub implementation of the nRF Connect SDK SMS subscriber manager
 * (modem/sms.h) API, for unit-testing sm_at_sms.c without the modem.
 */

#include <errno.h>
#include <stddef.h>
#include <zephyr/sys/util.h>
#include <modem/sms.h>

static sms_callback_t registered_listener;
static void *registered_context;
static int next_handle = 1;

int sms_register_listener(sms_callback_t listener, void *context)
{
	if (listener == NULL) {
		return -EINVAL;
	}

	registered_listener = listener;
	registered_context = context;

	return next_handle++;
}

void sms_unregister_listener(int handle)
{
	ARG_UNUSED(handle);

	registered_listener = NULL;
	registered_context = NULL;
}

int sms_send_text(const char *number, const char *text)
{
	ARG_UNUSED(number);
	ARG_UNUSED(text);

	return 0;
}

int sms_send(const char *number, const uint8_t *data, uint16_t data_len, enum sms_data_type type)
{
	ARG_UNUSED(number);
	ARG_UNUSED(data);
	ARG_UNUSED(data_len);
	ARG_UNUSED(type);

	return 0;
}

/* Test helper: directly invoke the registered listener, as the real SMS
 * subscriber module would do when a message arrives from the modem.
 */
void sms_stub_deliver(struct sms_data *data)
{
	if (registered_listener != NULL) {
		registered_listener(data, registered_context);
	}
}
