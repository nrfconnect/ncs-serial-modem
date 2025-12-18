/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sm_at_host.h"
#include "sm_at_fota.h"
#include "sm_at_socket.h"
#include "sm_uart_handler.h"
#include "sm_util.h"
#include "sm_ctrl_pin.h"
#include "sm_at_dfu.h"
#if defined(CONFIG_SM_PPP)
#include "sm_ppp.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

/* Added for XRESET command */
extern void final_call(void (*func)(void));
extern FUNC_NORETURN void sm_reset(void);

LOG_MODULE_REGISTER(sm_at_host, CONFIG_SM_LOG_LEVEL);

#define HEXDUMP_LIMIT    16

#define AT_XDFU_INIT_CMD "AT#XDFUINIT"
#define AT_XDFU_WRITE_CMD "AT#XDFUWRITE"
#define AT_XDFU_APPLY_CMD "AT#XDFUAPPLY"
#define AT_XRESET_CMD "AT#XRESET"

/* Operation mode variables */
enum sm_operation_mode {
	SM_AT_COMMAND_MODE,		/* AT command host or bridge */
	SM_DATA_MODE,			/* Raw data sending */
	SM_NULL_MODE			/* Discard incoming until next command */
};
static enum sm_operation_mode at_mode;
static sm_datamode_handler_t datamode_handler;
static int datamode_handler_result;
uint16_t sm_datamode_time_limit; /* Send trigger by time in data mode */
K_MUTEX_DEFINE(mutex_mode); /* Protects the operation mode variables. */
static size_t datamode_data_len; /* Expected data length in data mode. */

uint8_t sm_at_buf[CONFIG_SM_AT_BUF_SIZE + 1];
uint8_t sm_data_buf[SM_MAX_MESSAGE_SIZE];

RING_BUF_DECLARE(data_rb, CONFIG_SM_DATAMODE_BUF_SIZE);
static uint8_t quit_str_partial_match;
K_MUTEX_DEFINE(mutex_data); /* Protects the data_rb and quit_str_partial_match. */

static struct k_work raw_send_scheduled_work;

enum sm_debug_print {
	SM_DEBUG_PRINT_NONE,
	SM_DEBUG_PRINT_SHORT,
	SM_DEBUG_PRINT_FULL
};

static void echo_timer_handler(struct k_timer *timer);
static struct echo_ctx {
	bool enabled;
	struct k_timer timer;
} echo_ctx = {
	.enabled = false,
	.timer = Z_TIMER_INITIALIZER(echo_ctx.timer, echo_timer_handler, NULL),
};

static struct sm_urc_ctx urc_ctx;

/* Event callback mechanism */
static void event_work_fn(struct k_work *work);
static struct event_ctx {
	sys_slist_t event_cbs;
	struct k_work work;
	atomic_t events;
} event_ctx = {
	.event_cbs = SYS_SLIST_STATIC_INIT(&event_ctx.event_cbs),
	.work = Z_WORK_INITIALIZER(event_work_fn),
	.events = ATOMIC_INIT(0),
};

/* global functions defined in different files */
int sm_at_init(void);
void sm_at_uninit(void);

static enum sm_operation_mode get_sm_mode(void)
{
	enum sm_operation_mode mode;

	k_mutex_lock(&mutex_mode, K_FOREVER);
	mode = at_mode;
	k_mutex_unlock(&mutex_mode);

	return mode;
}

/* Lock mutex_mode, before calling. */
static bool set_sm_mode(enum sm_operation_mode mode)
{
	bool ret = false;

	if (at_mode == SM_AT_COMMAND_MODE) {
		if (mode == SM_DATA_MODE) {
			ret = true;
		}
	} else if (at_mode == SM_DATA_MODE) {
		if (mode == SM_NULL_MODE || mode == SM_AT_COMMAND_MODE) {
			ret = true;
		}
	} else if (at_mode == SM_NULL_MODE) {
		if (mode == SM_AT_COMMAND_MODE || mode == SM_NULL_MODE) {
			ret = true;
		}
	}

	if (ret) {
		LOG_DBG("SM mode changed: %d -> %d", at_mode, mode);
		at_mode = mode;
	} else {
		LOG_ERR("Failed to change SM mode: %d -> %d", at_mode, mode);
	}

	return ret;
}

static void sm_at_host_event_notify(enum sm_event event)
{
	atomic_or(&event_ctx.events, event);
	k_work_submit_to_queue(&sm_work_q, &event_ctx.work);
}

