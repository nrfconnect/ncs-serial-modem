/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file nrf_modem_at_wrapper.c
 * Wrapper for nrf_modem_at_cmd to intercept AT commands in tests
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <nrf_modem_at.h>
#include <nrf_errno.h>
#include <modem/at_cmd_custom.h>

/* External wrapper functions declared by SM_AT_CMD_CUSTOM macro */
extern int handle_at_smver_wrapper_xsmver(char *buf, size_t len, char *at_cmd);
extern int handle_at_sleep_wrapper_xsleep(char *buf, size_t len, char *at_cmd);
extern int handle_at_shutdown_wrapper_xshutdown(char *buf, size_t len, char *at_cmd);
extern int handle_at_reset_wrapper_xreset(char *buf, size_t len, char *at_cmd);
extern int handle_at_modemreset_wrapper_xmodemreset(char *buf, size_t len, char *at_cmd);
extern int handle_at_uuid_wrapper_xuuid(char *buf, size_t len, char *at_cmd);
extern int handle_at_datactrl_wrapper_xdatactrl(char *buf, size_t len, char *at_cmd);
extern int handle_at_clac_wrapper_xclac(char *buf, size_t len, char *at_cmd);
extern int handle_ate0_wrapper_ate0(char *buf, size_t len, char *at_cmd);
extern int handle_ate1_wrapper_ate1(char *buf, size_t len, char *at_cmd);

int nrf_modem_at_cmd(void *buf, size_t buf_size, const char *fmt, ...)
{
	char at_cmd[256];
	va_list args;
	int ret;

	va_start(args, fmt);
	vsnprintf(at_cmd, sizeof(at_cmd), fmt, args);
	va_end(args);

	/* Route to appropriate handler based on command */
	if (strncasecmp(at_cmd, "AT#XSMVER", 9) == 0) {
		ret = handle_at_smver_wrapper_xsmver((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XSLEEP", 9) == 0) {
		ret = handle_at_sleep_wrapper_xsleep((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XSHUTDOWN", 12) == 0) {
		ret = handle_at_shutdown_wrapper_xshutdown((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XRESET", 9) == 0) {
		ret = handle_at_reset_wrapper_xreset((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XMODEMRESET", 14) == 0) {
		ret = handle_at_modemreset_wrapper_xmodemreset((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XUUID", 8) == 0) {
		ret = handle_at_uuid_wrapper_xuuid((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XDATACTRL", 12) == 0) {
		ret = handle_at_datactrl_wrapper_xdatactrl((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "AT#XCLAC", 8) == 0) {
		ret = handle_at_clac_wrapper_xclac((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "ATE0", 4) == 0) {
		ret = handle_ate0_wrapper_ate0((char *)buf, buf_size, at_cmd);
	} else if (strncasecmp(at_cmd, "ATE1", 4) == 0) {
		ret = handle_ate1_wrapper_ate1((char *)buf, buf_size, at_cmd);
	} else {
		/* Unknown command - return error */
		ret = -NRF_EINVAL;
	}

	return ret;
}
