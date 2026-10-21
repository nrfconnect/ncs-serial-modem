/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Hardware-in-the-loop test for CMUX control frame latency on a real nRF91 Serial Modem.
 *
 * This board acts as the CMUX host, connected over UART in the same way as the sm_ppp_shell
 * sample. The Zephyr modem_cmux module is attached directly to the UART backend pipe.
 *
 * The test reproduces the host behavior reported in EXTG-287:
 * 1. AT+CFUN=0 is sent on the AT channel.
 * 2. AT+CFUN=0 is re-sent before the first one has completed.
 * 3. As soon as the first OK is received, CMUX is closed down with the multiplexer close down
 *    (CLD) command.
 *
 * A second scenario sends AT+CFUN=0 and closes down CMUX immediately, without waiting for
 * any response.
 *
 * Serial Modem executes the second AT+CFUN=0 while the CLD is pending. The test measures the
 * time from CLD to the CLD response, which disconnects the host side, and checks that it stays
 * below the configured limit.
 *
 * The host re-transmits CLD every T1 and disconnects locally after N2 retries even without a
 * response, so a latency at that ceiling means that Serial Modem did not respond at all.
 * Every received frame, including the CLD responses, is visible in the CMUX debug log.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/modem/backend/uart.h>
#include <zephyr/modem/cmux.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(cmux_cld_hil, LOG_LEVEL_INF);

#define UART_BUF_SIZE		2048
#define DLCI_BUF_SIZE		2048
#define AT_LINE_MAX		256
#define CMUX_CONNECT_ATTEMPTS	3
#define AT_PROBE_TIMEOUT	K_MSEC(500)
#define AT_MODE_TIMEOUT		K_SECONDS(1)
#define AT_CMD_TIMEOUT		K_SECONDS(1)
#define AT_TX_TIMEOUT		K_SECONDS(1)
#define CFUN_TIMEOUT		K_SECONDS(60)
#define DLCI_OPEN_TIMEOUT	K_SECONDS(1)
#define CLD_TIMEOUT		K_SECONDS(1)
#define DRAIN_QUIET_TIME	K_MSEC(50)
#define CEREG_PREFIX		"+CEREG: "
#define CFUN_PREFIX		"+CFUN: "
#define CEREG_REGISTERED_HOME	1
#define CEREG_REGISTERED_ROAM	5

struct at_channel {
	struct modem_pipe *pipe;
	struct k_sem rx_sem;
	char line[AT_LINE_MAX];
	size_t line_len;
};

struct cld_result {
	int64_t cfun_sent_ms;
	int64_t ok_ms;
	int64_t cld_ms;
	int64_t disconnected_ms;
};

/* Start/end flag of a CMUX basic option frame */
#define CMUX_SOF 0xF9

#define XCMUX_CMD "AT#XCMUX=" STRINGIFY(CONFIG_TEST_CMUX_AT_DLCI) "\r\n"

/* SABM on the control channel (DLCI 0) and the UA response to it. */
static const uint8_t cmux_sabm_dlci0[] = {CMUX_SOF, 0x03, 0x3F, 0x01, 0x1C, CMUX_SOF};
static const uint8_t cmux_ua_dlci0[] = {CMUX_SOF, 0x03, 0x73, 0x01, 0xD7, CMUX_SOF};
/* Close down command (CLD) on the control channel and its response. */
static const uint8_t cmux_cld[] = {CMUX_SOF, 0x03, 0xEF, 0x05, 0xC3, 0x01, 0xF2, CMUX_SOF};
static const uint8_t cmux_cld_rsp[] = {CMUX_SOF, 0x03, 0xEF, 0x05, 0xC1, 0x01, 0xF2, CMUX_SOF};
static const uint8_t at_ok_rsp[] = "\r\nOK\r\n";

static const struct device *const sm_uart = DEVICE_DT_GET(DT_ALIAS(sm_uart));

static struct modem_backend_uart uart_backend;
static uint8_t uart_receive_buf[UART_BUF_SIZE];
static uint8_t uart_transmit_buf[UART_BUF_SIZE];
static struct modem_pipe *uart_pipe;

static struct modem_cmux cmux;
static uint8_t cmux_receive_buf[MODEM_CMUX_WORK_BUFFER_SIZE];
static uint8_t cmux_transmit_buf[MODEM_CMUX_WORK_BUFFER_SIZE];

