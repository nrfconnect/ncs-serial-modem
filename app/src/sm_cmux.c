/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "sm_cmux.h"
#include "sm_at_host.h"
#include "sm_ppp.h"
#include "sm_trace_backend_cmux.h"
#include "sm_util.h"
#include "sm_uart_handler.h"
#include "sm_trace_backend_cmux.h"
#include <zephyr/logging/log.h>
#include <zephyr/modem/cmux.h>
#include <zephyr/modem/pipe.h>
#include <assert.h>

/* This makes use of part of the Zephyr modem subsystem which has a CMUX module. */
LOG_MODULE_REGISTER(sm_cmux, CONFIG_SM_LOG_LEVEL);

#define RECV_BUF_LEN     SM_AT_MAX_CMD_LEN
/* The CMUX module reserves some spare buffer bytes. To achieve a maximum
 * response length of SM_AT_MAX_RSP_LEN (comprising the "OK" or "ERROR"
 * that is sent separately), the transmit buffer must be made a bit bigger.
 * 49 extra bytes was manually found to allow SM_AT_MAX_RSP_LEN long responses.
 */
#define TRANSMIT_BUF_LEN (49 + SM_AT_MAX_RSP_LEN)

#define DLCI_TO_INDEX(dlci)  ((dlci) - 1)
#define INDEX_TO_DLCI(index) ((index) + 1)
#define STOP_DELAY           K_MSEC(10)

static void stop_work_fn(struct k_work *work);

static struct {
	/* UART backend */
	struct modem_pipe *uart_pipe;
	bool uart_pipe_open;

	/* CMUX */
	struct modem_cmux instance;
	uint8_t cmux_receive_buf[MODEM_CMUX_WORK_BUFFER_SIZE];
	uint8_t cmux_transmit_buf[MODEM_CMUX_WORK_BUFFER_SIZE];

	/* CMUX channels (Data Link Connection Identifier); index = address - 1 */
	struct cmux_dlci {
		struct modem_cmux_dlci instance;
		struct modem_pipe *pipe;
		uint8_t receive_buf[RECV_BUF_LEN];
		struct k_work open_work;
	} dlcis[CONFIG_SM_CMUX_CHANNEL_COUNT];
	/* Index of the DLCI used for AT communication; defaults to 0. */
	unsigned int at_channel;

	/* CMUX control */
	struct k_work_delayable stop_work;
} cmux;

static void cmux_event_handler(struct modem_cmux *, enum modem_cmux_event event, void *)
{
	if (event == MODEM_CMUX_EVENT_CONNECTED || event == MODEM_CMUX_EVENT_DISCONNECTED) {
		LOG_INF("CMUX %sconnected.", (event == MODEM_CMUX_EVENT_CONNECTED) ? "" : "dis");
	}
	switch (event) {
	case MODEM_CMUX_EVENT_CONNECTED:
		break;
	case MODEM_CMUX_EVENT_DISCONNECTED:
		/* Return to AT command mode */
		k_work_reschedule_for_queue(&sm_work_q, &cmux.stop_work, STOP_DELAY);
		break;
	}
}

static void init_dlci(size_t dlci_idx, uint16_t recv_buf_size,
		      uint8_t recv_buf[static recv_buf_size])
{
	assert(ARRAY_SIZE(cmux.dlcis) > dlci_idx);

	struct cmux_dlci *const dlci = &cmux.dlcis[dlci_idx];
	const struct modem_cmux_dlci_config dlci_config = {
		.dlci_address = dlci_idx + 1,
		.receive_buf = recv_buf,
		.receive_buf_size = recv_buf_size
	};

	dlci->pipe = modem_cmux_dlci_init(&cmux.instance, &dlci->instance, &dlci_config);

	sm_at_host_attach(dlci->pipe);
}

bool sm_cmux_is_started(void)
{
	return (cmux.uart_pipe != NULL);
}

