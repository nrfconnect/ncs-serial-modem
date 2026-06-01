/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file test_at_nrfcloud.c
 *
 * Unit tests for sm_at_nrfcloud.c.
 *
 * Covers:
 *   - AT#XNRFCLOUD  – connect / disconnect / send / read / test operations
 *   - AT#XNRFCLOUDPOS – parameter validation, success path, and URC output
 *   - %NCELLMEAS parsing (search types 1/2 and GCI 3/4) via the
 *     CONFIG_UNITY-visible wrappers sm_at_nrfcloud_test_parse_ncellmeas()
 *     and sm_at_nrfcloud_test_parse_ncellmeas_gci().
 *
 * %NCELLMEAS test data is taken from the nRF Location library test suite
 * (nrf/tests/lib/location/src/location_test.c).
 *
 * CMock naming convention used in this project
 * --------------------------------------------
 * The cmock_handle() macro uses the linker --defsym trick to rename the real
 * symbol   foo()  →  __cmock_foo().
 * CMock then provides a new   foo()  implementation (the mock).
 *
 * As a result the CMock helper macros are prefixed with __cmock_, e.g.:
 *   __cmock_nrf_cloud_coap_connect_ExpectAnyArgsAndReturn(0)
 *   __cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&r)
 *
 * The Init / Verify functions use the cmock_ prefix:
 *   cmock_nrf_cloud_coap_Init()
 *   cmock_nrf_cloud_coap_Verify()
 */

#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <modem/at_monitor.h>

#include "sm_at_host.h"
#include "sm_at_nrfcloud.h"
#include "uart_stub.h"

/* CMock-generated mocks */
#include "cmock_nrf_modem_at.h"
#include "cmock_nrf_cloud_coap.h"


static const char *resp;
static char *result;
extern char test_at_nrfcloud_ncellmeas_resp[];
extern int test_at_nrfcloud_ncellmeas_resp_ret;

/* ---------------------------------------------------------------------------
 * Externals provided by the stub / helper files
 * ---------------------------------------------------------------------------
 */
extern const char  *get_captured_response(void);
extern size_t       get_captured_response_len(void);
extern void         clear_captured_response(void);

/* at_monitor_dispatch() is implemented in at_monitor library and
 * we'll call it directly to fake received AT commands/notifications
 */
extern void at_monitor_dispatch(const char *at_notif);

/* Strings for cellular positioning */
static const char ncellmeas_resp_pci1[] =
	"%NCELLMEAS:0,\"00011B07\",\"26295\",\"00B7\",2300,7,63,31,"
	"150344527,2300,8,60,29,0,2400,11,55,26,184\r\n";

static const char ncellmeas_resp_gci1[] =
	"%NCELLMEAS:0,\"00011B07\",\"26295\",\"00B7\",10512,9034,2300,7,63,31,150344527,1,0,"
	"\"00011B08\",\"26295\",\"00B7\",65535,0,2300,9,62,30,150345527,0,0\r\n";

static const char ncellmeas_resp_gci5[] =
	"%NCELLMEAS:0,\"00011B07\",\"26295\",\"00B7\",10512,9034,2300,7,63,31,150344527,1,0,"
	"\"00011B66\",\"26287\",\"00C3\",65535,0,4300,6,71,30,150345527,0,0,"
	"\"0002ABCD\",\"26287\",\"00C3\",65535,0,4300,6,71,30,150345527,0,0,"
	"\"00103425\",\"26244\",\"0056\",65535,0,6400,6,71,30,150345527,0,0,"
	"\"00076543\",\"26256\",\"00C3\",65535,0,62000,6,71,30,150345527,0,0,"
	"\"00011B08\",\"26295\",\"00B7\",65535,0,2300,9,62,30,150345527,0,0\r\n";

/* Normal NCELLMEAS without neighbor cells: only serving cell data. */
static const char ncellmeas_resp_no_neighbors[] =
	"%NCELLMEAS:0,\"00011B07\",\"26295\",\"00B7\",500,6400,71,61,35,"
	"135488527\r\n";

/* ---------------------------------------------------------------------------
 * setUp / tearDown
 * ---------------------------------------------------------------------------
 */

void setUp(void)
{
	clear_captured_response();

	cmock_nrf_modem_at_Init();
	cmock_nrf_cloud_coap_Init();
	strcpy(test_at_nrfcloud_ncellmeas_resp, "\r\nOK\r\n");
	test_at_nrfcloud_ncellmeas_resp_ret = 0;
}