static bool exit_datamode(void)
{
	bool ret = false;

	k_mutex_lock(&mutex_mode, K_FOREVER);

	if (set_sm_mode(SM_AT_COMMAND_MODE)) {
		if (datamode_handler) {
			(void)datamode_handler(DATAMODE_EXIT, NULL, 0, SM_DATAMODE_FLAGS_NONE);
		}
		datamode_handler = NULL;
		datamode_data_len = 0;
		quit_str_partial_match = 0;

		k_mutex_lock(&mutex_data, K_FOREVER);
		ring_buf_reset(&data_rb);
		k_mutex_unlock(&mutex_data);

		if (datamode_handler_result) {
			LOG_ERR("Datamode handler error: %d", datamode_handler_result);
			datamode_handler_result = -1;
		}
		rsp_send("\r\n#XDATAMODE: %d\r\n", datamode_handler_result);
		datamode_handler_result = 0;

		sm_at_host_event_notify(SM_EVENT_AT_MODE);

		LOG_INF("Exit datamode");
		ret = true;
	}

	k_mutex_unlock(&mutex_mode);

	/* Flush the TX buffer. */
	sm_tx_write(NULL, 0, true, false);

	return ret;
}

/* Lock mutex_data, before calling. */
static void raw_send(uint8_t flags)
{
	uint8_t *data = NULL;
	int size_send, size_sent, size_all, size_finish;

	/* NOTE ring_buf_get_claim() might not return full size */
	do {
		size_all = ring_buf_size_get(&data_rb);
		size_send = ring_buf_get_claim(&data_rb, &data, CONFIG_SM_DATAMODE_BUF_SIZE);
		if (size_all != size_send) {
			flags |= SM_DATAMODE_FLAGS_MORE_DATA;
		}
		LOG_INF("Raw send: size_send: %d, data %p", size_send, (void *)data);
		if (data != NULL && size_send > 0) {

			/* Raw data sending */
			size_finish = 0;

			LOG_HEXDUMP_DBG(data, MIN(size_send, HEXDUMP_LIMIT), "RX");
			k_mutex_lock(&mutex_mode, K_FOREVER);
			if (datamode_handler && size_send > 0) {
				size_sent = datamode_handler(DATAMODE_SEND, data, size_send, flags);
				if (size_sent > 0) {
					size_finish += size_sent;
				} else if (size_sent == 0) {
					size_finish += size_send;
				} else {
					LOG_WRN("Raw send failed, %d dropped", size_send);
					size_finish += size_send;
				}
				(void)ring_buf_get_finish(&data_rb, size_finish);
			} else {
				LOG_WRN("no handler, %d dropped", size_send);
				(void)ring_buf_get_finish(&data_rb, size_send + size_finish);
			}
			k_mutex_unlock(&mutex_mode);
		} else {
			break;
		}
	} while (true);
}

/* Lock mutex_data, before calling. */
static void write_data_buf(const uint8_t *buf, size_t len)
{
	size_t ret;
	size_t index = 0;

	/* Reset the ring buffer so that UDP packets have enough continuous space. */
	if (ring_buf_is_empty(&data_rb)) {
		ring_buf_reset(&data_rb);
	}

	while (index < len) {
		ret = ring_buf_put(&data_rb, buf + index, len - index);
		if (ret) {
			index += ret;
		} else {
			/* Buffer is full. Send data.*/
			raw_send(SM_DATAMODE_FLAGS_MORE_DATA);
		}
	}
}

static void raw_send_scheduled(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&mutex_data, K_FOREVER);

	/* Interpret partial quit_str as data, if we send due to timeout. */
	if (quit_str_partial_match > 0) {
		write_data_buf(CONFIG_SM_DATAMODE_TERMINATOR, quit_str_partial_match);
		quit_str_partial_match = 0;
	}

	raw_send(SM_DATAMODE_FLAGS_NONE);

	k_mutex_unlock(&mutex_data);
}

static void inactivity_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	LOG_DBG("Time limit reached");
	if (!ring_buf_is_empty(&data_rb)) {
		k_work_submit_to_queue(&sm_work_q, &raw_send_scheduled_work);
	} else {
		LOG_DBG("data buffer empty");
	}
}
K_TIMER_DEFINE(inactivity_timer, inactivity_timer_handler, NULL);

