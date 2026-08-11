/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file cmux.h
 *
 * Minimal test replacement for <zephyr/modem/cmux.h>.
 *
 * The real header drags in the whole Zephyr modem CMUX subsystem, which needs
 * a serial backend and a CMUX peer to complete the multiplexer handshake. The
 * unit tests only care about which DLCI pipe sm_cmux.c hands out to which
 * consumer, so the CMUX framing itself is replaced by cmux_stubs.c.
 */

#ifndef ZEPHYR_MODEM_CMUX_
#define ZEPHYR_MODEM_CMUX_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/modem/pipe.h>

#define MODEM_CMUX_WORK_BUFFER_SIZE 128

struct modem_cmux;

enum modem_cmux_event {
	MODEM_CMUX_EVENT_CONNECTED = 0,
	MODEM_CMUX_EVENT_DISCONNECTED,
};

typedef void (*modem_cmux_callback)(struct modem_cmux *cmux, enum modem_cmux_event event,
				    void *user_data);

struct modem_cmux_config {
	modem_cmux_callback callback;
	void *user_data;
	uint8_t *receive_buf;
	uint16_t receive_buf_size;
	uint8_t *transmit_buf;
	uint16_t transmit_buf_size;
};

struct modem_cmux {
	modem_cmux_callback callback;
	void *user_data;
	struct modem_pipe *pipe;
	bool attached;
};

struct modem_cmux_dlci_config {
	uint8_t dlci_address;
	uint8_t *receive_buf;
	uint16_t receive_buf_size;
};

struct modem_cmux_dlci {
	struct modem_cmux *cmux;
	uint8_t dlci_address;
};

void modem_cmux_init(struct modem_cmux *cmux, const struct modem_cmux_config *config);

struct modem_pipe *modem_cmux_dlci_init(struct modem_cmux *cmux, struct modem_cmux_dlci *dlci,
					const struct modem_cmux_dlci_config *config);

int modem_cmux_attach(struct modem_cmux *cmux, struct modem_pipe *pipe);

void modem_cmux_release(struct modem_cmux *cmux);

#endif /* ZEPHYR_MODEM_CMUX_ */
