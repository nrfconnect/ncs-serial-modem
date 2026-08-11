/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file test_at_ppp.c
 *
 * Unit tests for sm_ppp.c and for the CMUX channel assignment of sm_cmux.c.
 *
 * Covers the PPP link state machine:
 *   - START  via AT#XPPP=1, AT+CGDATA and +CGEV auto-start
 *   - STOP   via AT#XPPP=0 and network loss
 *   - RESTART after a data socket polling error and after a link loss
 *   - Failure handling of net_if_up(), net_if_down() and socket creation
 *
 * and the two CMUX channel assignment behaviors:
 *   - Legacy   (AT#XCMUX):  a DLCI is statically reserved for PPP, so the PPP
 *                           channel keeps its pipe when PPP stops and PPP is
 *                           restarted automatically on the next +CGEV.
 *   - Standard (AT+CMUX):   no channel is reserved, PPP borrows the AT channel
 *                           through AT+CGDATA and gives it back to the AT host
 *                           when it stops. No automatic restart.
 *
 * Only sm_ppp.c and sm_cmux.c are compiled into the test image. The network
 * interface, the sockets, the eventfd, the CMUX multiplexer and the Serial
 * Modem AT host are stubbed (see net_if_stubs.c, socket_stubs.c, cmux_stubs.c
 * and sm_stubs.c), which makes the otherwise asynchronous state machine
 * deterministic.
 *
 * sm_ppp.c runs its state machine from the ppp_data_passing_thread, which is
 * woken up through the (stubbed) eventfd. pump() gives that thread a chance to
 * run before the assertions are evaluated.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unity.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ppp.h>

#include "sm_cmux.h"
#include "sm_defines.h"
#include "sm_ppp.h"
#include "ppp_stubs.h"

extern void at_monitor_dispatch(const char *at_notif);

/* Fake pipes handed out by cmux_stubs.c. Before CMUX is started the AT host
 * serves the UART pipe; once started it moves onto a DLCI pipe.
 */
#define UART_PIPE  ppp_stub_uart_pipe()
#define DLCI1_PIPE ppp_stub_dlci_pipe(CMUX_AT_CHANNEL)
#define DLCI2_PIPE ppp_stub_dlci_pipe(CMUX_PPP_CHANNEL)
#define DLCI3_PIPE ppp_stub_dlci_pipe(CMUX_MODEM_TRACE_CHANNEL)

/** Let the PPP data passing thread and the SM work queue run. */
static void pump(void)
{
	k_sleep(K_MSEC(50));
}

/** Let the PPP thread run past the one second delay of its error path. */
static void pump_long(void)
{
	k_sleep(K_MSEC(1500));
}

static void send_at_ok(const char *cmd)
{
	TEST_ASSERT_EQUAL_INT_MESSAGE(-SILENT_AT_COMMAND_RET, ppp_stub_send_at(cmd), cmd);
	pump();
}

static void start_ppp_ok(const char *cmd)
{
	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at(cmd));
	pump();
	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "PPP did not reach the running state");
}

void setUp(void)
{
	ppp_stub_net_if_reset();
	ppp_stub_socket_reset();
	ppp_stub_cmux_reset();
	ppp_stub_sm_reset();

	/* Model the AT host state at boot: the UART pipe gets the first (and
	 * therefore oldest) context, then sm_cmux_init() adds one per CMUX
	 * DLCI pipe. AT commands are received over the UART pipe until CMUX is
	 * started.
	 */
	ppp_stub_set_current_pipe(UART_PIPE);
	for (uint8_t ch = 1; ch <= CONFIG_SM_CMUX_CHANNEL_COUNT; ch++) {
		ppp_stub_set_current_pipe(ppp_stub_dlci_pipe(ch));
	}
	ppp_stub_set_current_pipe(UART_PIPE);
}

void tearDown(void)
{
	/* Return sm_ppp.c to its initial state: stopped, no pipe attached and
	 * the +CGEV monitor paused.
	 */
	sm_ppp_detach();
	pump();
	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "PPP was left running by a test");

	/* sm_cmux.c keeps static state, so make sure CMUX is stopped as well. */
	if (sm_cmux_is_started()) {
		(void)ppp_stub_send_at("AT#XCMUXCLD");
		pump();
	}
	TEST_ASSERT_FALSE_MESSAGE(sm_cmux_is_started(), "CMUX was left started by a test");
}

