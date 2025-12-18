/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include "sm_util.h"

LOG_MODULE_REGISTER(buttons, LOG_LEVEL_DBG);

static void button_handler(struct input_event *evt, void *user_data)
{
	if (evt->type != INPUT_EV_KEY || evt->value != 1) {
		return;
	}

	int ret;
	const char *at_cmd;

	switch (evt->code) {
	case INPUT_KEY_0:
		at_cmd = CONFIG_SM_BUTTON0_AT;
		break;
	case INPUT_KEY_1:
		at_cmd = CONFIG_SM_BUTTON1_AT;
		break;
	case INPUT_KEY_2:
		at_cmd = CONFIG_SM_BUTTON2_AT;
		break;
	case INPUT_KEY_3:
		at_cmd = CONFIG_SM_BUTTON3_AT;
		break;
	default:
		return;
	}

	ret = sm_util_at_printf(at_cmd);
	LOG_DBG("Sent AT command \"%s\" from button %d, ret=%d", at_cmd, evt->code, ret);
}

INPUT_CALLBACK_DEFINE(NULL, button_handler, NULL);