/* Search for quit_str and send data prior to that. Tracks quit_str over several calls. */
static size_t raw_rx_handler(const uint8_t *buf, const size_t len)
{
	k_mutex_lock(&mutex_data, K_FOREVER);

	const char *const quit_str = CONFIG_SM_DATAMODE_TERMINATOR;
	size_t processed;
	bool quit_str_match = false;
	uint8_t quit_str_match_count = quit_str_partial_match;
	uint8_t prev_quit_str_match_count = quit_str_partial_match;
	uint8_t prev_quit_str_match_count_original = quit_str_partial_match;

	/* If <data_len> is set in datamode, skip searching for quit_str. Just send data until
	 * length is reached.
	 */
	if (datamode_data_len > 0) {
		for (processed = 0; processed < len && datamode_data_len > 0; processed++) {
			write_data_buf(&buf[processed], 1);
			datamode_data_len--;
		}
		if (datamode_data_len == 0) {
			raw_send(SM_DATAMODE_FLAGS_NONE);
			(void)exit_datamode();
		}
	} else {
		/* Find quit_str or partial match at the end of the buffer. */
		for (processed = 0; processed < len && quit_str_match == false; processed++) {
			if (buf[processed] == quit_str[quit_str_match_count]) {
				quit_str_match_count++;
				if (quit_str_match_count == strlen(quit_str)) {
					quit_str_match = true;
				}
			} else if (quit_str_match_count > 0) {
				/* Check if we match a beginning of a new quit_str.
				 * We either match the first character, or in the edge case of
				 * quit_str starting with multiple same characters, e.g. "aaabbb",
				 * we match all but the current character (with input aaaa).
				 */
				for (int i = 0; i < quit_str_match_count; i++) {
					if (buf[processed] != quit_str[i]) {
						quit_str_match_count = i;
						break;
					}
				}
				if (quit_str_match_count == 0) {
					/* No match.
					 * Previous partial quit_str is data.
					 */
					prev_quit_str_match_count = 0;
				} else if (prev_quit_str_match_count > 0) {
					/* Partial match.
					 * Part of the previous partial quit_str is data.
					 */
					prev_quit_str_match_count--;
				}
			}
		}

		/* Write data which was previously interpreted as a possible partial quit_str. */
		write_data_buf(quit_str,
			       prev_quit_str_match_count_original - prev_quit_str_match_count);

		/* Write data from buf until the start of the possible (partial) quit_str. */
		write_data_buf(buf, processed - (quit_str_match_count - prev_quit_str_match_count));

		if (quit_str_match) {
			raw_send(SM_DATAMODE_FLAGS_NONE);
			(void)exit_datamode();
			quit_str_partial_match = 0;
		} else {
			quit_str_partial_match = quit_str_match_count;
		}
	}
	k_mutex_unlock(&mutex_data);

	return processed;
}

/*
 * Check AT command grammar based on below.
 *  AT<NULL>
 *  ATE0<NULL>
 *  ATE1<NULL>
 *  AT<separator><body><NULL>
 *  AT<separator><body>=<NULL>
 *  AT<separator><body>?<NULL>
 *  AT<separator><body>=?<NULL>
 *  AT<separator><body>=<parameters><NULL>
 * In which
 * <separator>: +, %, #
 * <body>: alphanumeric char or '_' only, size > 0
 * <parameters>: arbitrary, size > 0
 */
static int cmd_grammar_check(const char *cmd, size_t length)
{
	const char *body;

	/* check AT (if not, no check) */
	if (length < 2 || toupper((int)cmd[0]) != 'A' || toupper((int)cmd[1]) != 'T') {
		return -EINVAL;
	}

	/* check AT<NULL> */
	cmd += 2;
	if (*cmd == '\0') {
		return 0;
	}

	/* Check ATE0 and ATE1 */
	if (toupper(*cmd) == 'E' && (*(cmd + 1) == '0' || *(cmd + 1) == '1') &&
	    *(cmd + 2) == '\0') {
		return 0;
	}

	/* check AT<separator> */
	if ((*cmd != '+') && (*cmd != '%') && (*cmd != '#')) {
		return -EINVAL;
	}

	/* check AT<separator><body> */
	cmd += 1;
	body = cmd;
	while (true) {
		/* check body is alphanumeric or '_' */
		if (!isalpha((int)*cmd) && !isdigit((int)*cmd) && *cmd != '_') {
			break;
		}
		cmd++;
	}

	/* check body size > 0 */
	if (cmd == body) {
		return -EINVAL;
	}

	/* check AT<separator><body><NULL> */
	if (*cmd == '\0') {
		return 0;
	}

	/* check AT<separator><body>= or check AT<separator><body>? */
	if (*cmd != '=' && *cmd != '?') {
		return -EINVAL;
	}

	/* check AT<separator><body>?<NULL> */
	if (*cmd == '?') {
		cmd += 1;
		if (*cmd == '\0') {
			return 0;
		} else {
			return -EINVAL;
		}
	}

	/* check AT<separator><body>=<NULL> */
	cmd += 1;
	if (*cmd == '\0') {
		return 0;
	}

	/* check AT<separator><body>=?<NULL> */
	if (*cmd == '?') {
		cmd += 1;
		if (*cmd == '\0') {
			return 0;
		} else {
			return -EINVAL;
		}
	}

	/* no need to check AT<separator><body>=<parameters><NULL> */
	return 0;
}

static char *strrstr(const char *str1, const char *str2)
{
	size_t len1;
	size_t len2;

	if (str1 == NULL || str2 == NULL) {
		return NULL;
	}

	len1 = strlen(str1);
	len2 = strlen(str2);

	if (len2 > len1 || len1 == 0 || len2 == 0) {
		return NULL;
	}

	for (int i = len1 - len2; i >= 0; i--) {
		if (strncmp(str1 + i, str2, len2) == 0) {
			return (char *)str1 + i;
		}
	}

	return NULL;
}

