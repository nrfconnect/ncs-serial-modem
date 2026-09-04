/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sm_log.h"

void sm_log_flush(void)
{
	/* Stub - no-op for tests */
}

int sm_log_level(void)
{
	/* Stub - tests don't exercise redaction; report everything as shown. */
	return SM_LOG_ON_FULL;
}

const char *sm_log_cmd_sensitive_prefix(const char *cmd, size_t len)
{
	(void)cmd;
	(void)len;
	return NULL;
}

void sm_log_rx_command(const uint8_t *buf, size_t len)
{
	(void)buf;
	(void)len;
}

void sm_log_urc(const char *dest, const void *pipe, const uint8_t *data, size_t len)
{
	(void)dest;
	(void)pipe;
	(void)data;
	(void)len;
}
