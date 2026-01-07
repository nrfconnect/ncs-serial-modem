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
#include "cmock_modem_jwt.h"

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
	const char *response;

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
	const char *response;

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
	const char *response;

	/* Send AT#XSMVER=? command (test mode) */
	send_at_command("AT#XSMVER=?\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XUUID command - basic functionality
 * Tests that the command returns device UUID
 */
void test_xuuid_basic(void)
{
	const char *response;
	struct nrf_device_uuid dev_uuid;

	/* Setup mock - device UUID */
	strcpy(dev_uuid.str, "50503041-3633-4261-803d-1e2b8f70111a");
	__cmock_modem_jwt_get_uuids_ExpectAndReturn(NULL, NULL, 0);
	__cmock_modem_jwt_get_uuids_IgnoreArg_dev();
	__cmock_modem_jwt_get_uuids_IgnoreArg_mfw();
	__cmock_modem_jwt_get_uuids_ReturnThruPtr_dev(&dev_uuid);

	/* Send AT#XUUID command */
	send_at_command("AT#XUUID\r\n");

	/* Verify response contains UUID */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XUUID:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "50503041-3633-4261-803d-1e2b8f70111a") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
}

/*
 * Test: AT#XUUID when modem_jwt_get_uuids fails
 * Tests error handling when UUID retrieval fails
 */
void test_xuuid_get_uuid_fails(void)
{
	const char *response;

	/* Setup mock - return error */
	__cmock_modem_jwt_get_uuids_ExpectAndReturn(NULL, NULL, -EINVAL);
	__cmock_modem_jwt_get_uuids_IgnoreArg_dev();
	__cmock_modem_jwt_get_uuids_IgnoreArg_mfw();

	/* Send AT#XUUID command */
	send_at_command("AT#XUUID\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XUUID with query (?) - should return error
 * The command only supports SET operation, not READ
 */
void test_xuuid_query_not_supported(void)
{
	const char *response;

	/* Send AT#XUUID? command (query mode) */
	send_at_command("AT#XUUID?\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XUUID with test (=?) - should return error
 * The command only supports SET operation, not TEST
 */
void test_xuuid_test_not_supported(void)
{
	const char *response;

	/* Send AT#XUUID=? command (test mode) */
	send_at_command("AT#XUUID=?\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XDATACTRL command - set time limit
 * Tests setting a valid time limit for data mode control
 */
void test_xdatactrl_set_valid(void)
{
	const char *response;

	/* Send AT#XDATACTRL command with valid time limit (100 ms) */
	send_at_command("AT#XDATACTRL=100\r\n");

	/* Verify successful response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
}

/*
 * Test: AT#XDATACTRL command - set invalid time limit (too small)
 * Tests that setting a time limit below minimum is rejected
 */
void test_xdatactrl_set_invalid(void)
{
	const char *response;

	/* Send AT#XDATACTRL command with invalid time limit (1 ms, too small) */
	send_at_command("AT#XDATACTRL=1\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XDATACTRL command - set zero time limit
 * Tests that setting zero time limit is rejected
 */
void test_xdatactrl_set_zero(void)
{
	const char *response;

	/* Send AT#XDATACTRL command with zero time limit */
	send_at_command("AT#XDATACTRL=0\r\n");

	/* Verify error response */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "ERROR") != NULL);
}

/*
 * Test: AT#XDATACTRL? command - read current configuration
 * Tests reading the current time limit and minimum time limit
 */
void test_xdatactrl_read(void)
{
	const char *response;

	/* First set a known value */
	send_at_command("AT#XDATACTRL=200\r\n");
	clear_captured_response();

	/* Now read it back */
	send_at_command("AT#XDATACTRL?\r\n");

	/* Verify response contains current and minimum time limits */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XDATACTRL:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "200") != NULL); /* Current value */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
}

/*
 * Test: AT#XDATACTRL=? command - test command syntax
 * Tests that the test command returns the command syntax
 */
void test_xdatactrl_test(void)
{
	const char *response;

	/* Send AT#XDATACTRL=? command (test mode) */
	send_at_command("AT#XDATACTRL=?\r\n");

	/* Verify response contains syntax */
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "#XDATACTRL=<time_limit>") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
}

/*
 * Test: ATE0/ATE1 commands - echo control
 * - Commands: ATE1\r\n (enable), ATE0\r\n (disable)
 * - Tests: Echo functionality - commands should be echoed when enabled, not echoed when disabled
 * - Expected: With ATE1, subsequent commands are echoed; with ATE0, they are not
 */
void test_ate_echo_control(void)
{
	const char *response;

	/* Enable echo with ATE1 */
	send_at_command("ATE1\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	clear_captured_response();

	/* Send a test command - it should be echoed back */
	send_at_command("AT#XSMVER\r\n");
	response = get_captured_response();
	printk("Response:\n%s\n", response);
	TEST_ASSERT_TRUE(strstr(response, "AT#XSMVER") != NULL); /* Command echoed */
	TEST_ASSERT_TRUE(strstr(response, "#XSMVER:") != NULL);  /* Response present */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	clear_captured_response();

	/* Disable echo with ATE0 */
	send_at_command("ATE0\r\n");
	response = get_captured_response();
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
	clear_captured_response();

	/* Send a test command - it should NOT be echoed back */
	send_at_command("AT#XSMVER\r\n");
	response = get_captured_response();
	printk("Response:\n%s\n", response);
	TEST_ASSERT_TRUE(strstr(response, "AT#XSMVER") == NULL); /* Command NOT echoed */
	TEST_ASSERT_TRUE(strstr(response, "#XSMVER:") != NULL);  /* Response still present */
	TEST_ASSERT_TRUE(strstr(response, "OK") != NULL);
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