static struct modem_cmux_dlci at_dlci;
static struct modem_pipe *at_dlci_pipe;
static uint8_t at_dlci_receive_buf[DLCI_BUF_SIZE];

static struct at_channel at_chan;

static K_SEM_DEFINE(cmux_disconnected_sem, 0, 1);
static int64_t cmux_disconnected_ms;

static void cmux_callback(struct modem_cmux *cmux, enum modem_cmux_event event, void *user_data)
{
	ARG_UNUSED(cmux);
	ARG_UNUSED(user_data);

	if (event == MODEM_CMUX_EVENT_DISCONNECTED) {
		cmux_disconnected_ms = k_uptime_get();
		k_sem_give(&cmux_disconnected_sem);
	}
}

/* Pipe callbacks are invoked with the pipe spinlock held; only signal from here. */
static void at_pipe_callback(struct modem_pipe *pipe, enum modem_pipe_event event,
			     void *user_data)
{
	struct at_channel *chan = user_data;

	ARG_UNUSED(pipe);

	if (event == MODEM_PIPE_EVENT_RECEIVE_READY) {
		k_sem_give(&chan->rx_sem);
	}
}

static void at_channel_attach(struct at_channel *chan, struct modem_pipe *pipe)
{
	chan->pipe = pipe;
	chan->line_len = 0;
	k_sem_reset(&chan->rx_sem);
	modem_pipe_attach(pipe, at_pipe_callback, chan);
}

/*
 * Discard received data until nothing has been received for @p quiet.
 * Returns true if a CMUX frame start flag was among the discarded bytes.
 */
static bool at_drain(struct at_channel *chan, k_timeout_t quiet)
{
	uint8_t buf[64];
	size_t dropped = 0;
	bool frame_seen = false;
	int ret;

	do {
		while ((ret = modem_pipe_receive(chan->pipe, buf, sizeof(buf))) > 0) {
			dropped += ret;
			frame_seen |= memchr(buf, CMUX_SOF, ret) != NULL;
		}
	} while (!ret && !k_sem_take(&chan->rx_sem, quiet));

	chan->line_len = 0;
	if (dropped) {
		LOG_WRN("Dropped %zu stale bytes%s", dropped,
			frame_seen ? ", CMUX frames are leaking" : "");
	}

	return frame_seen;
}

static int pipe_write_all(struct modem_pipe *pipe, const uint8_t *data, size_t len)
{
	const k_timepoint_t end = sys_timepoint_calc(AT_TX_TIMEOUT);
	size_t sent = 0;
	int ret;

	while (sent < len) {
		ret = modem_pipe_transmit(pipe, &data[sent], len - sent);
		if (ret < 0) {
			return ret;
		}
		if (ret == 0) {
			if (sys_timepoint_expired(end)) {
				return -EAGAIN;
			}
			k_sleep(K_MSEC(1));
			continue;
		}
		sent += ret;
	}

	return 0;
}

static int at_send(struct at_channel *chan, const char *cmd)
{
	LOG_INF("TX: %s", cmd);

	int ret = pipe_write_all(chan->pipe, (const uint8_t *)cmd, strlen(cmd));

	return ret ? ret : pipe_write_all(chan->pipe, (const uint8_t *)"\r\n", 2);
}

/* Receive exactly @p len bytes and compare them to @p expected. */
static int at_expect_raw(struct at_channel *chan, const uint8_t *expected, size_t len,
			 k_timeout_t timeout)
{
	const k_timepoint_t end = sys_timepoint_calc(timeout);
	uint8_t c;
	int ret;

	for (size_t i = 0; i < len;) {
		ret = modem_pipe_receive(chan->pipe, &c, sizeof(c));
		if (ret < 0) {
			return ret;
		}
		if (ret == 0) {
			if (k_sem_take(&chan->rx_sem, sys_timepoint_timeout(end))) {
				LOG_ERR("Timeout, received %zu of %zu bytes", i, len);
				return -EAGAIN;
			}
			continue;
		}
		if (c != expected[i]) {
			LOG_ERR("Byte %zu: expected 0x%02x, received 0x%02x", i, expected[i], c);
			return -EBADMSG;
		}
		i++;
	}

	return 0;
}

