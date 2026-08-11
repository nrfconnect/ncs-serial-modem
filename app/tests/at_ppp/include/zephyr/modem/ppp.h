/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file ppp.h
 *
 * Test-specific replacement of <zephyr/modem/ppp.h>.
 *
 * The real header defines MODEM_PPP_DEFINE() in terms of NET_DEVICE_INIT() and
 * the PPP L2, which would pull in the whole Zephyr networking stack. The unit
 * tests only need a handle that can be attached to / released from a modem pipe
 * and a network interface pointer, so the module is reduced to a plain struct
 * bound to the fake interface provided by net_if_stubs.c.
 */

#ifndef TEST_ZEPHYR_MODEM_PPP_H_
#define TEST_ZEPHYR_MODEM_PPP_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/net/net_if.h>
#include <zephyr/modem/pipe.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fake modem PPP module. */
struct modem_ppp {
	const char *name;
	size_t mtu;
	size_t buf_size;
	struct modem_pipe *pipe;
	bool attached;
};

#define MODEM_PPP_DEFINE(_name, _init_iface, _prio, _mtu, _buf_size)                               \
	static struct modem_ppp _name = {                                                          \
		.name = #_name,                                                                    \
		.mtu = (_mtu),                                                                     \
		.buf_size = (_buf_size),                                                           \
	}

int modem_ppp_attach(struct modem_ppp *ppp, struct modem_pipe *pipe);
struct net_if *modem_ppp_get_iface(struct modem_ppp *ppp);
int modem_ppp_release(struct modem_ppp *ppp);

#ifdef __cplusplus
}
#endif

#endif /* TEST_ZEPHYR_MODEM_PPP_H_ */
