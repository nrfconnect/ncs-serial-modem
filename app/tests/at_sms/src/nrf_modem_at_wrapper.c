/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file nrf_modem_at_wrapper.c
 * Wrapper for nrf_modem_at_cmd to intercept AT commands in tests
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <nrf_modem_at.h>
#include <nrf_errno.h>
#include <modem/at_cmd_custom.h>

/* External wrapper function declared by SM_AT_CMD_CUSTOM macro in sm_at_sms.c */
extern int handle_at_sms_wrapper_xsms(char *buf, size_t len, char *at_cmd);

int nrf_modem_at_cmd(void *buf, size_t buf_size, const char *fmt, ...)
{
	char at_cmd[256];
	va_list args;
	int ret;

	va_start(args, fmt);
	vsnprintf(at_cmd, sizeof(at_cmd), fmt, args);
	va_end(args);

	/* Route to appropriate handler based on command */
	if (strncasecmp(at_cmd, "AT#XSMS", 7) == 0) {
		ret = handle_at_sms_wrapper_xsms((char *)buf, buf_size, at_cmd);
	} else {
		/* Unknown command - return error */
		ret = -NRF_EINVAL;
	}

	return ret;
}