static int sm_cmux_init(void)
{
	const struct modem_cmux_config cmux_config = {
		.callback = cmux_event_handler,
		.receive_buf = cmux.cmux_receive_buf,
		.receive_buf_size = sizeof(cmux.cmux_receive_buf),
		.transmit_buf = cmux.cmux_transmit_buf,
		.transmit_buf_size = sizeof(cmux.cmux_transmit_buf),
	};

	modem_cmux_init(&cmux.instance, &cmux_config);

	for (size_t i = 0; i != ARRAY_SIZE(cmux.dlcis); ++i) {
		init_dlci(i, sizeof(cmux.dlcis[i].receive_buf), cmux.dlcis[i].receive_buf);
	}

	k_work_init_delayable(&cmux.stop_work, stop_work_fn);

	cmux.at_channel = 0;
	return 0;
}
SYS_INIT(sm_cmux_init, APPLICATION, 0);

static void stop_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (sm_cmux_is_started()) {
		if (IS_ENABLED(CONFIG_SM_PPP)) {
			sm_ppp_detach();
		}

		if (IS_ENABLED(CONFIG_SM_MODEM_TRACE_BACKEND_CMUX)) {
			sm_trace_backend_detach();
		}

		modem_cmux_release(&cmux.instance);
		cmux.at_channel = 0;

		/* Return AT host to UART pipe */
		sm_at_host_set_pipe(sm_at_host_get_urc_ctx(), cmux.uart_pipe);
		cmux.uart_pipe = NULL;
		cmux.uart_pipe_open = false;

		/* Reset all DLCI pipes to a closed state (modem_cmux_release does not clean up) */
		for (size_t i = 0; i != ARRAY_SIZE(cmux.dlcis); ++i) {
			init_dlci(i, sizeof(cmux.dlcis[i].receive_buf), cmux.dlcis[i].receive_buf);
		}
	}
	LOG_INF("Returned to AT command mode.");
}

struct modem_pipe *sm_cmux_get_dlci(uint8_t address)
{
	int i = DLCI_TO_INDEX(address);

	if (i < 0 || i >= ARRAY_SIZE(cmux.dlcis)) {
		return NULL;
	}
	return cmux.dlcis[i].pipe;
}

static void assign_default_channels(void)
{
	/*
	 * Assign default channels as previously done
	 * with proprietary AT#XCMUX command.
	 *
	 */
	if (IS_ENABLED(CONFIG_SM_PPP)) {
		/* Reserve PPP channel pipe for PPP module */
		struct modem_pipe *ppp_pipe = cmux.at_channel == 0
						      ? sm_cmux_get_dlci(CMUX_PPP_CHANNEL)
						      : cmux.dlcis[!cmux.at_channel].pipe;

		LOG_DBG("Reserving CMUX PPP channel pipe %p for PPP module", (void *)ppp_pipe);
		sm_at_host_release(sm_at_host_get_ctx_from(ppp_pipe));
		sm_ppp_attach(ppp_pipe);
	}
	if (IS_ENABLED(CONFIG_SM_MODEM_TRACE_BACKEND_CMUX)) {
		/* Reserve trace channel pipe for trace backend */
		struct modem_pipe *trace_pipe = sm_cmux_get_dlci(CMUX_MODEM_TRACE_CHANNEL);

		LOG_DBG("Reserving CMUX trace channel pipe %p for trace backend",
			(void *)trace_pipe);
		sm_at_host_release(sm_at_host_get_ctx_from(trace_pipe));
		sm_trace_backend_attach(trace_pipe);
	}
}