static void format_final_result(char *buf, size_t buf_len, size_t buf_max_len)
{
	static const char ok_str[] = "OK\r\n";
	static const char error_str[] = "ERROR\r\n";
	static const char cme_error_str[] = "+CME ERROR:";
	static const char cms_error_str[] = "+CMS ERROR:";
	char *result = NULL;

	result = strrstr(buf, ok_str);
	if (result == NULL) {
		result = strrstr(buf, error_str);
	}

	if (result == NULL) {
		result = strrstr(buf, cme_error_str);
	}

	if (result == NULL) {
		result = strrstr(buf, cms_error_str);
	}

	if (result == NULL) {
		LOG_WRN("Final result not found");
		return;
	}

	/* insert CRLF before final result if there is information response before it */
	if (result != buf + strlen(CRLF_STR)) {
		if (buf_len + strlen(CRLF_STR) < buf_max_len) {
			memmove((void *)(result + strlen(CRLF_STR)), (void *)result,
				strlen(result));
			result[0] = CR;
			result[1] = LF;
			buf_len += strlen(CRLF_STR);
			buf[buf_len] = '\0';
		} else {
			LOG_WRN("No room to insert CRLF");
		}
	}
}

static int sm_at_send_internal(const uint8_t *data, size_t len, bool urc,
			       enum sm_debug_print print_debug)
{
	int ret;

	if (k_is_in_isr()) {
		LOG_ERR("FIXME: Attempt to send AT response (of size %u) in ISR.", len);
		return -EINTR;
	}

	ret = sm_tx_write(data, len, true, urc);
	if (!ret) {
		if (print_debug == SM_DEBUG_PRINT_FULL) {
			LOG_HEXDUMP_DBG(data, len, "TX");
		} else if (print_debug == SM_DEBUG_PRINT_SHORT) {
			LOG_HEXDUMP_DBG(data, MIN(HEXDUMP_LIMIT, len), "TX");
		}
	}
	return ret;
}

int sm_at_send(const uint8_t *data, size_t len)
{
	return sm_at_send_internal(data, len, false, SM_DEBUG_PRINT_FULL);
}

int sm_at_send_str(const char *str)
{
	return sm_at_send(str, strlen(str));
}

static void handle_bootloader_at_cmd(uint8_t *buf, size_t buf_size, char *at_cmd)
{
	int err;

	if (strncasecmp(at_cmd, AT_XDFU_INIT_CMD, sizeof(AT_XDFU_INIT_CMD) - 1) == 0) {
		err = sm_at_handle_xdfu_init(buf + strlen(CRLF_STR),
			buf_size - strlen(CRLF_STR), at_cmd);
		if (err) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
		} else {
			rsp_send_ok();
		}
	} else if (strncasecmp(at_cmd, AT_XDFU_WRITE_CMD,
			   sizeof(AT_XDFU_WRITE_CMD) - 1) == 0) {
		err = sm_at_handle_xdfu_write(buf + strlen(CRLF_STR),
			buf_size - strlen(CRLF_STR), at_cmd);
		if (err) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
		} else {
			rsp_send_ok();
		}
	} else if (strncasecmp(at_cmd, AT_XDFU_APPLY_CMD,
			       sizeof(AT_XDFU_APPLY_CMD) - 1) == 0) {
		err = sm_at_handle_xdfu_apply(buf + strlen(CRLF_STR),
						buf_size - strlen(CRLF_STR),
						at_cmd);
		if (err) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
		} else {
			rsp_send_ok();
		}
	} else if (strncasecmp(at_cmd, AT_XRESET_CMD, sizeof(AT_XRESET_CMD) - 1) == 0) {
		LOG_INF("Rebooting device via %s command", AT_XRESET_CMD);
		LOG_PANIC();
		final_call(sm_reset);
	} else {
		LOG_ERR("AT command not supported in bootloader mode: %s", at_cmd);
		rsp_send_error();
	}
}

