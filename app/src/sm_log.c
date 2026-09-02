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
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static bool log_active;
static int log_level;

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

int sm_log_level(void)
{
	return log_level;
}

/* Commands whose payload/credential bytes are redacted from the UART log at
 * AT#XLOG=1 and shown only at AT#XLOG=2.
 */
const char *sm_log_cmd_sensitive_prefix(const char *cmd, size_t len)
{
	/* nRF modem commands whose argument or response carries a secret (credential,
	 * key, token, PIN or password). Serial Modem commands which carry possibly
	 * encrypted data.
	 *
	 * Currently in sync with modem releases:
	 * - 2.0.3
	 * - NTN 1.0.0
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
		"AT#XMQTTCON",   /* MQTT connect: username / password */
		"AT#XMQTTPUB",   /* MQTT publish: topic + message inline */
		"AT#XMQTTSUB",   /* MQTT subscribe: topic inline */
		"AT#XHTTPCREQ",  /* HTTP request: may carry Authorization headers */
		"AT#XCOAPCREQ",  /* CoAP request: URI path + options (Uri-Query / Proxy-Uri) */
		"AT#XSMS",       /* SMS send: recipient number + message text inline */
		"AT#XSEND",      /* SM app data (also matches AT#XSENDTO) */
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

/* Hexdump an inbound AT command line, redacting the payload of sensitive
 * commands unless AT#XLOG=2 (SM_LOG_ON_FULL) is set.
 */
void sm_log_rx_command(const uint8_t *buf, size_t len)
{
	size_t off = 0;

	while (off + 1 < len && !(toupper(buf[off]) == 'A' && toupper(buf[off + 1]) == 'T')) {
		off++;
	}

	const char *cmd = (const char *)buf + off;
	size_t cmd_len = len - off;
	const char *prefix = sm_log_cmd_sensitive_prefix(cmd, cmd_len);

	if (log_level < SM_LOG_ON_FULL && prefix) {
		size_t shown = off + strlen(prefix);

		LOG_HEXDUMP_DBG(buf, shown, "RX");
		if (len > shown) {
			LOG_DBG("RX: [%zu bytes payload redacted; AT#XLOG=2 to show]", len - shown);
		}
		return;
	}

	LOG_HEXDUMP_DBG(buf, len, "RX");
}

/* URCs whose payload/credential bytes are redacted from the UART log at
 * AT#XLOG=1 and shown only at AT#XLOG=2.
 */
static const char *urc_sensitive_prefix(const char *data, size_t len)
{
	/* nRF modem URCs which carry sensitive data.
	 *
	 * Currently in sync with modem releases:
	 * - 2.0.3
	 * - NTN 1.0.0
	 */
	static const char *const sensitive[] = {
		"+CMT:", /* SMS-DELIVER: header + message PDU */
		"+CDS:", /* SMS-STATUS-REPORT: header + delivery-report PDU (dest address) */
		NULL,
	};

	for (size_t i = 0; sensitive[i] != NULL; i++) {
		size_t n = strlen(sensitive[i]);

		if (len >= n && strncasecmp(data, sensitive[i], n) == 0) {
			return sensitive[i];
		}
	}
	return NULL;
}

void sm_log_urc(const char *dest, const void *pipe, const uint8_t *data, size_t len)
{
	size_t off = 0;
	const char *cdata = (const char *)data;

	while (off < len && (cdata[off] == '\r' || cdata[off] == '\n')) {
		off++;
	}

	const char *prefix = urc_sensitive_prefix(cdata + off, len - off);

	if (log_level < SM_LOG_ON_FULL && prefix) {
		size_t shown = off + strlen(prefix);

		LOG_DBG("URC %s pipe=%p: %.*s", dest, pipe, (int)shown, cdata);
		if (len > shown) {
			LOG_DBG("URC %s pipe=%p: [%zu bytes PDU redacted; AT#XLOG=2 to show]",
				dest, pipe, len - shown);
		}
		return;
	}
	LOG_DBG("URC %s pipe=%p: %.*s", dest, pipe, (int)len, cdata);
}

SM_AT_CMD_CUSTOM(xlog, "AT#XLOG", handle_at_log);
STATIC int handle_at_log(enum at_parser_cmd_type cmd_type, struct at_parser *parser, uint32_t)
{
	const struct log_backend *log_be = log_backend_get_by_name("log_backend_uart");

	if (!log_be) {
		return -ENODEV;
	}

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		int level;
		int ret = at_parser_num_get(parser, 1, &level);

		if (ret || (level < SM_LOG_OFF) || (level > SM_LOG_ON_FULL)) {
			return -EINVAL;
		}

		const bool want_on = (level > SM_LOG_OFF);

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

		log_level = level;
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		rsp_send("\r\n#XLOG: %d\r\n", log_level);
		return 0;
	} else if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XLOG: (0,1,2)\r\n");
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
		log_level = SM_LOG_ON_FULL;
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
