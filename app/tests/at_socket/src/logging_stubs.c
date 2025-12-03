/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdarg.h>
#include <stddef.h>

/* Logging stubs - disable logging in tests */

void z_log_msg_runtime_create(void *src_level, void *timestamp, void *data_ptr, size_t data_len,
			      const char *fmt, ...)
{
	/* No-op for tests */
}

void z_log_msg_static_create(void *src_level, const void *data, size_t data_len)
{
	/* No-op for tests */
}