/*
 * Read lines until one starts with @p prefix. A line is complete only when its terminating
 * "\r\n" has been consumed, so nothing of it is left in the pipe. Returns 0 on match, -EIO on
 * an error response and -EAGAIN on timeout. The matching line is left in chan->line.
 */
static int at_wait_for(struct at_channel *chan, const char *prefix, k_timeout_t timeout,
		       int64_t *matched_ms)
{
	const k_timepoint_t end = sys_timepoint_calc(timeout);
	uint8_t c;
	int ret;

	while (true) {
		ret = modem_pipe_receive(chan->pipe, &c, sizeof(c));
		if (ret < 0) {
			return ret;
		}
		if (ret == 0) {
			if (k_sem_take(&chan->rx_sem, sys_timepoint_timeout(end))) {
				return -EAGAIN;
			}
			continue;
		}
		if (c == '\r') {
			continue;
		}
		if (c != '\n') {
			if (chan->line_len < sizeof(chan->line) - 1) {
				chan->line[chan->line_len++] = c;
			}
			continue;
		}
		if (!chan->line_len) {
			continue;
		}

		chan->line[chan->line_len] = '\0';
		chan->line_len = 0;
		LOG_INF("RX: %s", chan->line);

		if (!strncmp(chan->line, prefix, strlen(prefix))) {
			if (matched_ms) {
				*matched_ms = k_uptime_get();
			}
			return 0;
		}
		if (strstr(chan->line, "ERROR")) {
			return -EIO;
		}
	}
}

static int at_cmd(struct at_channel *chan, const char *cmd, k_timeout_t timeout)
{
	int ret = at_send(chan, cmd);

	return ret ? ret : at_wait_for(chan, "OK", timeout, NULL);
}

static int at_probe(struct at_channel *chan, k_timeout_t timeout)
{
	const k_timepoint_t end = sys_timepoint_calc(timeout);

	do {
		if (!at_cmd(chan, "AT", AT_PROBE_TIMEOUT)) {
			return 0;
		}
	} while (!sys_timepoint_expired(end));

	return -ETIMEDOUT;
}

/*
 * A retried AT+CFUN=0 may answer after CMUX has been closed. Serial Modem executes AT
 * commands in order, so the +CFUN read response comes after any such leftover response.
 */
static int at_sync(struct at_channel *chan)
{
	int ret = at_send(chan, "AT+CFUN?");

	if (!ret) {
		ret = at_wait_for(chan, CFUN_PREFIX, CFUN_TIMEOUT, NULL);
	}
	return ret ? ret : at_wait_for(chan, "OK", AT_CMD_TIMEOUT, NULL);
}

/* (Re)initialize the CMUX instance; a released DLCI is not reset by modem_cmux. */
static void cmux_init(void)
{
	const struct modem_cmux_config cmux_config = {
		.callback = cmux_callback,
		.receive_buf = cmux_receive_buf,
		.receive_buf_size = sizeof(cmux_receive_buf),
		.transmit_buf = cmux_transmit_buf,
		.transmit_buf_size = sizeof(cmux_transmit_buf),
	};
	const struct modem_cmux_dlci_config dlci_config = {
		.dlci_address = CONFIG_TEST_CMUX_AT_DLCI,
		.receive_buf = at_dlci_receive_buf,
		.receive_buf_size = sizeof(at_dlci_receive_buf),
	};

	modem_cmux_init(&cmux, &cmux_config);
	at_dlci_pipe = modem_cmux_dlci_init(&cmux, &at_dlci, &dlci_config);
}

/*
 * Release CMUX from the UART and talk AT directly over it again. When @p strict is set,
 * CMUX frames received after the CLD response fail the close down.
 */
static int cmux_stop(k_timeout_t timeout, bool strict)
{
	modem_cmux_release(&cmux);
	at_channel_attach(&at_chan, uart_pipe);
	/* Frames received after the CLD response would otherwise be parsed as AT responses. */
	if (at_drain(&at_chan, DRAIN_QUIET_TIME) && strict) {
		return -EBADMSG;
	}

	return at_probe(&at_chan, timeout);
}