/* ------------------------------------------------------------------------
 * START
 * ------------------------------------------------------------------------
 */

void test_ppp_start_brings_link_up(void)
{
	start_ppp_ok("AT#XPPP=1");

	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_socket_calls);
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_open_socket_count());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_carrier_on_calls);
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_dormant_off_calls);
	/* The AT pipe is handed over to PPP. */
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_host_release_calls);
	/* MTU is taken from the PDN dynamic information. */
	TEST_ASSERT_EQUAL_UINT16(1464, ppp_stub_net_if_mtu);

	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,0"));
}

void test_ppp_start_with_cid(void)
{
	start_ppp_ok("AT#XPPP=1,2");

	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,2"));
}

void test_ppp_start_twice_fails(void)
{
	start_ppp_ok("AT#XPPP=1");

	TEST_ASSERT_EQUAL_INT(-EALREADY, ppp_stub_send_at("AT#XPPP=1"));
	TEST_ASSERT_TRUE(ppp_is_running());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
}

void test_ppp_start_without_pipe_fails(void)
{
	ppp_stub_set_current_pipe(NULL);

	TEST_ASSERT_EQUAL_INT(-ENODEV, ppp_stub_send_at("AT#XPPP=1"));
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_up_calls);
}

void test_ppp_start_without_ip_address_fails(void)
{
	ppp_stub_ipv4_addr[0] = '\0';
	ppp_stub_ipv6_addr[0] = '\0';

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	/* The interface is never brought up when there is no connectivity. */
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_up_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_socket_calls);
}

void test_ppp_start_with_ipv6_only(void)
{
	ppp_stub_ipv4_addr[0] = '\0';
	strcpy(ppp_stub_ipv6_addr, "2001:db8::1");

	start_ppp_ok("AT#XPPP=1");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
}

void test_ppp_read_command_reports_state(void)
{
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT#XPPP?"));
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 0,0,0"));

	ppp_stub_clear_output();
	start_ppp_ok("AT#XPPP=1");

	ppp_stub_clear_output();
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT#XPPP?"));
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,0"));
}

void test_ppp_start_via_cgdata(void)
{
	/* AT+CGDATA hands over to a work item that activates the PDP context. */
	TEST_ASSERT_EQUAL_INT(-AT_COMMAND_CONTINUE_RET, ppp_stub_send_at("AT+CGDATA=\"PPP\""));
	pump();

	TEST_ASSERT_TRUE(ppp_is_running());
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "CONNECT"));
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
}

void test_ppp_start_via_cgdata_fails_without_lte(void)
{
	ppp_stub_lte_enabled = false;

	TEST_ASSERT_EQUAL_INT(-ENOTCONN, ppp_stub_send_at("AT+CGDATA=\"PPP\""));
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
}

/* ------------------------------------------------------------------------
 * STOP
 * ------------------------------------------------------------------------
 */

void test_ppp_stop_brings_link_down(void)
{
	start_ppp_ok("AT#XPPP=1");
	ppp_stub_clear_output();

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=0"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_carrier_off_calls);
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_dormant_on_calls);
	/* The pipe is given back to the AT host. */
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_host_attach_calls);

	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 0,0,0"));
}

void test_ppp_stop_when_already_stopped_is_a_noop(void)
{
	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=0"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_carrier_off_calls);
}

void test_ppp_stop_on_socket_error(void)
{
	start_ppp_ok("AT#XPPP=1");
	ppp_stub_clear_output();

	/* POLLERR on the data sockets, as seen when the LTE link goes down. */
	ppp_stub_signal_socket_error();
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 0,0,0"));
}

/* ------------------------------------------------------------------------
 * RESTART
 * ------------------------------------------------------------------------
 */

void test_ppp_restart_on_poll_failure(void)
{
	/* Keep the pipe attached so that a network/error stop restarts the
	 * link instead of returning the pipe to the AT host (CMUX behavior).
	 */
	sm_ppp_attach(DLCI2_PIPE);
	start_ppp_ok("AT#XPPP=1");
	ppp_stub_clear_output();

	ppp_stub_signal_poll_failure();
	pump_long();

	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "PPP did not restart");
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_net_if_up_calls);
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_open_socket_count());
	/* The link went down and came back up again. */
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 0,0,0"));
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,0"));
}

