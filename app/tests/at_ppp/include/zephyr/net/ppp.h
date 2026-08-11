/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file ppp.h
 *
 * Test-specific replacement of <zephyr/net/ppp.h>.
 *
 * The real header only defines struct ppp_context::ipcp when CONFIG_NET_IPV4 is
 * enabled, which in turn requires the whole networking stack to be configured.
 * This unit test build only needs the few PPP L2 definitions that sm_ppp.c
 * touches, so they are provided unconditionally here.
 */

#ifndef TEST_ZEPHYR_NET_PPP_H_
#define TEST_ZEPHYR_NET_PPP_H_

#include <stdint.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length of network interface identifier */
#define PPP_INTERFACE_IDENTIFIER_LEN 8

/** IPv4 control protocol options */
struct ipcp_options {
	/** IPv4 address */
	struct net_in_addr address;
	/** Primary DNS server address */
	struct net_in_addr dns1_address;
	/** Secondary DNS server address */
	struct net_in_addr dns2_address;
};

/** IPv6 control protocol options */
struct ipv6cp_options {
	/** Interface identifier */
	uint8_t iid[PPP_INTERFACE_IDENTIFIER_LEN];
};

/** PPP L2 context specific to a network interface */
struct ppp_context {
	/** IPv4 control protocol */
	struct {
		struct ipcp_options my_options;
		struct ipcp_options peer_options;
	} ipcp;

	/** IPv6 control protocol */
	struct {
		struct ipv6cp_options my_options;
		struct ipv6cp_options peer_options;
	} ipv6cp;
};

/* Event values only need to be distinct for the tests; nothing dispatches
 * them through the real net_mgmt event subsystem.
 */
#define NET_EVENT_PPP_PHASE_RUNNING 0x0AF10001ULL
#define NET_EVENT_PPP_PHASE_DEAD    0x0AF10002ULL

#ifdef __cplusplus
}
#endif

#endif /* TEST_ZEPHYR_NET_PPP_H_ */