/* Serial Modem may still be in CMUX mode from an earlier run; close it down if so. */
static int sm_enter_at_mode(void)
{
	at_channel_attach(&at_chan, uart_pipe);
	if (!at_probe(&at_chan, AT_MODE_TIMEOUT)) {
		return 0;
	}

	LOG_WRN("No AT response, trying to close down a stale CMUX session");
	cmux_init();
	if (!modem_cmux_attach(&cmux, uart_pipe) && !modem_cmux_connect(&cmux)) {
		(void)modem_cmux_disconnect(&cmux);
	}
	return cmux_stop(AT_MODE_TIMEOUT, false);
}

/* Attach the host CMUX to the UART, connect it and open the AT DLCI. */
static int cmux_connect(void)
{
	int ret;

	cmux_init();
	ret = modem_cmux_attach(&cmux, uart_pipe);
	if (ret) {
		return ret;
	}

	for (int i = 0; i < CMUX_CONNECT_ATTEMPTS; i++) {
		ret = modem_cmux_connect(&cmux);
		if (!ret) {
			break;
		}
	}
	if (ret) {
		return ret;
	}

	at_channel_attach(&at_chan, at_dlci_pipe);
	ret = modem_pipe_open(at_dlci_pipe, DLCI_OPEN_TIMEOUT);
	if (ret) {
		return ret;
	}

	return at_probe(&at_chan, AT_MODE_TIMEOUT);
}

static int cmux_start(void)
{
	int ret = at_cmd(&at_chan, "AT#XCMUX=" STRINGIFY(CONFIG_TEST_CMUX_AT_DLCI), AT_CMD_TIMEOUT);

	return ret ? ret : cmux_connect();
}

/* Activate LTE so that AT+CFUN=0 involves a network detach and takes several seconds. */
static void modem_activate(void)
{
	const k_timepoint_t end = sys_timepoint_calc(K_SECONDS(CONFIG_TEST_CMUX_NETWORK_WAIT_S));
	long status;

	zassert_ok(at_cmd(&at_chan, "AT+CEREG=1", AT_CMD_TIMEOUT));
	zassert_ok(at_cmd(&at_chan, "AT+CFUN=1", AT_CMD_TIMEOUT));

	while (!at_wait_for(&at_chan, CEREG_PREFIX, sys_timepoint_timeout(end), NULL)) {
		status = strtol(&at_chan.line[strlen(CEREG_PREFIX)], NULL, 10);
		if (status == CEREG_REGISTERED_HOME || status == CEREG_REGISTERED_ROAM) {
			LOG_INF("Registered to network");
			return;
		}
	}
	LOG_WRN("Not registered within %d s, continuing", CONFIG_TEST_CMUX_NETWORK_WAIT_S);
}

/* Close down CMUX with CLD and wait until the host side is disconnected. */
static void cmux_close_down(struct cld_result *res)
{
	k_sem_reset(&cmux_disconnected_sem);

	res->cld_ms = k_uptime_get();
	zassert_ok(modem_cmux_disconnect_async(&cmux));
	zassert_ok(k_sem_take(&cmux_disconnected_sem, CLD_TIMEOUT), "CMUX was not disconnected");
	res->disconnected_ms = cmux_disconnected_ms;
}

/*
 * Start CMUX, send AT+CFUN=0, optionally re-send it before the response, and close down CMUX
 * as soon as the first OK arrives. Returns -ENOEXEC if the first AT+CFUN=0 completed before
 * the retry could be sent.
 */
static int cfun_off_then_cld(bool retry, struct cld_result *res)
{
	int ret = 0;

	*res = (struct cld_result){0};

	zassert_ok(cmux_start(), "Failed to start CMUX");
	modem_activate();

	zassert_ok(at_send(&at_chan, "AT+CFUN=0"));
	res->cfun_sent_ms = k_uptime_get();

	if (retry) {
		ret = at_wait_for(&at_chan, "OK", K_MSEC(CONFIG_TEST_CMUX_CFUN_RETRY_DELAY_MS),
				  &res->ok_ms);
		if (!ret) {
			LOG_WRN("AT+CFUN=0 completed in %lld ms, before the retry",
				res->ok_ms - res->cfun_sent_ms);
			ret = -ENOEXEC;
		} else {
			zassert_equal(ret, -EAGAIN, "AT+CFUN=0 failed: %d", ret);
			zassert_ok(at_send(&at_chan, "AT+CFUN=0"));
			ret = 0;
		}
	}

	if (!res->ok_ms) {
		zassert_ok(at_wait_for(&at_chan, "OK", CFUN_TIMEOUT, &res->ok_ms),
			   "No response to AT+CFUN=0");
	}

	cmux_close_down(res);

	zassert_ok(cmux_stop(AT_MODE_TIMEOUT, true),
		   "Serial Modem did not return to AT command mode");
	zassert_ok(at_sync(&at_chan), "AT command mode out of sync");

	if (!ret) {
		LOG_INF("AT+CFUN=0 took %lld ms, CLD -> disconnected %lld ms",
			res->ok_ms - res->cfun_sent_ms, res->disconnected_ms - res->cld_ms);
	}
	return ret;
}

