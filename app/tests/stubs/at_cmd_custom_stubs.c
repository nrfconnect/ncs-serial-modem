/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file at_cmd_custom_stubs.c
 * Stub implementation for at_cmd_custom_respond function
 */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

/* Stub for at_cmd_custom_respond - formats response into buffer */
int at_cmd_custom_respond(char *buf, size_t buf_size, const char *response, ...)
{
	va_list args;
	int ret;

	va_start(args, response);
	ret = vsnprintf(buf, buf_size, response, args);
	va_end(args);

	return (ret < 0 || ret >= (int)buf_size) ? -ENOMEM : 0;
}
