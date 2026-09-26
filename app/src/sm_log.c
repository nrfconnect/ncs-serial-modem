/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * AT#XLOG command — enable/disable the Zephyr UART log backend at runtime.
 *
 * The UART (zephyr,console) is shared with the modem-trace backend
 * (sm_trace_backend_uart.c).  AT#XLOG and AT#XTRACE are mutually exclusive:
 * each refuses to activate while the other is in use.
 *
 */

#include <zephyr/devicetree.h>

/* Only compiled if zephyr,console is present and active in the devicetree. */
#if DT_HAS_CHOSEN(zephyr_console) && DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_console), okay)

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/pm/device.h>

#include "sm_at_host.h"
#include "sm_log.h"

LOG_MODULE_REGISTER(sm_log, CONFIG_SM_LOG_LEVEL);

/* Zephyr console UART is used both for application logs and modem traces. */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)

#if DT_HAS_CHOSEN(ncs_sm_uart)
/* sm_log_init() suspends this UART to save power. That cannot work if it is
 * also the AT host UART: the AT host keeps asynchronous RX enabled, so
 * pm_device_action_run(SUSPEND) returns -EAGAIN, reported as "INIT ERROR".
 * Suspending it successfully would be worse, as it releases the pins.
 */
BUILD_ASSERT(!DT_SAME_NODE(DT_CHOSEN(zephyr_console), DT_CHOSEN(ncs_sm_uart)),
	     "zephyr,console must not be the same node as ncs,sm-uart. "
	     "Point the console at a different UART in the board devicetree.");
#endif

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static bool log_active;
static int log_mode;

static int uart_suspend(void)
{
	int ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);

	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to %s UART device: %d", "suspend", ret);
		return ret;
	}

	return 0;
}

static int uart_resume(void)
{
	int ret = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);

	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to %s UART device: %d", "resume", ret);
		return ret;
	}
	return 0;
}

static bool uart_is_active(void)
{
	enum pm_device_state state = PM_DEVICE_STATE_OFF;
	int err = pm_device_state_get(uart_dev, &state);

	if (err) {
		LOG_ERR("Failed to get UART device state (%d).", err);
		return false;
	}
	return state == PM_DEVICE_STATE_ACTIVE;
}

void sm_log_flush(void)
{
	const struct log_backend *log_be = log_backend_get_by_name("log_backend_uart");

	if (log_be && log_be->cb && log_be->cb->initialized) {
		log_flush();
	}
}

int sm_log_mode(void)
{
	return log_mode;
}

/* Commands whose payload/credential bytes are redacted from the UART log at
 * AT#XLOG=1 and shown only at DBG level with AT#XLOG=2.
 */
const char *sm_log_cmd_sensitive_prefix(const char *cmd, size_t len)
{
	/* nRF modem commands whose argument or response carries a secret (credential,
	 * key, token, PIN or password). Serial Modem commands which carry possibly
	 * encrypted data.
	 *
	 * Currently in sync with modem releases:
	 * - mfw_nrf91x1 v2.0.4
	 * - mfw_nrf9151-ntn v1.0.1
	 */
	static const char *const sensitive[] = {
		"AT%CMNG",       /* credentials: certs, PSKs, private keys */
		"AT%KEYGEN",     /* generated key / CSR */
		"AT%KEYINJECT",  /* injected key material */
		"AT%JWT",        /* signed JWT */
		"AT%ATTESTTOKEN",
		"AT%CLAIMTOKEN",
		"AT%XPMNG",      /* public-key storage */
		"AT%XSUDO",      /* signed authenticated access */
		"AT%XUSIMLCK",   /* personalization / lock codes */
		"AT+CPIN",       /* PIN (also matches +CPINR) */
		"AT+CPWD",       /* password change */
		"AT+CLCK",       /* facility password */
		"AT+CGAUTH",     /* PDN username / password */
		"AT+CMGS",       /* SMS send: recipient number + message text/PDU inline */
		"AT#XMQTTCON",   /* MQTT connect: username / password */
		"AT#XMQTTPUB",   /* MQTT publish: topic + message inline */
		"AT#XMQTTSUB",   /* MQTT subscribe: topic inline */
		"AT#XHTTPCREQ",  /* HTTP request: may carry Authorization headers */
		"AT#XCOAPCREQ",  /* CoAP request: URI path + options (Uri-Query / Proxy-Uri) */
		"AT#XSMS",       /* SMS send: recipient number + message text inline */
		"AT#XSEND",      /* SM app data (also matches AT#XSENDTO) */
		"AT#XCARRIER=\"app_data_set\"", /* LwM2M carrier app data: inline hex payload */
		"AT#XCARRIER=\"log_data\"",     /* LwM2M carrier event log: inline hex payload */
		"AT#XNRFCLOUDOBSAUTO",    /* nRF Cloud Obs auto-upload: project_key override */
		"AT#XNRFCLOUDOBSUPLOAD",  /* nRF Cloud Obs upload: project_key override */
		"AT#XNRFCLOUDOBSFORWARD", /* nRF Cloud Obs forward: chunk + project_key override */
		NULL,
	};

	for (size_t i = 0; sensitive[i] != NULL; i++) {
		size_t n = strlen(sensitive[i]);

		if (len < n || strncasecmp(cmd, sensitive[i], n) != 0) {
			continue;
		}
		/* AT+CMD=? test syntax only lists supported parameters, never a secret. */
		if (len >= n + 2 && cmd[n] == '=' && cmd[n + 1] == '?') {
			return NULL;
		}
		return sensitive[i];
	}
	return NULL;
}