static void *suite_setup(void)
{
	const struct modem_backend_uart_config uart_config = {
		.uart = sm_uart,
		.receive_buf = uart_receive_buf,
		.receive_buf_size = sizeof(uart_receive_buf),
		.transmit_buf = uart_transmit_buf,
		.transmit_buf_size = sizeof(uart_transmit_buf),
	};

	zassert_true(device_is_ready(sm_uart), "Serial Modem UART not ready");

	k_sem_init(&at_chan.rx_sem, 0, 1);
	uart_pipe = modem_backend_uart_init(&uart_backend, &uart_config);

	zassert_ok(modem_pipe_open(uart_pipe, K_SECONDS(1)));
	zassert_ok(sm_enter_at_mode(), "Serial Modem does not respond to AT");

	return NULL;
}

/* Return Serial Modem to AT command mode if a test failed with CMUX running. */
static void after_each(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)modem_cmux_disconnect(&cmux);
	modem_cmux_release(&cmux);
	if (sm_enter_at_mode()) {
		LOG_WRN("Serial Modem did not return to AT command mode");
	}
}

static void suite_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	modem_pipe_close(uart_pipe, K_SECONDS(1));
}

ZTEST(cmux_cld_hil, test_cld_after_single_cfun)
{
	struct cld_result res;

	zassert_ok(cfun_off_then_cld(false, &res));
	zassert_true(res.disconnected_ms - res.cld_ms <= CONFIG_TEST_CMUX_CLD_MAX_LATENCY_MS,
		     "CLD took %lld ms", res.disconnected_ms - res.cld_ms);
}

ZTEST(cmux_cld_hil, test_cld_while_retried_cfun_executes)
{
	struct cld_result res;
	int64_t worst_ms = 0;
	int exercised = 0;

	for (int i = 0; i < CONFIG_TEST_CMUX_ITERATIONS; i++) {
		LOG_INF("Iteration %d/%d", i + 1, CONFIG_TEST_CMUX_ITERATIONS);

		if (cfun_off_then_cld(true, &res) == -ENOEXEC) {
			continue;
		}
		exercised++;
		worst_ms = MAX(worst_ms, res.disconnected_ms - res.cld_ms);
	}

	if (!exercised) {
		ztest_test_skip();
	}

	LOG_INF("Worst CLD -> disconnected %lld ms over %d cycles", worst_ms, exercised);
	zassert_true(worst_ms <= CONFIG_TEST_CMUX_CLD_MAX_LATENCY_MS,
		     "CLD took %lld ms while AT+CFUN=0 was executing", worst_ms);
}

/*
 * Close down CMUX right after sending AT+CFUN=0, without waiting for its response. Also
 * report when Serial Modem accepts AT commands on the UART again, as the return to AT command
 * mode may also wait for the AT+CFUN=0 to complete.
 */
ZTEST(cmux_cld_hil, test_cld_right_after_cfun)
{
	struct cld_result res = {0};
	int64_t cld_latency_ms;
	int64_t at_mode_ms;

	zassert_ok(cmux_start(), "Failed to start CMUX");
	modem_activate();

	zassert_ok(at_send(&at_chan, "AT+CFUN=0"));
	res.cfun_sent_ms = k_uptime_get();
	cmux_close_down(&res);
	cld_latency_ms = res.disconnected_ms - res.cld_ms;

	zassert_ok(cmux_stop(CFUN_TIMEOUT, true), "Serial Modem did not return to AT command mode");
	at_mode_ms = k_uptime_get() - res.cld_ms;
	k_sleep(K_MSEC(100));
	zassert_ok(at_sync(&at_chan), "AT command mode out of sync");

	LOG_INF("CLD -> disconnected %lld ms, CLD -> AT command mode %lld ms", cld_latency_ms,
		at_mode_ms);
	zassert_true(cld_latency_ms <= CONFIG_TEST_CMUX_CLD_MAX_LATENCY_MS,
		     "CLD took %lld ms while AT+CFUN=0 was executing", cld_latency_ms);
}