static int do_at_and_ppp_channel_switch(int new_at_channel)
{
	/*
	 * This is the legacy behavior of AT#XCMUX=2 that was implemented
	 * to allow |SM| to be used with Zephyr modem driver that expects
	 * ATD*99# command to switch current channel to PPP data mode.
	 * When we did not have a such command, we emulated it with sequence of
	 *  AT#XPPP=1
	 *  AT+CFUN=1
	 *  AT#XCMUX=2
	 * And if that was executed before network is attached, it succeeded
	 * to switch the PPP pipe and AT pipe before the PPP module started.
	 *
	 * However, it was problematic if re-attaching of PPP was required.
	 * All 3GPP compliant modems set the channel back to AT mode when PPP
	 * stops, so Zephyr expected that it can just re-run the dial script on DCL1
	 * to re-attach PPP, but our AT channel was already switched to DCL2.
	 */

	/* Update the AT channel after answering "OK" on the current DLCI. */
	rsp_send_ok();
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	cmux.at_channel = new_at_channel;
	int ret = sm_at_host_set_pipe(ctx, cmux.dlcis[cmux.at_channel].pipe);

	if (ret) {
		LOG_ERR("Failed to switch AT host to CMUX DLCI pipe. (%d)", ret);
		return ret;
	}
	if (IS_ENABLED(CONFIG_SM_PPP)) {
		/* Switch PPP pipe to where AT channel was earlier */
		struct modem_pipe *ppp_pipe = cmux.dlcis[!cmux.at_channel].pipe;

		LOG_DBG("Switching CMUX PPP channel to %d", !cmux.at_channel + 1);
		sm_at_host_release(sm_at_host_get_ctx_from(ppp_pipe));
		sm_ppp_attach(ppp_pipe);
		sm_ppp_detach_after_disconnect();
	}
	return -SILENT_AT_COMMAND_RET;
}

static int cmux_start(void)
{
	int ret;

	if (sm_cmux_is_started()) {
		ret = modem_pipe_open(cmux.uart_pipe, K_SECONDS(CONFIG_SM_MODEM_PIPE_TIMEOUT));
		if (!ret) {
			cmux.uart_pipe_open = true;
			LOG_INF("CMUX resumed.");
		}
		return ret;
	}

	/* Get the UART pipe (already open and attached to AT host) */
	cmux.uart_pipe = sm_uart_pipe_get();
	if (!cmux.uart_pipe) {
		return -ENODEV;
	}

	/* Switch AT host to CMUX DLCI pipe */
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	ret = sm_at_host_set_pipe(ctx, cmux.dlcis[cmux.at_channel].pipe);
	if (ret) {
		LOG_ERR("Failed to switch AT host to CMUX DLCI pipe. (%d)", ret);
		return ret;
	}

	/* Attach CMUX to UART pipe (AT host will be detached by transition) */
	ret = modem_cmux_attach(&cmux.instance, cmux.uart_pipe);
	if (ret) {
		LOG_ERR("Failed to attach CMUX to UART pipe. (%d)", ret);
		return ret;
	}

	/* Pipe is already open, just mark it */
	cmux.uart_pipe_open = true;

	return 0;
}

SM_AT_CMD_CUSTOM(xcmux, "AT#XCMUX", handle_at_xcmux);
static int handle_at_xcmux(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	unsigned int at_dlci;
	int ret;

	if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		rsp_send("\r\n#XCMUX: %u,%u\r\n", cmux.at_channel + 1,
			 CONFIG_SM_CMUX_CHANNEL_COUNT);
		return 0;
	}
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count > 2) {
		return -EINVAL;
	}

	if (param_count == 1 && sm_cmux_is_started()) {
		return -EALREADY;
	}

	if (param_count == 2) {
		ret = at_parser_num_get(parser, 1, &at_dlci);
		if (ret || (at_dlci != 1 && (!IS_ENABLED(CONFIG_SM_PPP) || at_dlci != 2))) {
			return -EINVAL;
		}
		const unsigned int at_channel = DLCI_TO_INDEX(at_dlci);

		if (IS_ENABLED(CONFIG_SM_PPP)) {
			if (ppp_is_running() && at_channel != cmux.at_channel) {
				/* The AT channel cannot be changed when PPP has a channel reserved.
				 */
				return -ENOTSUP;
			}
		}
		if (sm_cmux_is_started()) {
			return do_at_and_ppp_channel_switch(at_channel);
		}
		cmux.at_channel = at_channel;
	}
	assign_default_channels();

	/* Respond before starting CMUX. */
	rsp_send_ok();
	ret = cmux_start();
	if (ret) {
		LOG_ERR("Failed to start CMUX. (%d)", ret);
	} else {
		ret = -SILENT_AT_COMMAND_RET;
	}
	return ret;
}