void test_ppp_restart_via_cgev_after_network_loss(void)
{
	sm_ppp_attach(DLCI2_PIPE);
	start_ppp_ok("AT#XPPP=1");

	/* Network loss keeps the +CGEV monitor active so that PPP can be
	 * restarted when the PDN is activated again.
	 */
	ppp_stub_signal_socket_error();
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());

	ppp_stub_clear_output();
	at_monitor_dispatch("+CGEV: ME PDN ACT 0\r\n");
	pump();

	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "+CGEV did not restart PPP");
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_net_if_up_calls);
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,0"));
}

void test_ppp_cgev_for_other_cid_is_ignored(void)
{
	sm_ppp_attach(DLCI2_PIPE);
	start_ppp_ok("AT#XPPP=1");

	ppp_stub_signal_socket_error();
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());

	at_monitor_dispatch("+CGEV: ME PDN ACT 3\r\n");
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);

	/* Unrelated +CGEV notifications must not start PPP either. */
	at_monitor_dispatch("+CGEV: ME DETACH\r\n");
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
}

/* Regression tests for ppp_data_passing_thread() using stale descriptors.
 *
 * ppp_data_passing_thread() polls a fixed fds[] array holding the eventfd and
 * the two PPP data sockets. Servicing the eventfd runs ppp_work_fn(), which
 * performs the START/STOP/RESTART actions and therefore closes and reopens the
 * data sockets. Every remaining entry of fds[] then describes a descriptor
 * that no longer exists, together with the revents captured before the sockets
 * were replaced.
 *
 * Descriptors are recycled lowest-free-first, so the reopened sockets get the
 * very same numbers back and the stale entries still compare equal to
 * ppp_fds[]. Continuing the loop would therefore apply a stale POLLERR to the
 * freshly opened sockets and tear the new link down immediately.
 *
 * The loop must instead break out and poll again with a rebuilt fds[].
 *
 * Both ways of stopping PPP while it is still RUNNING are covered, as only
 * those leave the data sockets in fds[]. The POLLERR handler itself moves PPP
 * to PPP_STATE_STOPPING, so a stop that originates from the data sockets
 * shrinks fds[] to the eventfd alone and cannot expose the problem.
 */
static void run_stale_descriptor_regression(bool network_driven)
{
	send_at_ok("AT#XCMUX=1");
	start_ppp_ok("AT#XPPP=1");

	if (network_driven) {
		/* The peer has to be connected for a link loss to be reported.
		 * The matching NET_EVENT_PPP_PHASE_DEAD below clears this again.
		 */
		ppp_stub_raise_ppp_phase_event(NET_EVENT_PPP_PHASE_RUNNING);
		pump();
	}

	const int zephyr_fd = ppp_stub_open_socket_fd(0);
	const int modem_fd = ppp_stub_open_socket_fd(1);

	TEST_ASSERT_NOT_EQUAL_INT(-1, zephyr_fd);
	TEST_ASSERT_NOT_EQUAL_INT(-1, modem_fd);

	/* Freeze the PPP thread inside zsock_poll() so that the link error, the
	 * stop and the restart all end up in the same poll result.
	 */
	ppp_stub_hold_poll();

	/* The data sockets report POLLERR, as they do when the link drops. */
	ppp_stub_signal_socket_error();

	if (network_driven) {
		/* The link goes down on its own. PPP is still RUNNING here, so
		 * the data sockets are still part of fds[].
		 */
		ppp_stub_raise_ppp_phase_event(NET_EVENT_PPP_PHASE_DEAD);
	} else {
		/* The host stops PPP over the AT channel. */
		TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=0"));
		/* AT#XPPP=0 also disables auto-start, re-arm it so that the
		 * network can bring PPP back up.
		 */
		sm_ppp_set_auto_start(true);
	}

	/* The network is reactivated immediately, which queues the restart
	 * behind the stop. sm_ppp.c drains both in one ppp_work_fn() pass,
	 * closing and reopening the data sockets.
	 */
	at_monitor_dispatch("+CGEV: ME PDN ACT 0\r\n");

	ppp_stub_release_poll();
	pump();

	/* The poll really did report the eventfd and both data sockets, so the
	 * stale entries were there to be (mis)used.
	 */
	TEST_ASSERT_EQUAL_UINT_MESSAGE(3, ppp_stub_max_poll_events,
				       "the test did not build a multi-event poll result");

	/* The restarted link survives: the stale POLLERR of the previous poll
	 * must not be applied to the new sockets.
	 */
	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(),
				 "a stale POLLERR from the previous poll tore the new link down");
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_net_if_up_calls);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(1, ppp_stub_net_if_down_calls,
				       "the link was brought down more than once");
	TEST_ASSERT_FALSE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE));

	/* The sockets were closed and reopened, recycling the descriptors. */
	TEST_ASSERT_EQUAL_UINT(4, ppp_stub_socket_calls);
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_open_socket_count());
	TEST_ASSERT_EQUAL_INT_MESSAGE(zephyr_fd, ppp_stub_open_socket_fd(0),
				      "descriptors were not recycled, the test is not testing "
				      "the stale descriptor hazard anymore");
	TEST_ASSERT_EQUAL_INT_MESSAGE(modem_fd, ppp_stub_open_socket_fd(1),
				      "descriptors were not recycled, the test is not testing "
				      "the stale descriptor hazard anymore");
}