/*
 * Start CMUX with a pipelined frame, then close it down and return to AT mode.
 *
 * Send AT#XCMUX followed by the first CMUX frame without waiting for OK. The frame may be
 * split at @p split bytes, in which case the rest is sent after the OK has been received.
 * Before the DLCI is open nothing on the UART is AT, so Serial Modem must answer with OK,
 * in order, followed by the UA of the control channel. CMUX is then closed down with a raw
 * CLD, as the host CMUX would send another SABM, which Serial Modem does not answer once
 * the control channel is connected.
 */
static int cmux_pipelined_start(size_t split)
{
	uint8_t tx[sizeof(XCMUX_CMD) - 1 + sizeof(cmux_sabm_dlci0)];
	const size_t cmd_len = sizeof(XCMUX_CMD) - 1;
	int ret;

	memcpy(tx, XCMUX_CMD, cmd_len);
	memcpy(&tx[cmd_len], cmux_sabm_dlci0, sizeof(cmux_sabm_dlci0));

	at_channel_attach(&at_chan, uart_pipe);
	if (!split) {
		ret = pipe_write_all(uart_pipe, tx, sizeof(tx));
	} else {
		ret = pipe_write_all(uart_pipe, tx, cmd_len + split);
	}
	if (ret) {
		LOG_ERR("pipe_write_all failed (%d)", ret);
		return ret;
	}

	ret = at_expect_raw(&at_chan, at_ok_rsp, sizeof(at_ok_rsp) - 1, AT_CMD_TIMEOUT);
	if (ret) {
		LOG_ERR("No OK after AT#XCMUX (%d)", ret);
		return ret;
	}

	if (split) {
		ret = pipe_write_all(uart_pipe, &tx[cmd_len + split],
				     sizeof(tx) - cmd_len - split);
		if (ret) {
			LOG_ERR("pipe_write_all failed (%d)", ret);
			return ret;
		}
	}

	ret = at_expect_raw(&at_chan, cmux_ua_dlci0, sizeof(cmux_ua_dlci0), AT_CMD_TIMEOUT);
	if (ret) {
		LOG_ERR("No UA for the control channel SABM (%d)", ret);
		return ret;
	}

	ret = pipe_write_all(uart_pipe, cmux_cld, sizeof(cmux_cld));
	if (ret) {
		return ret;
	}

	ret = at_expect_raw(&at_chan, cmux_cld_rsp, sizeof(cmux_cld_rsp), AT_CMD_TIMEOUT);
	if (ret) {
		LOG_ERR("No CLD response (%d)", ret);
		return ret;
	}

	if (at_drain(&at_chan, DRAIN_QUIET_TIME)) {
		return -EBADMSG;
	}

	return at_probe(&at_chan, AT_MODE_TIMEOUT);
}

static void cmux_pipelined_start_loop(size_t split)
{
	for (int i = 0; i < CONFIG_TEST_CMUX_PIPELINE_ITERATIONS; i++) {
		LOG_INF("Iteration %d/%d", i + 1, CONFIG_TEST_CMUX_PIPELINE_ITERATIONS);

		zassert_ok(cmux_pipelined_start(split), "Pipelined CMUX start failed, iteration %d",
			   i);
	}
}

/* The first CMUX frame arrives in the same UART transfer as AT#XCMUX. */
ZTEST(cmux_cld_hil, test_cmux_frame_pipelined_with_xcmux)
{
	cmux_pipelined_start_loop(0);
}

/* AT#XCMUX is followed by a partial frame, the rest is sent after the OK. */
ZTEST(cmux_cld_hil, test_cmux_split_frame_pipelined_with_xcmux)
{
	cmux_pipelined_start_loop(3);
}

ZTEST_SUITE(cmux_cld_hil, NULL, suite_setup, NULL, after_each, suite_teardown);