SM_AT_CMD_CUSTOM(xcmuxcld, "AT#XCMUXCLD", handle_at_xcmuxcld);
static int handle_at_xcmuxcld(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			     uint32_t param_count)
{
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count != 1) {
		return -EINVAL;
	}

	if (!sm_cmux_is_started() || !cmux.uart_pipe_open) {
		return -EALREADY;
	}

	/* Respond before stopping CMUX. */
	rsp_send_ok();
	/* Return to AT command mode */
	k_work_reschedule_for_queue(&sm_work_q, &cmux.stop_work, STOP_DELAY);

	return -SILENT_AT_COMMAND_RET;
}

SM_AT_CMD_CUSTOM(atcmux, "AT+CMUX", handle_at_cmux);
static int handle_at_cmux(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	/* AT+CMUX follows the 3GPP TS 27.010 specification.
	 *
	 * Only following commands are supported:
	 * AT+CMUX=0       (basic mode)
	 * AT+CMUX=0,0     (basic mode, subset 0)
	 * AT+CMUX?        (read current configuration)
	 * AT+CMUX=?       (list supported parameter ranges)
	 * All other parameter combinations are rejected with an error response.
	 */
	unsigned int mode;
	unsigned int subset;
	int ret;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_TEST:
		/* Report supported ranges: only mode 0, subset 0 */
		rsp_send("\r\n+CMUX: (0),(0)\r\n");
		return 0;

	case AT_PARSER_CMD_TYPE_READ:
		/* Report current (and only) configuration */
		rsp_send("\r\n+CMUX: 0,0\r\n");
		return 0;

	case AT_PARSER_CMD_TYPE_SET:
		if (param_count < 2 || param_count > 3) {
			return -EINVAL;
		}

		ret = at_parser_num_get(parser, 1, &mode);
		if (ret || mode != 0) {
			return -EINVAL;
		}

		if (param_count == 3) {
			ret = at_parser_num_get(parser, 2, &subset);
			if (ret || subset != 0) {
				return -EINVAL;
			}
		}

		if (sm_cmux_is_started()) {
			return -EALREADY;
		}

		/* Respond before starting CMUX. */
		rsp_send_ok();
		ret = cmux_start();
		if (ret) {
			LOG_ERR("Failed to start CMUX. (%d)", ret);
		} else {
			ret = -SILENT_AT_COMMAND_RET;
		}
		return ret;

	default:
		return -EINVAL;
	}
}

#if CONFIG_SM_MODEM_TRACE_BACKEND_CMUX

SM_AT_CMD_CUSTOM(xcmuxtrace, "AT#XCMUXTRACE", handle_at_xcmuxtrace);
static int handle_at_xcmuxtrace(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	struct modem_pipe *pipe;

	if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XCMUXTRACE: (1 ... %d)\r\n", CONFIG_SM_CMUX_CHANNEL_COUNT);
		return 0;
	}
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count > 2) {
		return -EINVAL;
	}

	if (param_count == 2) {
		int ch;
		int ret = at_parser_num_get(parser, 1, &ch);

		if (ret || (ch < 2 || ch >= CONFIG_SM_CMUX_CHANNEL_COUNT)) {
			return -EINVAL;
		}
		pipe = sm_cmux_get_dlci(ch);
	} else {
		pipe = sm_at_host_get_current_pipe();
	}

	if (!pipe) {
		return -ENODEV;
	}
	rsp_send_ok();
	sm_trace_backend_detach();
	sm_at_host_release(sm_at_host_get_ctx_from(pipe));
	sm_trace_backend_attach(pipe);

	return -SILENT_AT_COMMAND_RET;
}

#endif
