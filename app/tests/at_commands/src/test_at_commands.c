/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file test_at_commands.c
 * Unit tests for sm_at_commands.c
 */

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <dfu/dfu_target.h>

#include "sm_at_host.h"

/* CMock-generated mocks */
#include "cmock_nrf_modem_at.h"

/* FOTA type - needed by sm_at_commands.c */
uint8_t sm_fota_type = DFU_TARGET_IMAGE_TYPE_NONE;

/* Custom AT command list symbols - needed by AT+CLAC handler */
extern int _nrf_modem_at_cmd_custom_list_start;
extern int _nrf_modem_at_cmd_custom_list_end;

int _nrf_modem_at_cmd_custom_list_start;
int _nrf_modem_at_cmd_custom_list_end;

/* Work queue needed by sm_at_host and other modules */
struct k_work_q sm_work_q;

/* Response capture - provided by sm_at_host_stubs.c */
extern void capture_response_data(const uint8_t *data, size_t len);
extern void clear_captured_response(void);
extern const char *get_captured_response(void);

/* Helper to send AT command and capture response */
static void send_at_command(const char *cmd)
{
	bool stop_at_receive = false;
	const uint8_t *cmd_bytes = (const uint8_t *)cmd;
	size_t cmd_len = strlen(cmd);

	clear_captured_response();

	/* Send command through sm_at_receive() */
	sm_at_receive(cmd_bytes, cmd_len, &stop_at_receive);
}

/*
 * Test: AT#XSMVER command - basic functionality
 * Tests that the command returns version information
 */
void test_xsmver_basic(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Send AT#XSMVER command */
	send_at_command("AT#XSMVER\r\n");

	/* Verify response contains version info */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XSMVER:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "customer_v1.0") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
}

/*
 * Test: AT#XSMVER with query (?) - should return error
 * The command only supports SET operation, not READ
 */
void test_xsmver_query_not_supported(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Send AT#XSMVER? command (query mode) */
	send_at_command("AT#XSMVER?\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XSMVER with test (=?) - should return error
 * The command only supports SET operation, not TEST
 */
void test_xsmver_test_not_supported(void)
{
	int ret;
	const char *response;

	ret = sm_at_host_init();
	TEST_ASSERT_EQUAL_INT(0, ret);

	/* Send AT#XSMVER=? command (test mode) */
	send_at_command("AT#XSMVER=?\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);

	sm_at_host_uninit();
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
