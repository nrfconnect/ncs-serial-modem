/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Intercept AT+CEREG / AT+CGEREP / AT%XTIME and filter +CEREG / +CGEV / %XTIME URC
 * forwarding so internal users (Memfault, PPP, date_time, etc.) stay subscribed
 * while the host can opt out.
 */

#include "sm_at_host.h"
#include "sm_defines.h"
#include <modem/at_cmd_custom.h>
#include <modem/at_monitor.h>
#include <modem/lte_lc.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/logging/log.h>

int sm_util_at_cmd_no_intercept(char *buf, size_t len, const char *at_cmd);

LOG_MODULE_REGISTER(sm_urcf, CONFIG_SM_LOG_LEVEL);

#define SM_AT_CEREG_INTERNAL_MODE 1

STATIC bool sm_urcf_fwd_cereg;
STATIC bool sm_urcf_fwd_cgev;
STATIC bool sm_urcf_fwd_xtime;

AT_CMD_CUSTOM(sm_urcf_cereg_interceptor, "AT+CEREG", sm_urcf_cereg_callback);
AT_CMD_CUSTOM(sm_urcf_cgerep_interceptor, "AT+CGEREP", sm_urcf_cgerep_callback);
AT_CMD_CUSTOM(sm_urcf_xtime_interceptor, "AT%XTIME", sm_urcf_xtime_callback);
AT_CMD_CUSTOM(sm_urcf_cfun_interceptor, "AT+CFUN=", sm_urcf_cfun_callback);

static void sm_urcf_cereg_subscribe(void)
{
	char buf[sizeof("\r\nOK")];
	char cmd[16];
	int ret;

	snprintf(cmd, sizeof(cmd), "AT+CEREG=%d", SM_AT_CEREG_INTERNAL_MODE);
	ret = sm_util_at_cmd_no_intercept(buf, sizeof(buf), cmd);
	if (ret) {
		LOG_ERR("Failed to subscribe to +CEREG notifications (%d).", ret);
	}
}

static void sm_urcf_cgerep_subscribe(void)
{
	char buf[sizeof("\r\nOK")];
	int ret;

	ret = sm_util_at_cmd_no_intercept(buf, sizeof(buf), "AT+CGEREP=1");
	if (ret) {
		LOG_ERR("Failed to subscribe to +CGEV notifications (%d).", ret);
	}
}

static void sm_urcf_xtime_subscribe(void)
{
	char buf[sizeof("\r\nOK")];
	int ret;

	ret = sm_util_at_cmd_no_intercept(buf, sizeof(buf), "AT%XTIME=1");
	if (ret) {
		LOG_ERR("Failed to subscribe to %%XTIME notifications (%d).", ret);
	}
}

static void sm_urcf_on_cfun(unsigned int mode)
{
	if (mode == LTE_LC_FUNC_MODE_NORMAL || mode == LTE_LC_FUNC_MODE_ACTIVATE_LTE) {
		sm_urcf_cgerep_subscribe();
		sm_urcf_cereg_subscribe();
		sm_urcf_xtime_subscribe();
	} else if (mode == LTE_LC_FUNC_MODE_POWER_OFF) {
		sm_urcf_fwd_cgev = false;
		sm_urcf_fwd_cereg = false;
		sm_urcf_fwd_xtime = false;
	}
}

STATIC int sm_urcf_cereg_callback(char *buf, size_t len, char *at_cmd)
{
	int ret;
	unsigned int mode = 0;
	const bool set_cmd = (sscanf(at_cmd, "%*[^=]=%u", &mode) == 1);

	if (!set_cmd && (!strcasecmp(at_cmd, "AT+CEREG") || !strcasecmp(at_cmd, "AT+CEREG="))) {
		LOG_ERR("The syntax %s is disallowed. Use AT+CEREG=0 instead.", at_cmd);
		return -EINVAL;
	}

	if (!set_cmd || mode != 0) {
		ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
		if (ret) {
			return ret;
		}

		if (at_cmd[strlen("AT+CEREG")] == '?' && !sm_urcf_fwd_cereg) {
			const size_t mode_idx = strlen("+CEREG: ");

			if (mode_idx < strlen(buf)) {
				buf[mode_idx] = '0';
			}
		}
	} else { /* AT+CEREG=0 */
		snprintf(buf, len, "%s", "OK\r\n");
	}

	if (set_cmd) {
		sm_urcf_fwd_cereg = (mode != 0);
	}

	return 0;
}

