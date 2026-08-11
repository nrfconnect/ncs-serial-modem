/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file ppp_stubs.h
 *
 * Control and observation interface of the stubs used by the sm_ppp.c unit
 * tests (net_if_stubs.c, socket_stubs.c and sm_stubs.c).
 */

#ifndef PPP_STUBS_H_
#define PPP_STUBS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct net_if;
struct modem_pipe;

/** Number of fake CMUX DLCI pipes provided by cmux_stubs.c. */
#define PPP_STUB_MAX_DLCI 4

/* ------------------------------------------------------------------------
 * CMUX / UART backend stubs (cmux_stubs.c)
 * ------------------------------------------------------------------------
 */

/** Reset the CMUX stub state and failure injection. */
void ppp_stub_cmux_reset(void);

/** Fake UART pipe returned by sm_uart_pipe_get(). */
struct modem_pipe *ppp_stub_uart_pipe(void);

/** Fake pipe of CMUX DLCI @p address (1-based), or NULL if out of range. */
struct modem_pipe *ppp_stub_dlci_pipe(uint8_t address);

/** Human readable name of a fake pipe, for assertion messages. */
const char *ppp_stub_pipe_name(struct modem_pipe *pipe);

/** Simulate the CMUX peer tearing the multiplexer down. */
void ppp_stub_cmux_signal_disconnect(void);

/** Error code returned by modem_cmux_attach(). 0 = success. */
extern int ppp_stub_cmux_attach_ret;

extern unsigned int ppp_stub_cmux_attach_calls;
extern unsigned int ppp_stub_cmux_release_calls;

/* ------------------------------------------------------------------------
 * net_if stubs (net_if_stubs.c)
 * ------------------------------------------------------------------------
 */

/** Fake PPP network interface handed out by modem_ppp_get_iface(). */
struct net_if *ppp_stub_iface(void);

/** Reset all net_if stub state and failure injection. */
void ppp_stub_net_if_reset(void);

/** Error code returned by the next net_if_up() call. 0 = success. */
extern int ppp_stub_net_if_up_ret;
/** Error code returned by the next net_if_down() calls. 0 = success. */
extern int ppp_stub_net_if_down_ret;
/** Number of net_if_down() calls that fail before it starts succeeding. */
extern int ppp_stub_net_if_down_fail_count;

extern unsigned int ppp_stub_net_if_up_calls;
extern unsigned int ppp_stub_net_if_down_calls;
extern unsigned int ppp_stub_net_if_carrier_on_calls;
extern unsigned int ppp_stub_net_if_carrier_off_calls;
extern unsigned int ppp_stub_net_if_dormant_on_calls;
extern unsigned int ppp_stub_net_if_dormant_off_calls;
extern uint16_t ppp_stub_net_if_mtu;

/* ------------------------------------------------------------------------
 * socket / eventfd stubs (socket_stubs.c)
 * ------------------------------------------------------------------------
 */

/** Reset all socket stub state and failure injection. */
void ppp_stub_socket_reset(void);

/**
 * Fail the n:th zsock_socket() call after the reset.
 * 0 disables the injection, 1 fails the Zephyr socket, 2 the modem socket.
 */
extern unsigned int ppp_stub_socket_fail_nth;
/** Make zsock_bind() fail. */
extern bool ppp_stub_bind_fail;
/** Make zsock_setsockopt() fail. */
extern bool ppp_stub_setsockopt_fail;
/** Value returned by zsock_inet_pton(). 1 = success. */
extern int ppp_stub_inet_pton_ret;

extern unsigned int ppp_stub_socket_calls;
extern unsigned int ppp_stub_close_calls;

/** Number of sockets created by zsock_socket() that are still open. */
unsigned int ppp_stub_open_socket_count(void);

/**
 * Descriptor of the @p idx:th still open socket, or -1 if there is none.
 * Descriptors are handed out lowest-free-first, like a real socket layer, so
 * closing and immediately reopening the PPP sockets recycles the same numbers.
 */
int ppp_stub_open_socket_fd(unsigned int idx);

/**
 * Invoke the net_mgmt event handlers sm_ppp.c registered for the PPP phase
 * events, as the net_mgmt event thread would. Use NET_EVENT_PPP_PHASE_RUNNING
 * to mark the peer as connected and NET_EVENT_PPP_PHASE_DEAD to drop the link.
 */
void ppp_stub_raise_ppp_phase_event(uint64_t mgmt_event);

/**
 * Block zsock_poll() from returning until ppp_stub_release_poll() is called.
 * This lets a test gather several independent events into a single poll
 * result.
 */
void ppp_stub_hold_poll(void);
void ppp_stub_release_poll(void);

/** Largest number of descriptors a single zsock_poll() call reported events on. */
extern unsigned int ppp_stub_max_poll_events;

/** Simulate a POLLERR on the PPP data sockets (link went down). */
void ppp_stub_signal_socket_error(void);

/** Make the next zsock_poll() call fail, which triggers a PPP restart. */
void ppp_stub_signal_poll_failure(void);

/* ------------------------------------------------------------------------
 * Serial Modem stubs (sm_stubs.c)
 * ------------------------------------------------------------------------
 */

/** Reset the Serial Modem stubs, including the captured response buffer. */
void ppp_stub_sm_reset(void);

/** Captured rsp_send()/urc_send_to() output. */
const char *ppp_stub_get_output(void);
void ppp_stub_clear_output(void);

/**
 * Pipe returned by sm_at_host_get_current()/sm_at_host_get_current_pipe().
 * Passing NULL drops every AT host context, modelling "no pipe in AT mode".
 */
void ppp_stub_set_current_pipe(struct modem_pipe *pipe);

/** Pipe the AT host is currently serving commands on. */
struct modem_pipe *ppp_stub_at_pipe(void);

/** Whether @p pipe currently has an AT host context, i.e. is in AT mode. */
bool ppp_stub_pipe_in_at_mode(struct modem_pipe *pipe);

/** Return value of sm_util_pdn_id_get(). */
extern int ppp_stub_pdn_id;
/** Return value of sm_util_cfun_is_lte_enabled(). */
extern bool ppp_stub_lte_enabled;
/** Return value of sm_util_cereg_is_registered(). */
extern bool ppp_stub_registered;
/** Return value of sm_util_is_cid_active(). */
extern bool ppp_stub_cid_active;
/** Return value of sm_util_at_printf() and sm_util_at_cmd_no_intercept(). */
extern int ppp_stub_at_ret;
/** Return value of sm_util_pdn_dynamic_info_get(). 0 = success. */
extern int ppp_stub_pdn_info_ret;
/** IPv4 address reported by util_get_ip_addr(). Empty string = none. */
extern char ppp_stub_ipv4_addr[];
/** IPv6 address reported by util_get_ip_addr(). Empty string = none. */
extern char ppp_stub_ipv6_addr[];

extern unsigned int ppp_stub_host_release_calls;
extern unsigned int ppp_stub_host_attach_calls;
extern unsigned int ppp_stub_cmd_done_calls;

/** Send an AT command to the sm_ppp.c custom command handlers. */
int ppp_stub_send_at(const char *at_cmd);

/** Last AT response ("OK\r\n", "ERROR\r\n", ...) built by sm_at_cb_wrapper(). */
const char *ppp_stub_last_at_response(void);

#endif /* PPP_STUBS_H_ */