/* Log an inbound AT command line.
 *
 * AT#XLOG=0/1: INF text; sensitive payloads redacted. Collected by nRF Cloud Observability.
 * AT#XLOG=2:   DBG hex dump; no INF. Not collected by nRF Cloud Observability.
 */
void sm_log_rx_command(const uint8_t *buf, size_t len)
{
	if (log_mode >= SM_LOG_MODE_FULL) {
		LOG_HEXDUMP_DBG(buf, len, "RX");
		return;
	}

	size_t off = 0;

	while (off + 1 < len && !(toupper(buf[off]) == 'A' && toupper(buf[off + 1]) == 'T')) {
		off++;
	}

	const char *cmd = (const char *)buf + off;
	size_t cmd_len = len - off;
	const char *prefix = sm_log_cmd_sensitive_prefix(cmd, cmd_len);

	if (prefix) {
		LOG_INF("RX: %.*s [+%zu B redacted]", (int)strlen(prefix), cmd,
			cmd_len - strlen(prefix));
	} else {
		LOG_INF("RX: %.*s", (int)cmd_len, cmd);
	}
}

/* Responses whose payload/credential bytes are redacted from the UART log at
 * AT#XLOG=1 and shown only at DBG level with AT#XLOG=2.
 */
static const char *sm_log_rsp_sensitive_prefix(const char *line, size_t len)
{
	/* Response/URC line prefixes that carry sensitive data (credentials, keys, tokens,
	 * phone numbers, message content). A match redacts this line and the rest of the
	 * response (e.g. the PDU line following an SMS URC header).
	 *
	 * Currently in sync with modem releases:
	 * - mfw_nrf91x1 v2.0.4
	 * - mfw_nrf9151-ntn v1.0.1
	 */

	static const char *const sensitive[] = {
		/* Modem credential / key / token responses */
		"%CMNG:",        /* credentials: certs, PSKs, private keys */
		"%KEYGEN:",      /* generated key / CSR */
		"%KEYINJECT:",   /* injected key material */
		"%JWT:",         /* signed JWT */
		"%ATTESTTOKEN:", /* attestation token */
		"%CLAIMTOKEN:",  /* claim token */
		"%XPMNG:",       /* public-key storage */
		"%XSUDO:",       /* signed authenticated access */
		/* Modem SMS URCs */
		"+CMT:",         /* SMS-DELIVER: header + message PDU */
		"+CDS:",         /* SMS-STATUS-REPORT: delivery-report PDU with dest address */
		/* SM SMS URC */
		"#XSMS:",        /* SM SMS receive: timestamp + sender number + message text */
		/* nRF Cloud Observability */
		"#XNRFCLOUDOBSAUTO:", /* read echoes the persisted project_key override */
		"MC:",                /* AT#XNRFCLOUDOBSEXPORT: nRF Cloud Obs chunk dump */
		NULL,
	};

	for (size_t i = 0; sensitive[i] != NULL; i++) {
		size_t n = strlen(sensitive[i]);

		if (len >= n && strncasecmp(line, sensitive[i], n) == 0) {
			return sensitive[i];
		}
	}
	return NULL;
}

/* Final result code that terminates an AT command response. */
static bool sm_log_rsp_is_final(const char *line, size_t len)
{
	static const char *const final[] = {
		"OK",
		"ERROR",
		"+CME ERROR:",
		"+CMS ERROR:",
		NULL,
	};

	for (size_t i = 0; final[i] != NULL; i++) {
		size_t n = strlen(final[i]);

		if (len >= n && strncasecmp(line, final[i], n) == 0) {
			return true;
		}
	}
	return false;
}

/* Find the next non-empty line, starting at *pos. Advances *pos past it. */
static bool sm_log_next_line(const char *buf, size_t len, size_t *pos, size_t *line_start,
			      size_t *line_len)
{
	while (*pos < len && buf[*pos] && (buf[*pos] == '\r' || buf[*pos] == '\n')) {
		(*pos)++;
	}

	if (*pos >= len || !buf[*pos]) {
		return false;
	}

	*line_start = *pos;

	while (*pos < len && buf[*pos] && buf[*pos] != '\r' && buf[*pos] != '\n') {
		(*pos)++;
	}

	*line_len = *pos - *line_start;
	return true;
}