static void cmd_send(uint8_t *buf, size_t cmd_length, size_t buf_size, bool *stop_at_receive)
{
	int err;
	size_t offset = 0;
	char *at_cmd = buf;

	LOG_HEXDUMP_DBG(buf, cmd_length, "RX");

	/* UART can send additional characters when the device is powered on.
	 * We ignore everything before the start of the AT-command.
	 */
	while (offset + 1 < cmd_length) {
		if (toupper(buf[offset]) == 'A' && toupper(buf[offset + 1]) == 'T') {
			at_cmd += offset;
			cmd_length -= offset;
			break;
		}
		offset++;
	}

	if (cmd_grammar_check(at_cmd, cmd_length) != 0) {
		LOG_ERR("AT command syntax invalid: %s", at_cmd);
		rsp_send_error();
		return;
	}

	/* If bootloader mode is enabled, handle custom AT commands. */
	if (sm_bootloader_mode_enabled) {
		handle_bootloader_at_cmd(buf, buf_size, at_cmd);
		return;
	} else {
		/* Send to modem. Same buffer used for sending and for the response.
		 * Reserve space for CRLF in response buffer.
		 */
		err = nrf_modem_at_cmd(buf + strlen(CRLF_STR), buf_size - strlen(CRLF_STR),
				       "%s", at_cmd);
		if (err == -SILENT_AT_COMMAND_RET) {
			return;
		} else if (err == -SILENT_AT_CMUX_COMMAND_RET) {
			/* Stop processing AT commands until CMUX pipe is established. */
			*stop_at_receive = true;
			return;
		} else if (err < 0) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
			return;
		} else if (err > 0) {
			LOG_ERR("AT command error (%d), type: %d: value: %d",
				err, nrf_modem_at_err_type(err), nrf_modem_at_err(err));
		}
	}

	/** Format as TS 27.007 command V1 with verbose response format,
	 *  based on current return of API nrf_modem_at_cmd() and MFWv1.3.x
	 */
	buf[0] = CR;
	buf[1] = LF;
	if (strlen(buf) > strlen(CRLF_STR)) {
		format_final_result(buf, strlen(buf), buf_size);
		err = sm_at_send_str(buf);
		if (err) {
			LOG_ERR("AT command response failed: %d", err);
		}
	}
}

static size_t cmd_rx_handler(const uint8_t *buf, const size_t len, bool *stop_at_receive)
{
	size_t processed;
	static bool inside_quotes;
	static size_t at_cmd_len;
	static size_t echo_len;
	static uint8_t prev_character;
	bool send = false;

	for (processed = 0; processed < len && send == false; processed++) {

		/* Handle control characters */
		switch (buf[processed]) {
		case 0x08: /* Backspace. */
			/* Fall through. */
		case 0x7F: /* DEL character */
			if (at_cmd_len == 0) {
				continue;
			}

			at_cmd_len--;
			/* If the removed character was a quote, need to toggle the flag. */
			if (prev_character == '"') {
				inside_quotes = !inside_quotes;
			}
			if (at_cmd_len > 0) {
				prev_character = sm_at_buf[at_cmd_len - 1];
			} else {
				prev_character = '\0';
			}
			continue;
		}

		/* Handle termination characters, if outside quotes. */
		if (!inside_quotes) {
			switch (buf[processed]) {
			case '\r':
				if (IS_ENABLED(CONFIG_SM_CR_TERMINATION)) {
					send = true;
				}
				break;
			case '\n':
				if (IS_ENABLED(CONFIG_SM_LF_TERMINATION)) {
					send = true;
				} else if (IS_ENABLED(CONFIG_SM_CR_LF_TERMINATION)) {
					if (at_cmd_len > 0 && prev_character == '\r') {
						at_cmd_len--; /* trim the CR char */
						send = true;
					}
				}
				break;
			}
		}

		if (send == false) {
			/* Write character to AT buffer, leave space for null */
			if (at_cmd_len < sizeof(sm_at_buf) - 1) {
				sm_at_buf[at_cmd_len] = buf[processed];
			}
			at_cmd_len++;

			/* Handle special written character */
			if (buf[processed] == '"') {
				inside_quotes = !inside_quotes;
			}

			prev_character = buf[processed];
		}
	}

	if (echo_ctx.enabled) {
		const uint8_t terminator_len = IS_ENABLED(CONFIG_SM_CR_LF_TERMINATION) ? 2 : 1;
		bool truncate = false;
		size_t echo_fragment_len = processed;

		/* Check if echo should be truncated. */
		if (echo_len + echo_fragment_len + (send ? 0 : terminator_len) >
		    CONFIG_SM_AT_ECHO_MAX_LEN) {
			truncate = true;
			echo_fragment_len = CONFIG_SM_AT_ECHO_MAX_LEN - echo_len - terminator_len;
		}

		/* Echoing incomplete AT-command will cause
		 * CONFIG_SM_URC_DELAY_WITH_INCOMPLETE_ECHO_MS delay in URCs after every
		 * UART RX buffer (keystroke, when typing).
		 */
		if (!send) {
			k_timer_start(&echo_ctx.timer,
				      K_MSEC(CONFIG_SM_URC_DELAY_WITH_INCOMPLETE_ECHO_MS),
				      K_NO_WAIT);
		} else {
			k_timer_stop(&echo_ctx.timer);
			sm_at_host_event_notify(SM_EVENT_URC);
		}

		(void)sm_at_send_internal(buf, echo_fragment_len, false, SM_DEBUG_PRINT_NONE);
		echo_len += echo_fragment_len;

		/* Send truncated termination characters.*/
		if (send && truncate) {
			if (IS_ENABLED(CONFIG_SM_CR_TERMINATION)) {
				(void)sm_at_send_internal((uint8_t *)"\r", 1, false,
							  SM_DEBUG_PRINT_NONE);
			} else if (IS_ENABLED(CONFIG_SM_LF_TERMINATION)) {
				(void)sm_at_send_internal((uint8_t *)"\n", 1, false,
							  SM_DEBUG_PRINT_NONE);
			} else {
				(void)sm_at_send_internal((uint8_t *)"\r\n", 2, false,
							  SM_DEBUG_PRINT_NONE);
			}
		}
	}

	if (send) {
		if (at_cmd_len > sizeof(sm_at_buf) - 1) {
			LOG_ERR("AT command buffer overflow, %d dropped", at_cmd_len);
			rsp_send_error();
		} else if (at_cmd_len > 0) {
			sm_at_buf[at_cmd_len] = '\0';
			cmd_send(sm_at_buf, at_cmd_len, sizeof(sm_at_buf), stop_at_receive);
		} else {
			/* Ignore 0 size command. */
		}

		inside_quotes = false;
		at_cmd_len = 0;
		echo_len = 0;
	}

	return processed;
}