/* The link is lost and the network is reactivated right away. */
void test_ppp_poll_loop_does_not_reuse_stale_descriptors_on_network_restart(void)
{
	run_stale_descriptor_regression(true);
}

/* AT#XPPP=0 is followed by a network reactivation. */
void test_ppp_poll_loop_does_not_reuse_stale_descriptors_on_cmd_restart(void)
{
	run_stale_descriptor_regression(false);
}

/* ------------------------------------------------------------------------
 * Failure injection
 * ------------------------------------------------------------------------
 */

void test_ppp_start_fails_when_net_if_up_fails(void)
{
	ppp_stub_net_if_up_ret = -EIO;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "PPP must be stopped if net_if_up() fails");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
	/* No sockets are opened when the interface cannot be brought up. */
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_socket_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
	/* The interface never came up, so it is not brought down again. */
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_down_calls);
	/* The pipe is returned to the AT host. */
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_host_attach_calls);
}

void test_ppp_stop_retries_when_net_if_down_fails(void)
{
	start_ppp_ok("AT#XPPP=1");

	/* Fail the first net_if_down() only; sm_ppp.c must reschedule the stop. */
	ppp_stub_net_if_down_fail_count = 1;
	ppp_stub_net_if_down_ret = -EIO;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=0"));
	pump();

	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "PPP must stop once net_if_down() succeeds");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(2, ppp_stub_net_if_down_calls,
				       "net_if_down() was not retried");
	/* The interface is marked dormant while the stop is pending. */
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_net_if_dormant_on_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
}

void test_ppp_stop_keeps_retrying_while_net_if_down_fails(void)
{
	start_ppp_ok("AT#XPPP=1");

	ppp_stub_net_if_down_fail_count = 3;
	ppp_stub_net_if_down_ret = -EIO;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=0"));
	pump();

	/* PPP stops only after net_if_down() finally succeeds. */
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(4, ppp_stub_net_if_down_calls);
	/* Sockets are released on the very first stop attempt. */
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
}

void test_ppp_start_fails_when_zephyr_socket_fails(void)
{
	ppp_stub_socket_fail_nth = 1;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_socket_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
	/* net_if_up() succeeded, so the interface must be brought down again. */
	TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_carrier_on_calls);
}

void test_ppp_start_fails_when_modem_socket_fails(void)
{
	ppp_stub_socket_fail_nth = 2;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_socket_calls);
	/* The already opened Zephyr socket must not be leaked. */
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, ppp_stub_open_socket_count(), "socket leaked");
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_carrier_on_calls);
}

void test_ppp_start_fails_when_bind_fails(void)
{
	ppp_stub_bind_fail = true;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, ppp_stub_open_socket_count(), "socket leaked");
}