void tearDown(void)
{
	/* Drain any pending work to avoid interference between tests. */
	k_sleep(K_MSEC(1));

	cmock_nrf_modem_at_Verify();
	cmock_nrf_cloud_coap_Verify();
}

/* ---------------------------------------------------------------------------
 * Helper functions
 * ---------------------------------------------------------------------------
 */

void helper_xnrfcloud_connect_ok(void)
{
	/* nrfcloud_conn_work_fn calls nrf_cloud_coap_connect(NULL). */
	__cmock_nrf_cloud_coap_connect_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XNRFCLOUD=1\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	result = strstr(resp, "#XNRFCLOUD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUD: 1,0\r\n", result);
}

void helper_xnrfcloud_disconnect_ok(void)
{
	__cmock_nrf_cloud_coap_disconnect_ExpectAndReturn(0);

	send_at_command("AT#XNRFCLOUD=0\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	result = strstr(resp, "#XNRFCLOUD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUD: 0,0\r\n", result);
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUD tests
 * ---------------------------------------------------------------------------
 */

/*
 * Tests test command.
 */
void test_xnrfcloud_test_cmd(void)
{
	send_at_command("AT#XNRFCLOUD=?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUD: (0,1,2),<send_location>\r\n\r\nOK\r\n"));
}

/*
 * Tests read command when nRF Cloud is not connected.
 */
void test_xnrfcloud_read_disconnected(void)
{
	send_at_command("AT#XNRFCLOUD?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUD: 0,0,16842753,\"\"\r\n\r\nOK\r\n"));
}

/*
 * Tests connect.
 */
