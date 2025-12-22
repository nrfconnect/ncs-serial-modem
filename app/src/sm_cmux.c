/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "sm_cmux.h"
#include "sm_at_host.h"
#if defined(CONFIG_SM_PPP)
#include "sm_ppp.h"
#endif
#include "sm_util.h"
#include "sm_uart_handler.h"
#include <zephyr/logging/log.h>
#include <zephyr/modem/cmux.h>
#include <zephyr/modem/pipe.h>
#include <assert.h>

/* This makes use of part of the Zephyr modem subsystem which has a CMUX module. */
LOG_MODULE_REGISTER(sm_cmux, CONFIG_SM_LOG_LEVEL);

#define CHANNEL_COUNT (1 + CMUX_EXT_CHANNEL_COUNT)

#define RECV_BUF_LEN SM_AT_MAX_CMD_LEN
/* The CMUX module reserves some spare buffer bytes. To achieve a maximum
 * response length of SM_AT_MAX_RSP_LEN (comprising the "OK" or "ERROR"
 * that is sent separately), the transmit buffer must be made a bit bigger.
 * 49 extra bytes was manually found to allow SM_AT_MAX_RSP_LEN long responses.
 */
#define TRANSMIT_BUF_LEN (49 + SM_AT_MAX_RSP_LEN)

#define DLCI_TO_INDEX(dlci) ((dlci) - 1)
#define INDEX_TO_DLCI(index) ((index) + 1)
#define STOP_DELAY K_MSEC(10)

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
		uint8_t address;
		uint8_t receive_buf[RECV_BUF_LEN];
		struct k_work open_work;
	} dlcis[CHANNEL_COUNT];
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
	dlci->address = dlci_config.dlci_address;

	sm_at_host_attach(dlci->pipe);
}

static bool cmux_is_started(void)
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

	if (cmux_is_started()) {
		/* Causes all DLCIs to close (including AT channels)*/
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

static struct cmux_dlci *cmux_get_dlci(enum cmux_channel channel)
{
#if defined(CONFIG_SM_PPP)
	if (channel == CMUX_PPP_CHANNEL) {
		/* The first DLCI that is not the AT channel's is PPP's. */
		return &cmux.dlcis[!cmux.at_channel];
	}
#endif
#if defined(CONFIG_SM_GNSS_OUTPUT_NMEA_ON_CMUX_CHANNEL)
	if (channel == CMUX_GNSS_CHANNEL) {
		/* The last DLCI. */
		return &cmux.dlcis[CHANNEL_COUNT - 1];
	}
#endif
	assert(false);
}

static int cmux_start(void)
{
	int ret;

	if (cmux_is_started()) {
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
	int err = sm_at_host_set_pipe(ctx, cmux.dlcis[cmux.at_channel].pipe);
	if (err) {
		LOG_ERR("Failed to switch AT host to CMUX DLCI pipe. (%d)", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_SM_PPP)) {
		/* Reserve PPP channel pipe for PPP module */
		struct modem_pipe *ppp_pipe = cmux_get_dlci(CMUX_PPP_CHANNEL)->pipe;
		LOG_DBG("Reserving CMUX PPP channel pipe %p for PPP module", (void *)ppp_pipe);
		sm_at_host_release(sm_at_host_get_ctx_from(ppp_pipe));
		sm_ppp_attach(ppp_pipe);
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

SM_AT_CMD_CUSTOM(xcmux, "AT#XCMUX", handle_at_cmux);
static int handle_at_cmux(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			  uint32_t param_count)
{
	unsigned int at_dlci;
	int ret;

	if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		rsp_send("\r\n#XCMUX: %u,%u\r\n", cmux.at_channel + 1, CHANNEL_COUNT);
		return 0;
	}
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count > 2) {
		return -EINVAL;
	}

	if (param_count == 1 && cmux_is_started()) {
		return -EALREADY;
	}

	if (param_count == 2) {
		ret = at_parser_num_get(parser, 1, &at_dlci);
		if (ret || (at_dlci != 1 && (!IS_ENABLED(CONFIG_SM_PPP) || at_dlci != 2))) {
			return -EINVAL;
		}
		const unsigned int at_channel = DLCI_TO_INDEX(at_dlci);

		if (IS_ENABLED(CONFIG_SM_PPP)) {
			if (!sm_ppp_is_stopped() && at_channel != cmux.at_channel) {
				/* The AT channel cannot be changed when PPP has a channel reserved.
				 */
				return -ENOTSUP;
			}
		}
		if (cmux_is_started()) {
			/* Update the AT channel after answering "OK" on the current DLCI. */
			rsp_send_ok();
			struct sm_at_host_ctx *ctx = sm_at_host_get_current();
			cmux.at_channel = at_channel;
			int err = sm_at_host_set_pipe(ctx, cmux.dlcis[cmux.at_channel].pipe);
			if (err) {
				LOG_ERR("Failed to switch AT host to CMUX DLCI pipe. (%d)", err);
				return err;
			}
			if (IS_ENABLED(CONFIG_SM_PPP)) {
				/* Reserve PPP channel pipe for PPP module */
				struct modem_pipe *ppp_pipe = cmux_get_dlci(CMUX_PPP_CHANNEL)->pipe;
				LOG_DBG("Reserving CMUX PPP channel pipe %p for PPP module",
					(void *)ppp_pipe);
				sm_at_host_release(sm_at_host_get_ctx_from(ppp_pipe));
				sm_ppp_attach(ppp_pipe);
			}
			return -SILENT_AT_COMMAND_RET;
		}
		cmux.at_channel = at_channel;
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
}

SM_AT_CMD_CUSTOM(xcmuxcld, "AT#XCMUXCLD", handle_at_cmuxcld);
static int handle_at_cmuxcld(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t param_count)
{
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count != 1) {
		return -EINVAL;
	}

	if (!cmux_is_started() || !cmux.uart_pipe_open) {
		return -EALREADY;
	}

	/* Respond before stopping CMUX. */
	rsp_send_ok();
	/* Return to AT command mode */
	k_work_reschedule_for_queue(&sm_work_q, &cmux.stop_work, STOP_DELAY);

	return -SILENT_AT_COMMAND_RET;
}