void test_ppp_start_fails_when_setsockopt_fails(void)
{
	ppp_stub_setsockopt_fail = true;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_socket_calls);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, ppp_stub_open_socket_count(), "socket leaked");
}

void test_ppp_start_fails_when_pdn_id_unavailable(void)
{
	ppp_stub_pdn_id = -1;

	TEST_ASSERT_EQUAL_INT(-SILENT_AT_COMMAND_RET, ppp_stub_send_at("AT#XPPP=1"));
	pump();

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, ppp_stub_open_socket_count(), "socket leaked");
}

/* ------------------------------------------------------------------------
 * CMUX channel assignment: legacy AT#XCMUX behavior
 *
 * assign_default_channels() statically reserves CMUX_PPP_CHANNEL for the PPP
 * module through sm_ppp_attach(), which sets sm_ppp_keep_pipe_attached. The
 * PPP channel therefore never returns to AT command mode and PPP restarts by
 * itself when the PDN comes back.
 *
 *   AT#XCMUX=1
 *   AT#XPPP=1
 *   AT+CFUN=1
 * ------------------------------------------------------------------------
 */

void test_cmux_legacy_reserves_ppp_channel(void)
{
	send_at_ok("AT#XCMUX=1");

	TEST_ASSERT_TRUE(sm_cmux_is_started());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_cmux_attach_calls);

	/* The AT host moved from the UART pipe onto the AT channel. */
	TEST_ASSERT_EQUAL_PTR(DLCI1_PIPE, ppp_stub_at_pipe());
	TEST_ASSERT_TRUE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE),
				 "AT channel must stay in AT command mode");
	/* The PPP channel was handed over to the PPP module. */
	TEST_ASSERT_FALSE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE),
				  "PPP channel must be reserved for PPP");
	/* Unrelated channels are untouched. */
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI3_PIPE));
}

void test_cmux_legacy_ppp_starts_on_reserved_channel(void)
{
	send_at_ok("AT#XCMUX=1");
	ppp_stub_clear_output();

	/* AT#XPPP is issued from the AT channel but PPP uses its reserved one,
	 * so the AT channel keeps serving AT commands.
	 */
	start_ppp_ok("AT#XPPP=1");

	TEST_ASSERT_EQUAL_PTR(DLCI1_PIPE, ppp_stub_at_pipe());
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE));
	TEST_ASSERT_FALSE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE));
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,0"));
}

void test_cmux_legacy_start_sequence_defers_ppp_until_pdn_activation(void)
{
	send_at_ok("AT#XCMUX=1");

	/* AT#XPPP=1 is accepted before the network is attached. PPP cannot
	 * start yet because the PDN has no IP address.
	 */
	ppp_stub_ipv4_addr[0] = '\0';
	ppp_stub_ipv6_addr[0] = '\0';

	send_at_ok("AT#XPPP=1");
	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "PPP must wait for the PDN");
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_net_if_up_calls);

	/* AT+CFUN=1 re-subscribes to the +CGEV notifications. */
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT+CFUN=1"));

	/* The PDN is activated and PPP starts on the reserved channel. */
	strcpy(ppp_stub_ipv4_addr, "192.0.2.1");
	at_monitor_dispatch("+CGEV: ME PDN ACT 0\r\n");
	pump();

	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "+CGEV did not start PPP");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
	TEST_ASSERT_FALSE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE));
}

void test_cmux_legacy_network_loss_keeps_pipe_and_reattaches(void)
{
	send_at_ok("AT#XCMUX=1");
	start_ppp_ok("AT#XPPP=1");
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT+CFUN=1"));

	/* The PDN is deactivated: the modem reports it and closes the data
	 * socket, which is what actually stops PPP.
	 */
	at_monitor_dispatch("+CGEV: ME PDN DEACT 0\r\n");
	ppp_stub_signal_socket_error();
	pump();

	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "network loss must stop PPP");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
	/* Legacy behavior: the reserved channel is NOT given back to the AT
	 * host, so the host cannot accidentally take it over.
	 */
	TEST_ASSERT_FALSE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE),
				  "PPP channel must stay reserved across a restart");
	TEST_ASSERT_EQUAL_PTR(DLCI1_PIPE, ppp_stub_at_pipe());

	/* Immediate re-attach: PPP comes back up on the very same channel. */
	ppp_stub_clear_output();
	at_monitor_dispatch("+CGEV: ME PDN ACT 0\r\n");
	pump();

	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "PPP did not re-attach after +CGEV");
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_net_if_up_calls);
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_open_socket_count());
	TEST_ASSERT_FALSE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE));
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "#XPPP: 1,0,0"));
}