void test_xnrfcloud_connect_ok(void)
{
	sm_nrf_cloud_ready = false;

	helper_xnrfcloud_connect_ok();

	clear_captured_response();

	send_at_command("AT#XNRFCLOUD?\r\n");
	k_sleep(K_MSEC(1));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUD: 1,0,16842753,\"\"\r\n\r\nOK\r\n"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests connect with <send_location> = 1.
 */
void test_xnrfcloud_connect_with_send_location(void)
{
	__cmock_nrf_cloud_coap_connect_ExpectAnyArgsAndReturn(0);

	send_at_command("AT#XNRFCLOUD=1,1\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	k_sleep(K_MSEC(1));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUD?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUD: 1,1,16842753,\"\"\r\n\r\nOK\r\n"));

	/* Disconnect */
	clear_captured_response();
	__cmock_nrf_cloud_coap_disconnect_ExpectAndReturn(0);

	send_at_command("AT#XNRFCLOUD=0\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	result = strstr(resp, "#XNRFCLOUD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUD: 0,1\r\n", result);
}

/*
 * Tests invalid <send_location> value.
 */
void test_xnrfcloud_connect_invalid_send_location(void)
{
	send_at_command("AT#XNRFCLOUD=1,2\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests connect when there is nRF Cloud connection already.
 */
void test_xnrfcloud_connect_already_connected(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUD=1\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests disconnect.
 */
void test_xnrfcloud_disconnect_ok(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests disconnect when nRF Cloud is not connected.
 */
void test_xnrfcloud_disconnect_not_connected(void)
{
	send_at_command("AT#XNRFCLOUD=0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests sending data to cloud with AT#XNRFCLOUD=2 when nRF Cloud is not connected.
 */
void test_xnrfcloud_send_not_connected(void)
{
	send_at_command("AT#XNRFCLOUD=2\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests invalid operation value.
 */
void test_xnrfcloud_invalid_op(void)
{
	send_at_command("AT#XNRFCLOUD=3\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUDPOS tests
 * ---------------------------------------------------------------------------
 */

/*
 * Tests AT#XNRFCLOUDPOS when nRF Cloud is not connected.
 */
void test_xnrfcloudpos_not_connected(void)
{
	send_at_command("AT#XNRFCLOUDPOS=1,0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests read command not supported.
 */
void test_xnrfcloudpos_read_not_supported(void)
{
	send_at_command("AT#XNRFCLOUDPOS?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests test command not supported.
 */
void test_xnrfcloudpos_test_not_supported(void)
{
	send_at_command("AT#XNRFCLOUDPOS=?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests no positioning method requested.
 */
void test_xnrfcloudpos_no_pos_method(void)
{
	send_at_command("AT#XNRFCLOUDPOS=0,0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests too big <cell_count> value.
 */
void test_xnrfcloudpos_cell_count_too_high(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=16,0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests invalid <wifi_pos> value.
 */
void test_xnrfcloudpos_wifi_pos_invalid(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=0,2\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests missing <wifi_pos> parameter.
 */
void test_xnrfcloudpos_missing_params(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests no Wi-Fi but APs given.
 */
void test_xnrfcloudpos_no_wifi_but_ap_params(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=1,0,\"AA:BB:CC:DD:EE:FF\"\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests no Wi-Fi APs.
 */
void test_xnrfcloudpos_wifi_no_aps(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=0,1\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests invalid WiFi MAC address.
 */
void test_xnrfcloudpos_wifi_invalid_mac(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command(
		"AT#XNRFCLOUDPOS=0,1,\"GG:GG:GG:GG:GG:GG\",\"HH:11:22:33:44:55\"\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests #XNRFCLOUDPOS with one AT%NCELLMEAS command.
 */
void test_xnrfcloudpos_cell_1ncellmeas_ok(void)
{
	static struct nrf_cloud_location_result loc_result = {
		.type = LOCATION_TYPE_SINGLE_CELL,
		.lat  = 60.1699,
		.lon  = 24.9384,
		.unc  = 1000,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	/* Expect exactly one call to location_get; fill *result with loc_result. */
	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&loc_result);

	send_at_command("AT#XNRFCLOUDPOS=1,0\r\n");

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	/* NCELLMEAS notification */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_no_neighbors);
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDPOS: 0,0,60.169900,24.938400,1000\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests #XNRFCLOUDPOS with 3 AT%NCELLMEAS commands.
 */
void test_xnrfcloudpos_cell_3ncellmeas_ok(void)
{
	static struct nrf_cloud_location_result loc_result = {
		.type = LOCATION_TYPE_MULTI_CELL,
		.lat  = 12.345678,
		.lon  = -57.987654,
		.unc  = 1234,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	/* Expect exactly one call to location_get; fill *result with loc_result. */
	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&loc_result);

	send_at_command("AT#XNRFCLOUDPOS=15,0\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	/* NCELLMEAS notifications */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_pci1);
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_gci1);
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_gci5);
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING_LEN("#XNRFCLOUDPOS: 0,1,12.345678,-57.987654,1234\r\n", result,
		strlen("#XNRFCLOUDPOS: 0,1,12.345678,-57.987654,1234\r\n"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests #XNRFCLOUDPOS with 2 AT%NCELLMEAS commands.
 */
void test_xnrfcloudpos_cell_2ncellmeas_ok(void)
{
	static struct nrf_cloud_location_result loc_result = {
		.type = LOCATION_TYPE_MULTI_CELL,
		.lat  = 12.345678,
		.lon  = -57.987654,
		.unc  = 1234,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	/* Expect exactly one call to location_get; fill *result with loc_result. */
	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&loc_result);

	send_at_command("AT#XNRFCLOUDPOS=4,0\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	/* NCELLMEAS notifications */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_pci1);
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_gci5);
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING_LEN("#XNRFCLOUDPOS: 0,1,12.345678,-57.987654,1234\r\n", result,
		strlen("#XNRFCLOUDPOS: 0,1,12.345678,-57.987654,1234\r\n"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests failing AT%NCELLMEAS command.
 */
void test_xnrfcloudpos_cell_ncellmeas_fail_ok(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	strcpy(test_at_nrfcloud_ncellmeas_resp, "\r\nERROR\r\n");
	test_at_nrfcloud_ncellmeas_resp_ret = -1;

	send_at_command("AT#XNRFCLOUDPOS=1,0\r\n");
	k_sleep(K_MSEC(1));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDPOS: -1\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests %NCELLMEAS notification having failure status.
 */
void test_xnrfcloudpos_cell_ncellmeas_notif_fail(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=4,0\r\n");
	/* Test that unsolicited NCELLMEAS notifications are ignored before
	 * the first AT%NCELLMEAS commands. This gets to the NCELLMEAS handler
	 * before the first AT%NCELLMEAS command because we don't sleep between
	 * AT#XNRFCLOUDPOS and at_monitor_dispatch().
	 */
	at_monitor_dispatch(ncellmeas_resp_pci1);
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	/* NCELLMEAS notifications */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch("%NCELLMEAS:1\r\n");
	k_sleep(K_MSEC(1));
	at_monitor_dispatch("%NCELLMEAS:notnumber\r\n");
	k_sleep(K_MSEC(1));
	at_monitor_dispatch("%NCELLMEAS:1\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING_LEN("#XNRFCLOUDPOS: -1\r\n", result,
		strlen("#XNRFCLOUDPOS: -1\r\n"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests failing cloud location request.
 */
void test_xnrfcloudpos_cell_cloud_request_fail(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	/* Expect exactly one call to location_get; fill *result with loc_result. */
	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(40100);

	send_at_command("AT#XNRFCLOUDPOS=4,0\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	/* NCELLMEAS notifications */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_pci1);
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_gci5);
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING_LEN("#XNRFCLOUDPOS: 40100\r\n", result,
		strlen("#XNRFCLOUDPOS: 40100\r\n"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests failing AT%NCELLMEAS command when Wi-Fi APs available.
 */
void test_xnrfcloudpos_cell_ncellmeas_notif_fail_wifi_ok(void)
{
	static struct nrf_cloud_location_result loc_result = {
		.type = LOCATION_TYPE_WIFI,
		.lat  = 60.1699,
		.lon  = 24.9384,
		.unc  = 50,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&loc_result);

	send_at_command("AT#XNRFCLOUDPOS=4,1,\"C0:FF:EE:00:11:22\",\"DE:AD:BE:EF:CA:FE\"\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	/* NCELLMEAS notifications */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch("%NCELLMEAS:1\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDPOS: 0,2,60.169900,24.938400,50\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests Wi-Fi only positioning.
 */
void test_xnrfcloudpos_wifi_only_ok(void)
{
	static struct nrf_cloud_location_result loc_result = {
		.type = LOCATION_TYPE_WIFI,
		.lat  = 60.1699,
		.lon  = 24.9384,
		.unc  = 50,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&loc_result);

	k_sleep(K_MSEC(1));
	send_at_command("AT#XNRFCLOUDPOS=0,1,\"C0:FF:EE:00:11:22\",\"DE:AD:BE:EF:CA:FE\"\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	k_sleep(K_MSEC(1));
	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDPOS");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDPOS: 0,2,60.169900,24.938400,50\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests failure when only one Wi-Fi AP given.
 */
void test_xnrfcloudpos_wifi_only_one_ap(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDPOS=0,1,\"AA:BB:CC:DD:EE:FF\",-60\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();

	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests combined cellular and Wi-Fi positioning.
 */
void test_xnrfcloudpos_cell_and_wifi_ok(void)
{
	static struct nrf_cloud_location_result loc_result = {
		.type = LOCATION_TYPE_MULTI_CELL,
		.lat  = 60.1699,
		.lon  = 24.9384,
		.unc  = 200,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	__cmock_nrf_cloud_coap_location_get_ExpectAnyArgsAndReturn(0);
	__cmock_nrf_cloud_coap_location_get_ReturnThruPtr_result(&loc_result);

	send_at_command(
		"AT#XNRFCLOUDPOS=8,1,"
		"\"C0:FF:EE:00:11:22\",-55,"
		"\"DE:AD:BE:EF:CA:FE\",-70\r\n");
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	/* NCELLMEAS notifications */
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_pci1);
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_gci5);
	k_sleep(K_MSEC(1));
	at_monitor_dispatch(ncellmeas_resp_gci5);
	k_sleep(K_MSEC(1));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "#XNRFCLOUDPOS:"),
				     "Expected #XNRFCLOUDPOS URC in response");
	/* type=1 (LOCATION_TYPE_MULTI_CELL) */
	TEST_ASSERT_NOT_NULL(strstr(resp, "1,"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/* ---------------------------------------------------------------------------
 * Main and sys init
 * ---------------------------------------------------------------------------
 */

/* This is needed because AT Monitor library is initialized in SYS_INIT. */
static int test_at_nrfcloud_sys_init(void)
{
	__cmock_nrf_modem_at_notif_handler_set_ExpectAnyArgsAndReturn(0);

	return 0;
}

SYS_INIT(test_at_nrfcloud_sys_init, POST_KERNEL, 0);

extern int unity_main(void);

int main(void)
{
	(void)unity_main();
	return 0;
}