/* Log an outbound buffer (AT responses and URCs).
 *
 * AT#XLOG=0/1: INF text, line by line; sensitive lines redacted. Collected by nRF Cloud Obs.
 * AT#XLOG=2:   DBG hex dump; no INF. Not collected by nRF Cloud Observability.
 */
void sm_log_tx(const uint8_t *data, size_t len)
{
	if (log_mode >= SM_LOG_MODE_FULL) {
		LOG_HEXDUMP_DBG(data, len, "TX");
		return;
	}

	const char *buf = (const char *)data;
	size_t pos = 0;
	size_t line_start, line_len;

	while (sm_log_next_line(buf, len, &pos, &line_start, &line_len)) {
		const char *line = buf + line_start;
		const char *prefix = sm_log_rsp_sensitive_prefix(line, line_len);

		if (!prefix) {
			LOG_INF("TX: %.*s", (int)line_len, line);
			continue;
		}

		/* Redact everything up to the final result code (or end of buffer),
		 * e.g. the PDU line following an SMS URC header, as a single count.
		 */
		size_t redacted_bytes = line_len - strlen(prefix);
		size_t final_start = 0, final_len = 0;

		while (sm_log_next_line(buf, len, &pos, &line_start, &line_len)) {
			if (sm_log_rsp_is_final(buf + line_start, line_len)) {
				final_start = line_start;
				final_len = line_len;
				break;
			}
			redacted_bytes += line_len;
		}

		LOG_INF("TX: %s [+%zu B redacted]", prefix, redacted_bytes);
		if (final_len) {
			LOG_INF("TX: %.*s", (int)final_len, buf + final_start);
		}
	}
}

SM_AT_CMD_CUSTOM(xlog, "AT#XLOG", handle_at_log);
STATIC int handle_at_log(enum at_parser_cmd_type cmd_type, struct at_parser *parser, uint32_t)
{
	const struct log_backend *log_be = log_backend_get_by_name("log_backend_uart");

	if (!log_be) {
		return -ENODEV;
	}

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		int mode;
		int ret = at_parser_num_get(parser, 1, &mode);
		int max_mode = IS_ENABLED(CONFIG_SM_LOG_LEVEL_DBG) ? SM_LOG_MODE_FULL
								    : SM_LOG_MODE_REDACTED;

		if (ret || (mode < SM_LOG_MODE_OFF) || (mode > max_mode)) {
			return -EINVAL;
		}

		const bool want_on = (mode > SM_LOG_MODE_OFF);

		if (want_on && !log_active) {
			if (uart_is_active()) {
				return -EBUSY;
			}
			ret = uart_resume();
			if (ret) {
				return ret;
			}
			if (!log_be->cb->initialized) {
				log_backend_init(log_be);
			}
			log_backend_enable(log_be, log_be->cb->ctx, CONFIG_LOG_DEFAULT_LEVEL);
			log_active = true;
		} else if (!want_on && log_active) {
			log_backend_disable(log_be);
			ret = uart_suspend();
			if (ret) {
				return ret;
			}
			log_active = false;
		}

		log_mode = mode;
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		rsp_send("\r\n#XLOG: %d\r\n", log_mode);
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		if (IS_ENABLED(CONFIG_SM_LOG_LEVEL_DBG)) {
			rsp_send("\r\n#XLOG: (0,1,2)\r\n");
		} else {
			rsp_send("\r\n#XLOG: (0,1)\r\n");
		}
		return 0;
	}

	return -EINVAL;
}

/* Whether bootloader mode is enabled. */
extern bool sm_bootloader_mode_enabled;

static int sm_log_init(void)
{
	if (sm_bootloader_mode_enabled) {
		/* Keep the logging (and logging UART) enabled in bootloader mode */
		log_active = true;
		log_mode = SM_LOG_MODE_REDACTED;
		return 0;
	}

	const struct log_backend *log_be = log_backend_get_by_name("log_backend_uart");

	if (log_be) {
		LOG_DBG("Use AT#XLOG=1 to enable UART logging");
		sm_log_flush();
		log_backend_disable(log_be);
	}

	/* Suspend the UART device that is shared by application log and modem trace */
	int ret = uart_suspend();

	if (ret) {
		urc_send(SM_SYNC_ERR_STR);
		return ret;
	}

	return 0;
}

/* Runs after application initialization */
SYS_INIT(sm_log_init, APPLICATION, 101);

#endif /* DT_HAS_CHOSEN(zephyr_console) */
