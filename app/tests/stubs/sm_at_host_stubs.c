/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file sm_at_host_stubs.c
 * Stub implementations and response capture for sm_at_host functions
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <zephyr/kernel.h>
#include "sm_at_host.h"

/* Global response buffer to capture AT command responses */
static char response_buffer[4096];
static size_t response_len;

/* Get the captured response */
const char *get_captured_response(void)
{
	return response_buffer;
}

size_t get_captured_response_len(void)
{
	return response_len;
}

/* Clear the captured response */
void clear_captured_response(void)
{
	memset(response_buffer, 0, sizeof(response_buffer));
	response_len = 0;
}

/* Capture response data from sm_tx_write (called by rsp_send ->
 * sm_at_send_internal -> sm_tx_write)
 *
 * This function accumulates responses to handle multi-part AT responses.
 */
void capture_response_data(const uint8_t *data, size_t len)
{
	size_t available_space = sizeof(response_buffer) - response_len - 1;

	if (len > available_space) {
		len = available_space;
	}

	if (len > 0) {
		memcpy(response_buffer + response_len, data, len);
		response_len += len;
		response_buffer[response_len] = '\0';
	}
}

/* Wrap sm_at_send to capture responses - this will be called
 * by rsp_send, rsp_send_ok, etc.
 *
 * Accumulates multiple responses instead of overwriting. This is necessary
 * because AT command responses may be sent in multiple calls:
 * 1. First call: command-specific response (e.g., "\r\n#XSOCKET: 3,1,6\r\n")
 * 2. Second call: OK response (e.g., "\r\nOK\r\n")
 *
 * Note: In the current test setup, only synchronous responses sent during
 * sm_at_receive() execution are captured. Responses sent asynchronously
 * (e.g., via work queues) after sm_at_receive() returns are not captured.
 */
int __wrap_sm_at_send(const uint8_t *data, size_t len)
{
	/* Accumulate the response instead of overwriting */
	size_t available_space = sizeof(response_buffer) - response_len - 1;

	if (len > available_space) {
		len = available_space;
	}

	if (len > 0) {
		memcpy(response_buffer + response_len, data, len);
		response_len += len;
		response_buffer[response_len] = '\0';
	}

	/* Call the real function (which will call sm_at_send_internal ->
	 * sm_tx_write)
	 */
	extern int __real_sm_at_send(const uint8_t *data, size_t len);
	int ret = __real_sm_at_send(data, len);

	return ret;
}

/* Stub for sm_fota_post_process - needed by sm_at_host_init */
void sm_fota_post_process(void)
{
	/* Stub - no-op for tests */
}

/* Stub for sm_at_fota_init - needed by sm_at_init */
int sm_at_fota_init(void)
{
	/* Stub - return success */
	return 0;
}

/* Stub for sm_at_fota_uninit - needed by sm_at_uninit */
int sm_at_fota_uninit(void)
{
	/* Stub - return success */
	return 0;
}

/* Bootloader mode flag - needed by sm_at_host.c after rebase */
bool sm_bootloader_mode_enabled;

/* Initialization failed flag - needed by sm_at_host_init */
bool sm_init_failed;

/* Stubs for DFU/bootloader functions - needed after rebase */
int sm_at_handle_xdfu_init(char *buf, size_t len, char *at_cmd)
{
	/* Stub - return success */
	return 0;
}

int sm_at_handle_xdfu_write(char *buf, size_t len, char *at_cmd)
{
	/* Stub - return success */
	return 0;
}

int sm_at_handle_xdfu_apply(char *buf, size_t len, char *at_cmd)
{
	/* Stub - return success */
	return 0;
}
