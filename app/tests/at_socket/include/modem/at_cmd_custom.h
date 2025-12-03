/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file at_cmd_custom.h
 * Test-specific override of AT_CMD_CUSTOM to make functions non-static when testing
 */

#ifndef AT_CMD_CUSTOM_H_
#define AT_CMD_CUSTOM_H_

#include <modem/at_cmd_custom.h>
#include <stddef.h>
#include <stdarg.h>

#undef AT_CMD_CUSTOM

/* Forward declaration for at_cmd_custom_respond stub function */
int at_cmd_custom_respond(char *buf, size_t buf_size, const char *response, ...);

/* Redefine AT_CMD_CUSTOM to generate non-static forward declarations when testing */
#define AT_CMD_CUSTOM(entry, _filter, _callback)                           \
	int _callback(char *buf, size_t len, char *at_cmd);                    \
	STRUCT_SECTION_ITERABLE(nrf_modem_at_cmd_custom,                       \
				entry) = {.cmd = _filter,                                  \
					  .callback = _callback,                               \
					  .cmd_strlen = sizeof(_filter) - sizeof(char)}

#endif /* AT_CMD_CUSTOM_H_ */