STATIC int sm_urcf_cgerep_callback(char *buf, size_t len, char *at_cmd)
{
	int ret;
	unsigned int subscribe = 0;
	const bool set_cmd = (sscanf(at_cmd, "%*[^=]=%u", &subscribe) == 1);

	if (!set_cmd && (!strcasecmp(at_cmd, "AT+CGEREP") || !strcasecmp(at_cmd, "AT+CGEREP="))) {
		LOG_ERR("The syntax %s is disallowed. Use AT+CGEREP=0 instead.", at_cmd);
		return -EINVAL;
	}

	if (!set_cmd || subscribe) {
		ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
		if (ret) {
			return ret;
		}

		if (at_cmd[strlen("AT+CGEREP")] == '?') {
			const size_t mode_idx = strlen("+CGEREP: ");

			if (mode_idx < strlen(buf)) {
				buf[mode_idx] = '0' + sm_urcf_fwd_cgev;
			}
		}
	} else { /* AT+CGEREP=0 */
		snprintf(buf, len, "%s", "OK\r\n");
	}

	if (set_cmd) {
		sm_urcf_fwd_cgev = (subscribe != 0);
	}

	return 0;
}

STATIC int sm_urcf_xtime_callback(char *buf, size_t len, char *at_cmd)
{
	int ret;
	unsigned int subscribe = 0;
	const bool set_cmd = (sscanf(at_cmd, "%*[^=]=%u", &subscribe) == 1);

	if (!set_cmd && (!strcasecmp(at_cmd, "AT%XTIME") || !strcasecmp(at_cmd, "AT%XTIME="))) {
		LOG_ERR("The syntax %s is disallowed. Use AT%%XTIME=0 instead.", at_cmd);
		return -EINVAL;
	}

	if (!set_cmd || subscribe) {
		ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
		if (ret) {
			return ret;
		}

		if (at_cmd[strlen("AT%XTIME")] == '?') {
			const size_t mode_idx = strlen("%XTIME: ");

			if (mode_idx < strlen(buf)) {
				buf[mode_idx] = '0' + sm_urcf_fwd_xtime;
			}
		}
	} else { /* AT%XTIME=0 */
		snprintf(buf, len, "%s", "OK\r\n");
	}

	if (set_cmd) {
		sm_urcf_fwd_xtime = (subscribe != 0);
	}

	return 0;
}

STATIC int sm_urcf_cfun_callback(char *buf, size_t len, char *at_cmd)
{
	unsigned int mode;
	const bool set_cmd = (sscanf(at_cmd, "%*[^=]=%u", &mode) == 1);
	int ret;

	ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
	if (ret) {
		return ret;
	}

	if (set_cmd) {
		sm_urcf_on_cfun(mode);
	}

	return 0;
}

STATIC bool sm_urcf_should_forward(const char *notification)
{
	if (!sm_urcf_fwd_cgev && !strncmp(notification, "+CGEV: ", strlen("+CGEV: "))) {
		return false;
	}

	if (!sm_urcf_fwd_cereg && !strncmp(notification, "+CEREG: ", strlen("+CEREG: "))) {
		return false;
	}

	if (!sm_urcf_fwd_xtime && !strncmp(notification, "%XTIME:", strlen("%XTIME:"))) {
		return false;
	}

	return true;
}

AT_MONITOR(sm_urcf_notify, ANY, sm_urcf_notification_handler);

static void sm_urcf_notification_handler(const char *notification)
{
	if (!sm_urcf_should_forward(notification)) {
		return;
	}

	urc_send(CRLF_STR "%s", notification);
}
