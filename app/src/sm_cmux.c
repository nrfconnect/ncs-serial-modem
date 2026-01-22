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
	} dlcis[CHANNEL_COUNT];
	/* Index of the DLCI used for AT communication; defaults to 0. */
	unsigned int at_channel;
	unsigned int requested_at_channel;

	/* Incoming data for DLCI's. */
	atomic_t dlci_channel_rx;
	struct k_work rx_work;

	/* Outgoing data for AT DLCI. */
	struct sm_urc_ctx *urc_ctx;
	struct k_work nonblock_tx_work;
	struct k_work_delayable stop_work;
	struct k_sem tx_sem;
} cmux;

static void rx_work_fn(struct k_work *work)
{
	static uint8_t recv_buf[RECV_BUF_LEN];
	int ret;
	bool is_at;
	bool ignored;

	for (int i = 0; i < ARRAY_SIZE(cmux.dlcis); ++i) {
		if (atomic_test_and_clear_bit(&cmux.dlci_channel_rx, i)) {
			/* Incoming data for DLCI. */
			is_at = (i == cmux.at_channel);

			ret = modem_pipe_receive(cmux.dlcis[i].pipe, recv_buf, sizeof(recv_buf));
			if (ret < 0) {
				LOG_ERR("DLCI %u%s failed modem_pipe_receive. (%d)",
					INDEX_TO_DLCI(i), is_at ? " (AT)" : "", ret);
				continue;
			}

			if (!is_at) {
				LOG_INF("DLCI %u discarding %u bytes of data.", INDEX_TO_DLCI(i),
					ret);
				continue;
			}

			LOG_DBG("DLCI %u (AT) received %u bytes of data.", INDEX_TO_DLCI(i), ret);
			sm_at_receive(recv_buf, ret, &ignored);
		}
	}
}

static void dlci_pipe_event_handler(struct modem_pipe *pipe,
				    enum modem_pipe_event event, void *user_data)
{
	const struct cmux_dlci *const dlci = user_data;
	bool is_at = (ARRAY_INDEX(cmux.dlcis, dlci) == cmux.at_channel);

	switch (event) {
		/* The events of the DLCIs other than that of the AT channel
		 * are received here when they haven't been attached to
		 * by their respective implementations.
		 */
	case MODEM_PIPE_EVENT_OPENED:
		LOG_INF("DLCI %u%s opened.", dlci->address, is_at ? " (AT)" : "");
		k_work_submit_to_queue(&sm_work_q, &cmux.nonblock_tx_work);
		break;

	case MODEM_PIPE_EVENT_CLOSED:
		LOG_INF("DLCI %u%s closed.", dlci->address, is_at ? " (AT)" : "");
		if (is_at) {
			k_sem_give(&cmux.tx_sem);
		}
		break;

	case MODEM_PIPE_EVENT_RECEIVE_READY:
		LOG_DBG("DLCI %u%s receive ready.", dlci->address, is_at ? " (AT)" : "");
		atomic_or(&cmux.dlci_channel_rx, BIT(DLCI_TO_INDEX(dlci->address)));
		k_work_submit_to_queue(&sm_work_q, &cmux.rx_work);
		break;

	case MODEM_PIPE_EVENT_TRANSMIT_IDLE:
		if (is_at) {
			k_sem_give(&cmux.tx_sem);
		}
		break;
	}
}

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

	modem_pipe_attach(dlci->pipe, dlci_pipe_event_handler, dlci);
}

static int cmux_write_at_channel_block(const uint8_t *data, size_t *len)
{
	size_t sent = 0;
	int ret;

	while (sent < *len) {
		ret = modem_pipe_transmit(cmux.dlcis[cmux.at_channel].pipe, data + sent,
					  *len - sent);
		if (ret < 0) {
			LOG_ERR("DLCI %u (AT) transmit failed (%d).",
				INDEX_TO_DLCI(cmux.at_channel), ret);
			return ret;
		} else if (ret == 0) {
			if (cmux.dlcis[cmux.at_channel].instance.state !=
			    MODEM_CMUX_DLCI_STATE_OPEN) {
				/* Drop URC when pipe is closed */
				return 0;
			}
			/* Pipe TX buffer full. Wait for transmit idle event. */
			k_sem_take(&cmux.tx_sem, K_FOREVER);
		} else {
			sent += ret;
		}
	}

	if (cmux.requested_at_channel != UINT_MAX) {
		cmux.at_channel = cmux.requested_at_channel;
		cmux.requested_at_channel = UINT_MAX;
		LOG_INF("DLCI %u (AT) updated.", INDEX_TO_DLCI(cmux.at_channel));
	}

	*len = sent;
	return 0;
}

static void nonblock_tx_work_fn(struct k_work *)
{
	static struct sm_event_callback event_cb = {
		.cb = nonblock_tx_work_fn
	};

	uint8_t *data;
	size_t len;
	int err;
	struct sm_urc_ctx *uc = cmux.urc_ctx; /* Take a local copy. */

	if (uc == NULL) {
		LOG_ERR("No URC context");
		return;
	}

	if (sm_at_host_echo_urc_delay()) {
		LOG_DBG("Defer URC processing until %s", "echo delay has elapsed");
		sm_at_host_register_event_cb(&event_cb, SM_EVENT_URC);
		return;
	}

	if (!in_at_mode()) {
		LOG_DBG("Defer URC processing until %s", "in AT mode");
		sm_at_host_register_event_cb(&event_cb, SM_EVENT_AT_MODE);
		return;
	}

	/* Do not lock the URC mutex. */
	do {
		len = ring_buf_get_claim(&uc->rb, &data, ring_buf_capacity_get(&uc->rb));
		err = cmux_write_at_channel_block(data, &len);
		ring_buf_get_finish(&uc->rb, len);

	} while (!ring_buf_is_empty(&uc->rb) && !err);

	if (err) {
		LOG_DBG("URC transmit failed (%d). %d bytes unsent.", err,
			ring_buf_size_get(&uc->rb));
	}
}