/* Search for quit_str and exit datamode when one is found. */
static size_t null_handler(const uint8_t *buf, const size_t len)
{
	const char *const quit_str = CONFIG_SM_DATAMODE_TERMINATOR;
	static size_t dropped_count;
	static uint8_t match_count;

	size_t processed;
	bool match = false;

	if (dropped_count == 0) {
		LOG_WRN("Data pipe broken. Dropping data until datamode is terminated.");
	}

	for (processed = 0; processed < len && match == false; processed++) {
		if (buf[processed] == quit_str[match_count]) {
			match_count++;
			if (match_count == strlen(quit_str)) {
				match = true;
			}
		} else {
			match_count = 0;
		}
		dropped_count++;
	}

	if (match) {
		dropped_count -= strlen(quit_str);
		dropped_count += ring_buf_size_get(&data_rb);
		LOG_WRN("Terminating datamode, %d dropped", dropped_count);
		(void)exit_datamode();

		match_count = 0;
		dropped_count = 0;
	}

	return processed;
}

size_t sm_at_receive(const uint8_t *buf, size_t len, bool *stop_at_receive)
{
	size_t ret = 0;

	k_timer_stop(&inactivity_timer);

	while (ret < len) {

		switch (get_sm_mode()) {
		case SM_AT_COMMAND_MODE:
			ret += cmd_rx_handler(buf + ret, len - ret, stop_at_receive);
			if (*stop_at_receive) {
				return ret;
			}
			break;
		case SM_DATA_MODE:
			ret += raw_rx_handler(buf + ret, len - ret);
			break;
		case SM_NULL_MODE:
			ret += null_handler(buf + ret, len - ret);
			break;
		}

		assert(ret <= len);
	}

	/* start inactivity timer in datamode */
	if (get_sm_mode() == SM_DATA_MODE) {
		k_timer_start(&inactivity_timer, K_MSEC(sm_datamode_time_limit), K_NO_WAIT);
	}

	return ret;
}

AT_MONITOR(at_notify, ANY, notification_handler);

static void notification_handler(const char *notification)
{
#if defined(CONFIG_SM_PPP)
	if (!sm_fwd_cgev_notifs && !strncmp(notification, "+CGEV: ", strlen("+CGEV: "))) {
		/* CGEV notifications are silenced. Do not forward them. */
		return;
	}
#endif
	sm_at_send_internal(CRLF_STR, strlen(CRLF_STR), true, SM_DEBUG_PRINT_FULL);
	sm_at_send_internal(notification, strlen(notification), true, SM_DEBUG_PRINT_FULL);
}

void rsp_send_ok(void)
{
	sm_at_send_str(OK_STR);
}

void rsp_send_error(void)
{
	sm_at_send_str(ERROR_STR);
}

static void rsp_send_internal(bool urc, const char *fmt, va_list arg_ptr)
{
	static K_MUTEX_DEFINE(mutex_rsp_buf);
	static char rsp_buf[SM_AT_MAX_RSP_LEN];
	int rsp_len;

	k_mutex_lock(&mutex_rsp_buf, K_FOREVER);

	rsp_len = vsnprintf(rsp_buf, sizeof(rsp_buf), fmt, arg_ptr);
	rsp_len = MIN(rsp_len, sizeof(rsp_buf) - 1);

	sm_at_send_internal(rsp_buf, rsp_len, urc, SM_DEBUG_PRINT_FULL);

	k_mutex_unlock(&mutex_rsp_buf);
}

void rsp_send(const char *fmt, ...)
{
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	rsp_send_internal(false, fmt, arg_ptr);
	va_end(arg_ptr);
}

void urc_send(const char *fmt, ...)
{
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	rsp_send_internal(true, fmt, arg_ptr);
	va_end(arg_ptr);
}

void data_send(const uint8_t *data, size_t len)
{
	sm_at_send_internal(data, len, false, SM_DEBUG_PRINT_SHORT);
}