void test_cmux_legacy_stop_command_keeps_channel_reserved(void)
{
	send_at_ok("AT#XCMUX=1");
	start_ppp_ok("AT#XPPP=1");

	/* An explicit AT#XPPP=0 is a user command, not a network event, so it
	 * disables the auto-start but still keeps the reserved channel.
	 */
	send_at_ok("AT#XPPP=0");

	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_FALSE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE),
				  "PPP channel must stay reserved");

	/* Auto-start is off, so a PDN activation must not restart PPP. */
	at_monitor_dispatch("+CGEV: ME PDN ACT 0\r\n");
	pump();
	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "AT#XPPP=0 must disable the auto-start");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
}

void test_cmux_legacy_stop_releases_ppp_channel(void)
{
	send_at_ok("AT#XCMUX=1");
	start_ppp_ok("AT#XPPP=1");

	/* Tearing CMUX down detaches PPP and returns the AT host to the UART. */
	send_at_ok("AT#XCMUXCLD");

	TEST_ASSERT_FALSE(sm_cmux_is_started());
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_cmux_release_calls);
	TEST_ASSERT_EQUAL_PTR(UART_PIPE, ppp_stub_at_pipe());
	/* All DLCI pipes are back in AT command mode. */
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE));
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE));
}

void test_cmux_legacy_start_twice_fails(void)
{
	send_at_ok("AT#XCMUX=1");

	TEST_ASSERT_EQUAL_INT(-EALREADY, ppp_stub_send_at("AT#XCMUX"));
	TEST_ASSERT_TRUE(sm_cmux_is_started());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_cmux_attach_calls);
}

void test_cmux_legacy_at_channel_cannot_move_while_ppp_runs(void)
{
	send_at_ok("AT#XCMUX=1");
	start_ppp_ok("AT#XPPP=1");

	/* The AT channel cannot be swapped onto the channel PPP is using. */
	TEST_ASSERT_EQUAL_INT(-ENOTSUP, ppp_stub_send_at("AT#XCMUX=2"));
	TEST_ASSERT_EQUAL_PTR(DLCI1_PIPE, ppp_stub_at_pipe());
	TEST_ASSERT_TRUE(ppp_is_running());
}

/* ------------------------------------------------------------------------
 * CMUX channel assignment: standard AT+CMUX behavior
 *
 * AT+CMUX only starts the multiplexer, it never reserves a channel. PPP is
 * started with AT+CGDATA on the AT channel and, per 3GPP TS 27.007, that
 * channel returns to AT command mode when PPP stops. There is no automatic
 * restart.
 *
 *   AT+CMUX=0
 *   AT+CFUN=1
 *   AT+CGDATA
 * ------------------------------------------------------------------------
 */

void test_cmux_standard_does_not_reserve_ppp_channel(void)
{
	send_at_ok("AT+CMUX=0");

	TEST_ASSERT_TRUE(sm_cmux_is_started());
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_cmux_attach_calls);

	TEST_ASSERT_EQUAL_PTR(DLCI1_PIPE, ppp_stub_at_pipe());
	/* No channel is handed over to PPP. */
	TEST_ASSERT_TRUE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE),
				 "AT+CMUX must not reserve a PPP channel");
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI3_PIPE));
}

void test_cmux_standard_cgdata_borrows_the_at_channel(void)
{
	send_at_ok("AT+CMUX=0");
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT+CFUN=1"));
	ppp_stub_clear_output();

	TEST_ASSERT_EQUAL_INT(-AT_COMMAND_CONTINUE_RET, ppp_stub_send_at("AT+CGDATA=\"PPP\""));
	pump();

	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "AT+CGDATA did not start PPP");
	TEST_ASSERT_NOT_NULL(strstr(ppp_stub_get_output(), "CONNECT"));
	/* The AT channel entered data mode. */
	TEST_ASSERT_FALSE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE),
				  "the AT channel must be in data mode while PPP runs");
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI2_PIPE));
}