static int cmux_write_at_channel_nonblock(const uint8_t *data, size_t len)
{
	int ret = 0;
	struct sm_urc_ctx *uc = cmux.urc_ctx; /* Take a local copy. */

	if (uc == NULL) {
		LOG_ERR("No URC context");
		return -EFAULT;
	}

	/* Lock to prevent concurrent writes. */
	k_mutex_lock(&uc->mutex, K_FOREVER);

	if (ring_buf_space_get(&uc->rb) >= len) {
		ring_buf_put(&uc->rb, data, len);
	} else {
		LOG_WRN("URC buf overflow, dropping %u bytes.", len);
		ret = -ENOBUFS;
	}

	k_mutex_unlock(&uc->mutex);

	k_work_submit_to_queue(&sm_work_q, &cmux.nonblock_tx_work);

	return ret;
}

static int cmux_write_at_channel(const uint8_t *data, size_t len, bool urc)
{
	int ret;

	/* To process, CMUX needs system work queue to be able to run.
	 * Send only from Serial Modem work queue to guarantee URC ordering.
	 */
	if (k_current_get() == k_work_queue_thread_get(&sm_work_q) && !urc) {
		ret = cmux_write_at_channel_block(data, &len);
	} else {
		/* In other contexts, we buffer until Serial Modem work queue becomes available. */
		ret = cmux_write_at_channel_nonblock(data, len);
	}

	return ret;
}

static void close_pipe(struct modem_pipe **pipe)
{
	if (*pipe) {
		modem_pipe_close_async(*pipe);
		modem_pipe_release(*pipe);
		*pipe = NULL;
	}
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

	cmux.dlci_channel_rx = ATOMIC_INIT(0);
	k_work_init(&cmux.rx_work, rx_work_fn);

	k_sem_init(&cmux.tx_sem, 1, 1);
	k_work_init(&cmux.nonblock_tx_work, nonblock_tx_work_fn);
	k_work_init_delayable(&cmux.stop_work, stop_work_fn);

	cmux.at_channel = 0;
	cmux.requested_at_channel = UINT_MAX;
	return 0;
}
SYS_INIT(sm_cmux_init, APPLICATION, 0);

static void stop_work_fn(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	/* Will stop the UART when calling the close_pipe() function. */
	if (cmux_is_started()) {
		modem_cmux_release(&cmux.instance);

		close_pipe(&cmux.uart_pipe);
		cmux.uart_pipe_open = false;

		for (size_t i = 0; i != ARRAY_SIZE(cmux.dlcis); ++i) {
			close_pipe(&cmux.dlcis[i].pipe);
		}
		sm_at_host_urc_ctx_release(cmux.urc_ctx, SM_URC_OWNER_CMUX);
	}

	err = sm_uart_handler_enable();
	if (err) {
		LOG_ERR("Failed to enable UART handler (%d).", err);
	}

	sm_cmux_init();
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

struct modem_pipe *sm_cmux_reserve(enum cmux_channel channel)
{
	/* Return the channel's pipe. The requesting module may attach to it,
	 * after which this pipe's events and data won't be received here anymore
	 * until the channel is released (below) and we attach back to the pipe.
	 */
	return cmux_get_dlci(channel)->pipe;
}

void sm_cmux_release(enum cmux_channel channel)
{
	struct cmux_dlci *dlci = cmux_get_dlci(channel);

	/* When PPP is stopped from first DLCI, move AT channel there.
	 * First open DLCI should always be the AT channel.
	 */
	if (channel == CMUX_PPP_CHANNEL && cmux.at_channel != 0) {
		cmux.at_channel = 0;
	}
	modem_pipe_attach(dlci->pipe, dlci_pipe_event_handler, dlci);
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

	{
		cmux.uart_pipe = sm_uart_pipe_init(cmux_write_at_channel);
		if (!cmux.uart_pipe) {
			return -ENODEV;
		}
	}

	cmux.urc_ctx = sm_at_host_urc_ctx_acquire(SM_URC_OWNER_CMUX);
	if (!cmux.urc_ctx) {
		close_pipe(&cmux.uart_pipe);
		return -EFAULT;
	}

	ret = modem_cmux_attach(&cmux.instance, cmux.uart_pipe);
	if (ret) {
		return ret;
	}

	ret = modem_pipe_open(cmux.uart_pipe, K_SECONDS(CONFIG_SM_MODEM_PIPE_TIMEOUT));
	if (!ret) {
		cmux.uart_pipe_open = true;
	}
	return ret;
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

#if defined(CONFIG_SM_PPP)
		if (!sm_ppp_is_stopped() && at_channel != cmux.at_channel) {
			/* The AT channel cannot be changed when PPP has a channel reserved. */
			return -ENOTSUP;
		}
#endif
		if (cmux_is_started()) {
			/* Update the AT channel after answering "OK" on the current DLCI. */
			cmux.requested_at_channel = at_channel;
			rsp_send_ok();
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
		ret = -SILENT_AT_CMUX_COMMAND_RET;
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