int enter_datamode(sm_datamode_handler_t handler, size_t data_len)
{
	k_mutex_lock(&mutex_mode, K_FOREVER);

	if (handler == NULL || datamode_handler != NULL || set_sm_mode(SM_DATA_MODE) == false) {
		LOG_INF("Invalid, not enter datamode");
		k_mutex_unlock(&mutex_mode);
		return -EINVAL;
	}

	k_mutex_lock(&mutex_data, K_FOREVER);
	ring_buf_reset(&data_rb);
	k_mutex_unlock(&mutex_data);

	datamode_handler = handler;
	datamode_data_len = data_len;
	if (sm_datamode_time_limit == 0) {
		if (sm_uart_baudrate > 0) {
			sm_datamode_time_limit = CONFIG_SM_UART_RX_BUF_SIZE * (8 + 1 + 1) * 1000 /
					      sm_uart_baudrate;
			sm_datamode_time_limit += UART_RX_MARGIN_MS;
		} else {
			LOG_WRN("Baudrate not set");
			sm_datamode_time_limit = 1000;
		}
	}
	LOG_INF("Enter datamode");

	k_mutex_unlock(&mutex_mode);

	return 0;
}

bool in_datamode(void)
{
	return (get_sm_mode() == SM_DATA_MODE);
}

bool in_at_mode(void)
{
	return (get_sm_mode() == SM_AT_COMMAND_MODE);
}

bool exit_datamode_handler(int result)
{
	bool ret = false;

	k_mutex_lock(&mutex_mode, K_FOREVER);

	if (set_sm_mode(SM_NULL_MODE)) {
		if (datamode_handler) {
			datamode_handler(DATAMODE_EXIT, NULL, 0, SM_DATAMODE_FLAGS_EXIT_HANDLER);
		}
		datamode_handler = NULL;
		datamode_handler_result = result;
		datamode_data_len = 0;
		ret = true;
	}

	k_mutex_unlock(&mutex_mode);

	return ret;
}

bool verify_datamode_control(uint16_t time_limit, uint16_t *min_time_limit)
{
	int min_time;

	if (sm_uart_baudrate == 0) {
		LOG_ERR("Baudrate not set");
		return false;
	}

	min_time = CONFIG_SM_UART_RX_BUF_SIZE * (8 + 1 + 1) * 1000 / sm_uart_baudrate;
	min_time += UART_RX_MARGIN_MS;

	if (time_limit > 0 && min_time > time_limit) {
		LOG_ERR("Invalid time_limit: %d, min: %d", time_limit, min_time);
		return false;
	}

	if (min_time_limit) {
		*min_time_limit = min_time;
	}

	return true;
}

int sm_at_cb_wrapper(char *buf, size_t len, char *at_cmd, sm_at_callback *cb)
{
	int err;
	struct at_parser parser;
	size_t valid_count = 0;
	enum at_parser_cmd_type type;

	assert(cb);

	err = at_parser_init(&parser, at_cmd);
	if (err) {
		return err;
	}

	err = at_parser_cmd_count_get(&parser, &valid_count);
	if (err) {
		return err;
	}

	err = at_parser_cmd_type_get(&parser, &type);
	if (err) {
		return err;
	}

	err = cb(type, &parser, valid_count);
	if (!err) {
		err = at_cmd_custom_respond(buf, len, "OK\r\n");
		if (err) {
			LOG_ERR("Failed to set OK response: %d", err);
		}
	} else if (err > 0) {
		int at_cmd_err = err;

		/* Reconstruct 'ERROR', 'CME ERROR' and 'CMS ERROR' response from
		 * nrf_modem_at_cmd() return value, which is returned by some Serial Modem specific
		 * AT commands, such as AT#XSMS
		 */
		switch (nrf_modem_at_err_type(err)) {
		case NRF_MODEM_AT_CME_ERROR:
			err = at_cmd_custom_respond(buf, len, "+CME ERROR: %d\r\n",
				nrf_modem_at_err(err));
			break;
		case NRF_MODEM_AT_CMS_ERROR:
			err = at_cmd_custom_respond(buf, len, "+CMS ERROR: %d\r\n",
				nrf_modem_at_err(err));
			break;
		case NRF_MODEM_AT_ERROR:
		default:
			err = at_cmd_custom_respond(buf, len, "ERROR\r\n");
			break;
		}
		if (err) {
			LOG_ERR("Failed to set error response: %d", err);
		} else {
			/* Return the original error code from 'cb()' */
			err = at_cmd_err;
		}
	}

	return err;
}

static int at_host_power_off(bool shutting_down)
{
	int err;

	if (shutting_down) {
		err = sm_uart_handler_disable();
		if (err) {
			LOG_WRN("Failed to disable UART. (%d)", err);
		}
	}

	err = pm_device_action_run(sm_uart_dev, PM_DEVICE_ACTION_SUSPEND);
	if (err) {
		LOG_WRN("Failed to suspend UART. (%d)", err);
	}

	return err;
}