void test_cmux_standard_network_loss_returns_channel_to_at_mode(void)
{
	send_at_ok("AT+CMUX=0");
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT+CFUN=1"));
	TEST_ASSERT_EQUAL_INT(-AT_COMMAND_CONTINUE_RET, ppp_stub_send_at("AT+CGDATA=\"PPP\""));
	pump();
	TEST_ASSERT_TRUE(ppp_is_running());

	/* The PDN is deactivated and the modem closes the data socket. */
	at_monitor_dispatch("+CGEV: ME PDN DEACT 0\r\n");
	ppp_stub_signal_socket_error();
	pump();

	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "network loss must stop PPP");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_down_calls);
	TEST_ASSERT_EQUAL_UINT(0, ppp_stub_open_socket_count());
	/* Standard behavior: the channel goes back to AT command mode. */
	TEST_ASSERT_TRUE_MESSAGE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE),
				 "the AT channel must return to AT command mode");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_host_attach_calls);

	/* An immediate PDN re-activation must NOT restart PPP by itself; the
	 * host is expected to re-run AT+CGDATA.
	 */
	at_monitor_dispatch("+CGEV: ME PDN ACT 0\r\n");
	pump();
	TEST_ASSERT_TRUE_MESSAGE(sm_ppp_is_stopped(), "AT+CMUX mode must not auto-restart PPP");
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_net_if_up_calls);
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE));
}

void test_cmux_standard_cgdata_can_be_repeated_after_network_loss(void)
{
	send_at_ok("AT+CMUX=0");
	TEST_ASSERT_EQUAL_INT(0, ppp_stub_send_at("AT+CFUN=1"));
	TEST_ASSERT_EQUAL_INT(-AT_COMMAND_CONTINUE_RET, ppp_stub_send_at("AT+CGDATA=\"PPP\""));
	pump();
	TEST_ASSERT_TRUE(ppp_is_running());

	ppp_stub_signal_socket_error();
	pump();
	TEST_ASSERT_TRUE(sm_ppp_is_stopped());
	TEST_ASSERT_TRUE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE));

	/* The AT channel accepts commands again, so the dial script can simply
	 * be re-run on the same channel.
	 */
	TEST_ASSERT_EQUAL_INT(-AT_COMMAND_CONTINUE_RET, ppp_stub_send_at("AT+CGDATA=\"PPP\""));
	pump();

	TEST_ASSERT_TRUE_MESSAGE(ppp_is_running(), "AT+CGDATA could not be repeated");
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_net_if_up_calls);
	TEST_ASSERT_EQUAL_UINT(2, ppp_stub_open_socket_count());
	TEST_ASSERT_FALSE(ppp_stub_pipe_in_at_mode(DLCI1_PIPE));
}

void test_cmux_standard_start_twice_fails(void)
{
	send_at_ok("AT+CMUX=0");

	TEST_ASSERT_EQUAL_INT(-EALREADY, ppp_stub_send_at("AT+CMUX=0"));
	TEST_ASSERT_EQUAL_UINT(1, ppp_stub_cmux_attach_calls);
}

void test_cmux_standard_rejects_unsupported_parameters(void)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, ppp_stub_send_at("AT+CMUX=1"));
	TEST_ASSERT_EQUAL_INT(-EINVAL, ppp_stub_send_at("AT+CMUX=0,1"));
	TEST_ASSERT_FALSE(sm_cmux_is_started());
}

void test_cmux_start_failure_leaves_cmux_stopped(void)
{
	ppp_stub_cmux_attach_ret = -EIO;

	TEST_ASSERT_EQUAL_INT(-EIO, ppp_stub_send_at("AT+CMUX=0"));
	pump();

	TEST_ASSERT_FALSE_MESSAGE(sm_cmux_is_started(), "a failed start must not leave CMUX up");
	/* The AT host is back on the UART pipe. */
	TEST_ASSERT_EQUAL_PTR(UART_PIPE, ppp_stub_at_pipe());
}

/* ------------------------------------------------------------------------
 * Test entry point
 * ------------------------------------------------------------------------
 */

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
