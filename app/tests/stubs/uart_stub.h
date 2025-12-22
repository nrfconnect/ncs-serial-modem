/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

void uart_stub_rx(const uint8_t *data, size_t len);

static inline void send_at_command(const char *cmd)
{
	uart_stub_rx((const uint8_t *)cmd, strlen(cmd));
}