int sm_at_host_power_off(void)
{
	const int err = at_host_power_off(false);

	/* Write sync str to buffer so it is sent first when resuming, do not flush. */
	if (!IS_ENABLED(CONFIG_SM_SKIP_READY_MSG)) {
		sm_tx_write(SM_SYNC_STR, strlen(SM_SYNC_STR), false, false);
	}

	return err;
}

int sm_at_host_power_on(void)
{
	const int err = pm_device_action_run(sm_uart_dev, PM_DEVICE_ACTION_RESUME);

	if (err && err != -EALREADY) {
		LOG_ERR("Failed to resume UART. (%d)", err);
		return err;
	}

	/* Flush the TX buffer. */
	sm_tx_write(NULL, 0, true, false);

	return 0;
}

void sm_at_host_echo(bool enable)
{
	echo_ctx.enabled = enable;
	k_timer_stop(&echo_ctx.timer);
}

bool sm_at_host_echo_urc_delay(void)
{
	return k_timer_remaining_get(&echo_ctx.timer) > 0;
}

static void echo_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	LOG_DBG("Time limit reached");
	sm_at_host_event_notify(SM_EVENT_URC);
}

void sm_at_host_register_event_cb(struct sm_event_callback *cb, enum sm_event event)
{
	sys_snode_t *p;

	LOG_DBG("Register event cb: %p for event: %d", (void *)cb, event);
	cb->events |= event;
	if (sys_slist_find(&event_ctx.event_cbs, &cb->node, &p)) {
		return;
	}
	sys_slist_append(&event_ctx.event_cbs, &cb->node);
}

static void event_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	struct sm_event_callback *event_cb;
	sys_snode_t *node, *tmp;

	enum sm_event events = atomic_clear(&event_ctx.events);

	SYS_SLIST_FOR_EACH_NODE_SAFE(&event_ctx.event_cbs, node, tmp) {
		event_cb = CONTAINER_OF(node, struct sm_event_callback, node);
		if (event_cb->events & events) {
			LOG_DBG("Notify event cb: %p for events: %d", (void *)event_cb, events);
			event_cb->cb(NULL);
			sys_slist_remove(&event_ctx.event_cbs, NULL, node);
		}
	}
}

struct sm_urc_ctx *sm_at_host_urc_ctx_acquire(enum sm_urc_owner owner)
{
	k_mutex_lock(&urc_ctx.mutex, K_FOREVER);

	if (urc_ctx.owner == SM_URC_OWNER_NONE || urc_ctx.owner == owner) {
		urc_ctx.owner = owner;
		k_mutex_unlock(&urc_ctx.mutex);
		return &urc_ctx;
	}

	k_mutex_unlock(&urc_ctx.mutex);
	return NULL;
}

void sm_at_host_urc_ctx_release(struct sm_urc_ctx *ctx, enum sm_urc_owner owner)
{
	if (ctx != &urc_ctx) {
		LOG_ERR("Invalid URC context");
		return;
	}

	k_mutex_lock(&ctx->mutex, K_FOREVER);

	if (ctx->owner == owner) {
		ctx->owner = SM_URC_OWNER_NONE;
	}

	k_mutex_unlock(&ctx->mutex);
}

static int sm_at_host_init(void)
{
	ring_buf_init(&urc_ctx.rb, sizeof(urc_ctx.buf), urc_ctx.buf);
	k_mutex_init(&urc_ctx.mutex);

	k_mutex_lock(&mutex_mode, K_FOREVER);
	sm_datamode_time_limit = 0;
	datamode_handler = NULL;
	at_mode = SM_AT_COMMAND_MODE;
	k_mutex_unlock(&mutex_mode);

	k_work_init(&raw_send_scheduled_work, raw_send_scheduled);

	return 0;
}
SYS_INIT(sm_at_host_init, APPLICATION, 0);

void sm_at_host_uninit(void)
{
	k_timer_stop(&echo_ctx.timer);

	k_mutex_lock(&mutex_mode, K_FOREVER);
	if (at_mode == SM_DATA_MODE) {
		k_timer_stop(&inactivity_timer);
	}
	datamode_handler = NULL;
	k_mutex_unlock(&mutex_mode);

	sm_at_uninit();

	at_host_power_off(true);

	LOG_DBG("at_host uninit done");
}

int sm_at_host_bootloader_init(void)
{
	int err;

	ring_buf_init(&urc_ctx.rb, sizeof(urc_ctx.buf), urc_ctx.buf);
	k_mutex_init(&urc_ctx.mutex);

	k_mutex_lock(&mutex_mode, K_FOREVER);
	sm_datamode_time_limit = 0;
	datamode_handler = NULL;
	at_mode = SM_AT_COMMAND_MODE;
	k_mutex_unlock(&mutex_mode);

	k_work_init(&raw_send_scheduled_work, raw_send_scheduled);

	err = sm_uart_handler_enable();
	if (err) {
		return err;
	}

	LOG_INF("at_host bootloader init done");
	return 0;
}
