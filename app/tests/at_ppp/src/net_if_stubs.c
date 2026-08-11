/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file net_if_stubs.c
 *
 * Fake PPP network interface used by the sm_ppp.c unit tests.
 *
 * The real Zephyr <zephyr/net/net_if.h> header is used, so all the inline
 * accessors (net_if_l2_data(), net_if_flag_set(), net_if_set_mtu(), ...)
 * operate on the fake interface defined here. The out-of-line net_if API that
 * sm_ppp.c relies on is implemented below with failure injection support.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ppp.h>
#include <zephyr/modem/ppp.h>

#include "ppp_stubs.h"

static struct ppp_context stub_ppp_ctx;
static struct net_if_dev stub_if_dev = {
	.l2_data = &stub_ppp_ctx,
	.mtu = 1500,
};
static struct net_if stub_iface = {
	.if_dev = &stub_if_dev,
};

int ppp_stub_net_if_up_ret;
int ppp_stub_net_if_down_ret;
int ppp_stub_net_if_down_fail_count;

unsigned int ppp_stub_net_if_up_calls;
unsigned int ppp_stub_net_if_down_calls;
unsigned int ppp_stub_net_if_carrier_on_calls;
unsigned int ppp_stub_net_if_carrier_off_calls;
unsigned int ppp_stub_net_if_dormant_on_calls;
unsigned int ppp_stub_net_if_dormant_off_calls;
uint16_t ppp_stub_net_if_mtu;

/* Registered by sm_ppp.c with NET_MGMT_REGISTER_EVENT_HANDLER(). The net_mgmt
 * event subsystem is not part of this build, so the iterable section it would
 * normally be collected into does not exist and the handler is referenced by
 * name instead.
 */
extern const struct net_mgmt_event_static_handler sm_ppp_event_handler;

void ppp_stub_raise_ppp_phase_event(uint64_t mgmt_event)
{
	if ((sm_ppp_event_handler.event_mask & mgmt_event) != mgmt_event) {
		return;
	}
	sm_ppp_event_handler.handler(mgmt_event, &stub_iface, NULL, 0,
				     sm_ppp_event_handler.user_data);
}

struct net_if *ppp_stub_iface(void)
{
	return &stub_iface;
}

void ppp_stub_net_if_reset(void)
{
	ppp_stub_net_if_up_ret = 0;
	ppp_stub_net_if_down_ret = 0;
	ppp_stub_net_if_down_fail_count = 0;
	ppp_stub_net_if_up_calls = 0;
	ppp_stub_net_if_down_calls = 0;
	ppp_stub_net_if_carrier_on_calls = 0;
	ppp_stub_net_if_carrier_off_calls = 0;
	ppp_stub_net_if_dormant_on_calls = 0;
	ppp_stub_net_if_dormant_off_calls = 0;
	ppp_stub_net_if_mtu = 0;
	memset(&stub_ppp_ctx, 0, sizeof(stub_ppp_ctx));
	atomic_clear_bit(stub_if_dev.flags, NET_IF_UP);
	atomic_clear_bit(stub_if_dev.flags, NET_IF_DORMANT);
	atomic_clear_bit(stub_if_dev.flags, NET_IF_LOWER_UP);
}

/* --- Out-of-line net_if API -------------------------------------------- */

int net_if_up(struct net_if *iface)
{
	ppp_stub_net_if_up_calls++;

	if (ppp_stub_net_if_up_ret) {
		return ppp_stub_net_if_up_ret;
	}

	atomic_set_bit(iface->if_dev->flags, NET_IF_UP);
	ppp_stub_net_if_mtu = iface->if_dev->mtu;
	return 0;
}

int net_if_down(struct net_if *iface)
{
	ppp_stub_net_if_down_calls++;

	if (ppp_stub_net_if_down_fail_count > 0) {
		ppp_stub_net_if_down_fail_count--;
		return ppp_stub_net_if_down_ret ? ppp_stub_net_if_down_ret : -EIO;
	}

	atomic_clear_bit(iface->if_dev->flags, NET_IF_UP);
	return 0;
}

void net_if_carrier_on(struct net_if *iface)
{
	ppp_stub_net_if_carrier_on_calls++;
	atomic_set_bit(iface->if_dev->flags, NET_IF_LOWER_UP);
}

void net_if_carrier_off(struct net_if *iface)
{
	ppp_stub_net_if_carrier_off_calls++;
	atomic_clear_bit(iface->if_dev->flags, NET_IF_LOWER_UP);
}

void net_if_dormant_on(struct net_if *iface)
{
	ppp_stub_net_if_dormant_on_calls++;
	atomic_set_bit(iface->if_dev->flags, NET_IF_DORMANT);
}

void net_if_dormant_off(struct net_if *iface)
{
	ppp_stub_net_if_dormant_off_calls++;
	atomic_clear_bit(iface->if_dev->flags, NET_IF_DORMANT);
}

int net_if_get_by_iface(struct net_if *iface)
{
	return (iface == &stub_iface) ? 1 : -1;
}

int net_if_set_link_addr_locked(struct net_if *iface, const uint8_t *addr, uint8_t len,
				enum net_link_type type)
{
	if (iface == NULL || addr == NULL || len > sizeof(iface->if_dev->link_addr.addr)) {
		return -EINVAL;
	}

	memcpy(iface->if_dev->link_addr.addr, addr, len);
	iface->if_dev->link_addr.len = len;
	iface->if_dev->link_addr.type = type;
	return 0;
}

/* --- modem_ppp fake ----------------------------------------------------- */

int modem_ppp_attach(struct modem_ppp *ppp, struct modem_pipe *pipe)
{
	ppp->pipe = pipe;
	ppp->attached = true;
	return 0;
}

int modem_ppp_release(struct modem_ppp *ppp)
{
	ppp->pipe = NULL;
	ppp->attached = false;
	return 0;
}

struct net_if *modem_ppp_get_iface(struct modem_ppp *ppp)
{
	ARG_UNUSED(ppp);
	return &stub_iface;
}
