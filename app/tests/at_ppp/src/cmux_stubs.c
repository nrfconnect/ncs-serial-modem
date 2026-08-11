/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file cmux_stubs.c
 *
 * CMUX multiplexer and UART backend stubs used by the sm_cmux.c/sm_ppp.c unit
 * tests.
 *
 * The real Zephyr CMUX subsystem is replaced by a bookkeeping-only
 * implementation: modem_cmux_dlci_init() hands out one distinct fake pipe per
 * DLCI address so that the tests can assert exactly which channel each
 * consumer (AT host, PPP, trace backend) ends up with.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/modem/cmux.h>

#include "ppp_stubs.h"

/* Fake pipes. Only their addresses are ever used. */
static struct modem_pipe *const uart_pipe = (struct modem_pipe *)0x1000;
static struct modem_pipe *const dlci_pipes[PPP_STUB_MAX_DLCI] = {
	(struct modem_pipe *)0x2001,
	(struct modem_pipe *)0x2002,
	(struct modem_pipe *)0x2003,
	(struct modem_pipe *)0x2004,
};

static struct modem_cmux *attached_cmux;

int ppp_stub_cmux_attach_ret;
unsigned int ppp_stub_cmux_attach_calls;
unsigned int ppp_stub_cmux_release_calls;

struct modem_pipe *ppp_stub_uart_pipe(void)
{
	return uart_pipe;
}

struct modem_pipe *ppp_stub_dlci_pipe(uint8_t address)
{
	if (address < 1 || address > PPP_STUB_MAX_DLCI) {
		return NULL;
	}
	return dlci_pipes[address - 1];
}

const char *ppp_stub_pipe_name(struct modem_pipe *pipe)
{
	if (pipe == NULL) {
		return "none";
	}
	if (pipe == uart_pipe) {
		return "uart";
	}
	for (uint8_t i = 0; i < PPP_STUB_MAX_DLCI; i++) {
		if (pipe == dlci_pipes[i]) {
			static char name[8];

			(void)snprintk(name, sizeof(name), "dlci%u", i + 1);
			return name;
		}
	}
	return "unknown";
}

void ppp_stub_cmux_reset(void)
{
	attached_cmux = NULL;
	ppp_stub_cmux_attach_ret = 0;
	ppp_stub_cmux_attach_calls = 0;
	ppp_stub_cmux_release_calls = 0;
}

void ppp_stub_cmux_signal_disconnect(void)
{
	if (attached_cmux && attached_cmux->callback) {
		attached_cmux->callback(attached_cmux, MODEM_CMUX_EVENT_DISCONNECTED,
					attached_cmux->user_data);
	}
}

/* --- Zephyr modem CMUX API --------------------------------------------- */

void modem_cmux_init(struct modem_cmux *cmux, const struct modem_cmux_config *config)
{
	memset(cmux, 0, sizeof(*cmux));
	cmux->callback = config->callback;
	cmux->user_data = config->user_data;
}

struct modem_pipe *modem_cmux_dlci_init(struct modem_cmux *cmux, struct modem_cmux_dlci *dlci,
					const struct modem_cmux_dlci_config *config)
{
	dlci->cmux = cmux;
	dlci->dlci_address = config->dlci_address;

	return ppp_stub_dlci_pipe(config->dlci_address);
}

int modem_cmux_attach(struct modem_cmux *cmux, struct modem_pipe *pipe)
{
	ppp_stub_cmux_attach_calls++;

	if (ppp_stub_cmux_attach_ret) {
		return ppp_stub_cmux_attach_ret;
	}

	cmux->pipe = pipe;
	cmux->attached = true;
	attached_cmux = cmux;

	return 0;
}

void modem_cmux_release(struct modem_cmux *cmux)
{
	ppp_stub_cmux_release_calls++;
	cmux->pipe = NULL;
	cmux->attached = false;
	attached_cmux = NULL;
}

/* --- Serial Modem UART backend ------------------------------------------ */

struct modem_pipe *sm_uart_pipe_get(void)
{
	return uart_pipe;
}

/* --- Modem trace backend ------------------------------------------------ */

void sm_trace_backend_attach(struct modem_pipe *pipe)
{
	ARG_UNUSED(pipe);
}

void sm_trace_backend_detach(void)
{
}
