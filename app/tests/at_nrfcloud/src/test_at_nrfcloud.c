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
#include <zephyr/settings/settings.h>
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

/* Memfault stubs (memfault_stubs.c) */
#include <sys/types.h>
#include "memfault/core/data_packetizer.h"
#include "memfault/http/http_client.h"
extern bool test_memfault_periodic_upload_enabled;
extern bool test_memfault_periodic_upload_logs_enabled;
extern int test_memfault_heartbeat_trigger_count;
extern ssize_t test_memfault_upload_return;
extern int test_memfault_log_trigger_count;
extern bool test_memfault_log_triggered_before_upload;
extern const char *test_memfault_upload_seen_api_key;
extern char test_memfault_crash_type[];
extern int test_memfault_crash_call_count;
extern int test_memfault_crash_return;
extern void test_memfault_set_chunks(const char * const chunks[], size_t count);
extern void test_memfault_stubs_reset(void);

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
	test_memfault_stubs_reset();
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
 * AT#XNRFCLOUDOBS* tests
 * ---------------------------------------------------------------------------
 */

/* Chunk used by the forward tests: "hello" base64-encoded. */
#define OBS_CHUNK_B64 "aGVsbG8="
#define OBS_CHUNK_BIN "hello"

/* A second, distinct chunk: "world" base64-encoded. Used where a test has to tell the
 * chunk of one command from the chunk of another.
 */
#define OBS_CHUNK2_B64 "d29ybGQ="
#define OBS_CHUNK2_BIN "world"

/* Captured arguments of the faked nrf_cloud_coap_post(). */
static char obs_post_resource[32];
static char obs_post_query[80];
static bool obs_post_query_null;
static uint8_t obs_post_payload[32];
static size_t obs_post_len;
static int obs_post_fmt;
static const char *obs_post_api_key;
static int obs_post_call_count;
/* Injected: CoAP result code the fake reports through the callback. */
static int16_t obs_post_result_code;
/* Captured by obs_post_fake_no_cb(), so that a test can deliver a late response. */
static coap_client_response_cb_t obs_post_saved_cb;
static void *obs_post_saved_user;

/* Settings handlers of sm_at_nrfcloud_obs.c, declared STATIC there so that the tests can
 * replay a stored configuration without a settings back-end.
 */
extern int obs_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg);
extern int obs_settings_commit(void);

static void obs_reset_post_capture(void)
{
	obs_post_saved_cb = NULL;
	obs_post_saved_user = NULL;
	memset(obs_post_resource, 0, sizeof(obs_post_resource));
	memset(obs_post_query, 0, sizeof(obs_post_query));
	memset(obs_post_payload, 0, sizeof(obs_post_payload));
	obs_post_query_null = false;
	obs_post_len = 0;
	obs_post_fmt = 0;
	obs_post_api_key = NULL;
	obs_post_call_count = 0;
	obs_post_result_code = COAP_RESPONSE_CODE_CREATED;
}

/* The configuration of the automatic upload is module state that survives setUp(), and an
 * enabled automatic upload leaves a timer armed. Every test that touches it starts and
 * ends from a known state.
 */
static void obs_auto_reset(void)
{
	send_at_command("AT#XNRFCLOUDOBSAUTO=0,3600,\"\"\r\n");
	k_sleep(K_MSEC(1));
	clear_captured_response();
}

/* Fake for nrf_cloud_coap_post(): captures the arguments and invokes the
 * response callback, which the handler waits for on a semaphore.
 */
static int obs_post_fake(const char *resource, const char *query, const uint8_t *buf, size_t len,
			 int fmt, bool reliable, coap_client_response_cb_t cb, void *user,
			 int cmock_num_calls)
{
	ARG_UNUSED(reliable);
	ARG_UNUSED(cmock_num_calls);

	obs_post_call_count++;
	strncpy(obs_post_resource, resource ? resource : "", sizeof(obs_post_resource) - 1);
	if (query == NULL) {
		obs_post_query_null = true;
	} else {
		strncpy(obs_post_query, query, sizeof(obs_post_query) - 1);
	}
	obs_post_len = MIN(len, sizeof(obs_post_payload));
	memcpy(obs_post_payload, buf, obs_post_len);
	obs_post_fmt = fmt;
	/* The project key is installed only for the duration of the operation. */
	obs_post_api_key = g_mflt_http_client_config.api_key;

	if (cb) {
		struct coap_client_response_data data = {
			.result_code = obs_post_result_code,
			.last_block = true,
		};

		cb(&data, user);
	}

	return 0;
}

/* Fake for nrf_cloud_coap_post() that accepts the request but never responds, so
 * that the handler times out. The callback is saved so that the test can deliver
 * the response late, after the handler gave up waiting for it.
 */
static int obs_post_fake_no_cb(const char *resource, const char *query, const uint8_t *buf,
			       size_t len, int fmt, bool reliable, coap_client_response_cb_t cb,
			       void *user, int cmock_num_calls)
{
	ARG_UNUSED(resource);
	ARG_UNUSED(query);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	ARG_UNUSED(fmt);
	ARG_UNUSED(reliable);
	ARG_UNUSED(cmock_num_calls);

	obs_post_call_count++;
	obs_post_saved_cb = cb;
	obs_post_saved_user = user;

	return 0;
}

/* Feeds a stored settings value to obs_settings_set(). */
struct obs_settings_blob {
	const void *data;
	size_t len;
};

static ssize_t obs_settings_read_fake(void *cb_arg, void *data, size_t len)
{
	struct obs_settings_blob *blob = cb_arg;
	size_t copied = MIN(len, blob->len);

	memcpy(data, blob->data, copied);

	return copied;
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUDOBSAUTO
 * ---------------------------------------------------------------------------
 */

/*
 * Tests that the automatic upload starts disabled, that it can be enabled and disabled,
 * and that the read command reports the state.
 */
void test_xnrfcloudobsauto_enable_disable(void)
{
	obs_auto_reset();

	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 0,3600,\"\"\r\n"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO=1\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 1,3600,\"\"\r\n"));

	obs_auto_reset();

	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 0,3600,\"\"\r\n"));
}

/*
 * Tests that the interval and the project key are stored and reported back.
 */
void test_xnrfcloudobsauto_interval_and_key(void)
{
	obs_auto_reset();

	send_at_command("AT#XNRFCLOUDOBSAUTO=1,600,\"prj_AUTOKEY\"\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 1,600,\"prj_AUTOKEY\"\r\n"));

	obs_auto_reset();
}

/*
 * Tests that an empty project key clears the stored one, back to the server-side routing,
 * and that omitting the parameter keeps it.
 */
void test_xnrfcloudobsauto_clear_key(void)
{
	obs_auto_reset();

	send_at_command("AT#XNRFCLOUDOBSAUTO=1,600,\"prj_AUTOKEY\"\r\n");
	k_sleep(K_MSEC(10));
	clear_captured_response();

	/* No key parameter at all: the stored key is kept. */
	send_at_command("AT#XNRFCLOUDOBSAUTO=1,900\r\n");
	k_sleep(K_MSEC(10));
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 1,900,\"prj_AUTOKEY\"\r\n"));

	/* An empty key clears it. */
	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO=1,900,\"\"\r\n");
	k_sleep(K_MSEC(10));
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 1,900,\"\"\r\n"));

	obs_auto_reset();
}

/*
 * Tests that an omitted interval keeps the stored one, so that the project key can be
 * given without repeating the interval.
 */
void test_xnrfcloudobsauto_omitted_interval_keeps_stored(void)
{
	obs_auto_reset();

	send_at_command("AT#XNRFCLOUDOBSAUTO=1,1200\r\n");
	k_sleep(K_MSEC(10));
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDOBSAUTO=1,,\"prj_LATER\"\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 1,1200,\"prj_LATER\"\r\n"));

	obs_auto_reset();
}

/*
 * Tests that an out-of-range interval and an invalid enable value are rejected, and that
 * the stored configuration is left untouched.
 */
void test_xnrfcloudobsauto_invalid_params(void)
{
	obs_auto_reset();

	send_at_command("AT#XNRFCLOUDOBSAUTO=2\r\n");
	k_sleep(K_MSEC(10));
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO=1,59\r\n");
	k_sleep(K_MSEC(10));
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO=1,86401\r\n");
	k_sleep(K_MSEC(10));
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));

	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 0,3600,\"\"\r\n"));
}

/*
 * Tests that the automatic upload runs when the interval expires, that it collects the
 * logs first, that it uses the stored project key, and that it rearms itself.
 */
void test_xnrfcloudobsauto_uploads_on_interval(void)
{
	obs_auto_reset();
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	test_memfault_upload_return = 64;

	send_at_command("AT#XNRFCLOUDOBSAUTO=1,60,\"prj_AUTOKEY\"\r\n");
	k_sleep(K_MSEC(10));
	TEST_ASSERT_EQUAL_INT(0, test_memfault_log_trigger_count);

	/* The simulated clock jumps ahead, so this does not take a minute of wall time. */
	k_sleep(K_SECONDS(61));

	TEST_ASSERT_EQUAL_INT(1, test_memfault_log_trigger_count);
	TEST_ASSERT_TRUE(test_memfault_log_triggered_before_upload);
	TEST_ASSERT_EQUAL_STRING("prj_AUTOKEY", test_memfault_upload_seen_api_key);
	/* The key is only installed for the upload. */
	TEST_ASSERT_EQUAL_STRING("", g_mflt_http_client_config.api_key);
	/* The automatic upload is silent: the host asked for it once. */
	TEST_ASSERT_NULL(strstr(get_captured_response(), "#XNRFCLOUDOBS"));

	/* It rearmed itself for the next interval. */
	k_sleep(K_SECONDS(61));
	TEST_ASSERT_EQUAL_INT(2, test_memfault_log_trigger_count);

	obs_auto_reset();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that the automatic upload is skipped, and rearmed, while there is no connection to
 * nRF Cloud.
 */
void test_xnrfcloudobsauto_skipped_when_disconnected(void)
{
	obs_auto_reset();
	sm_nrf_cloud_ready = false;

	send_at_command("AT#XNRFCLOUDOBSAUTO=1,60\r\n");
	k_sleep(K_MSEC(10));
	clear_captured_response();

	k_sleep(K_SECONDS(61));
	TEST_ASSERT_EQUAL_INT(0, test_memfault_log_trigger_count);

	/* Still armed: the upload resumes once the host connects. */
	helper_xnrfcloud_connect_ok();
	k_sleep(K_SECONDS(61));
	TEST_ASSERT_EQUAL_INT(1, test_memfault_log_trigger_count);

	obs_auto_reset();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a configuration loaded from settings is applied and arms the automatic
 * upload, which is what happens after a reboot.
 */
void test_xnrfcloudobsauto_stored_config_is_applied(void)
{
	bool stored_enabled = true;
	uint32_t stored_interval = 300;
	static const char stored_key[] = "prj_STORED";
	struct obs_settings_blob blob;

	obs_auto_reset();

	blob.data = &stored_enabled;
	blob.len = sizeof(stored_enabled);
	TEST_ASSERT_EQUAL_INT(0, obs_settings_set("auto", sizeof(stored_enabled),
						  obs_settings_read_fake, &blob));

	blob.data = &stored_interval;
	blob.len = sizeof(stored_interval);
	TEST_ASSERT_EQUAL_INT(0, obs_settings_set("interval", sizeof(stored_interval),
						  obs_settings_read_fake, &blob));

	blob.data = stored_key;
	blob.len = strlen(stored_key);
	TEST_ASSERT_EQUAL_INT(0, obs_settings_set("key", strlen(stored_key),
						  obs_settings_read_fake, &blob));

	TEST_ASSERT_EQUAL_INT(0, obs_settings_commit());

	send_at_command("AT#XNRFCLOUDOBSAUTO?\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XNRFCLOUDOBSAUTO: 1,300,\"prj_STORED\"\r\n"));

	/* The commit armed the upload, which now runs on the stored interval. */
	clear_captured_response();
	helper_xnrfcloud_connect_ok();
	clear_captured_response();
	k_sleep(K_SECONDS(301));
	TEST_ASSERT_EQUAL_INT(1, test_memfault_log_trigger_count);
	TEST_ASSERT_EQUAL_STRING("prj_STORED", test_memfault_upload_seen_api_key);

	obs_auto_reset();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that an unknown stored setting does not fail the load.
 */
void test_xnrfcloudobsauto_unknown_setting_is_ignored(void)
{
	uint8_t value = 1;
	struct obs_settings_blob blob = {&value, sizeof(value)};

	TEST_ASSERT_EQUAL_INT(0, obs_settings_set("obsolete", sizeof(value),
						  obs_settings_read_fake, &blob));
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUDOBSUPLOAD
 * ---------------------------------------------------------------------------
 */

/*
 * Tests that the on-demand upload requires a connection to nRF Cloud.
 */
void test_xnrfcloudobsupload_not_connected(void)
{
	sm_nrf_cloud_ready = false;

	send_at_command("AT#XNRFCLOUDOBSUPLOAD\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests a successful on-demand upload: OK first, then the result URC with the number of
 * bytes uploaded. Also verifies that the log collection is triggered before the upload
 * runs, so that the captured logs are included.
 */
void test_xnrfcloudobsupload_ok(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	test_memfault_upload_return = 1284;

	send_at_command("AT#XNRFCLOUDOBSUPLOAD\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));

	result = strstr(resp, "#XNRFCLOUDOBSUPLOAD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSUPLOAD: 0,1284\r\n", result);

	/* The logs are collected before draining. */
	TEST_ASSERT_EQUAL_INT(1, test_memfault_log_trigger_count);
	TEST_ASSERT_TRUE(test_memfault_log_triggered_before_upload);

	/* No project key given, so the server-side routing is used. */
	TEST_ASSERT_EQUAL_STRING("", test_memfault_upload_seen_api_key);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that an upload failure is reported as -1, whatever the error code of the
 * transport was.
 */
void test_xnrfcloudobsupload_error(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	test_memfault_upload_return = -116; /* -ETIMEDOUT */

	send_at_command("AT#XNRFCLOUDOBSUPLOAD\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	result = strstr(resp, "#XNRFCLOUDOBSUPLOAD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSUPLOAD: -1\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a project key overrides the routing for this upload only.
 */
void test_xnrfcloudobsupload_with_project_key(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	test_memfault_upload_return = 32;

	send_at_command("AT#XNRFCLOUDOBSUPLOAD=\"prj_TESTKEY\"\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDOBSUPLOAD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSUPLOAD: 0,32\r\n", result);

	/* The key was installed while uploading... */
	TEST_ASSERT_EQUAL_STRING("prj_TESTKEY", test_memfault_upload_seen_api_key);
	/* ...and restored afterwards. */
	TEST_ASSERT_EQUAL_STRING("", g_mflt_http_client_config.api_key);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUDOBSHEARTBEAT
 * ---------------------------------------------------------------------------
 */

/*
 * Tests that the heartbeat is collected. It does not require a connection to nRF Cloud,
 * because the data is buffered on the device until it is uploaded.
 */
void test_xnrfcloudobsheartbeat(void)
{
	sm_nrf_cloud_ready = false;

	send_at_command("AT#XNRFCLOUDOBSHEARTBEAT\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NULL(strstr(resp, "ERROR"));
	TEST_ASSERT_EQUAL_INT(1, test_memfault_heartbeat_trigger_count);
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUDOBSFORWARD
 * ---------------------------------------------------------------------------
 */

/*
 * Tests that the chunk is required.
 */
void test_xnrfcloudobsforward_missing_chunk(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDOBSFORWARD\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a malformed base64 chunk is rejected.
 */
void test_xnrfcloudobsforward_bad_base64(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"!!!not-base64!!!\"\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that forwarding requires a connection to nRF Cloud.
 */
void test_xnrfcloudobsforward_not_connected(void)
{
	sm_nrf_cloud_ready = false;

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n");
	k_sleep(K_MSEC(10));
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests forwarding a chunk: the base64 payload is decoded and posted to the
 * "chunks" resource as an octet stream, without a query.
 */
void test_xnrfcloudobsforward_ok(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: 0\r\n", result);

	TEST_ASSERT_EQUAL_INT(1, obs_post_call_count);
	TEST_ASSERT_EQUAL_STRING("chunks", obs_post_resource);
	/* No query is sent: the chunk carries the identity of the device that produced it,
	 * and nRF Cloud attributes it to the authenticated device.
	 */
	TEST_ASSERT_TRUE(obs_post_query_null);
	TEST_ASSERT_EQUAL_INT(COAP_CONTENT_FORMAT_APP_OCTET_STREAM, obs_post_fmt);
	/* The decoded chunk is posted, not the base64 text. */
	TEST_ASSERT_EQUAL_UINT(strlen(OBS_CHUNK_BIN), obs_post_len);
	TEST_ASSERT_EQUAL_MEMORY(OBS_CHUNK_BIN, obs_post_payload, strlen(OBS_CHUNK_BIN));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a project key overrides the routing while the chunk is posted and
 * is restored afterwards.
 */
void test_xnrfcloudobsforward_with_project_key(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\",\"prj_TESTKEY\"\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: 0\r\n", result);

	/* The key was installed while posting... */
	TEST_ASSERT_EQUAL_STRING("prj_TESTKEY", obs_post_api_key);
	/* ...and restored afterwards. */
	TEST_ASSERT_EQUAL_STRING("", g_mflt_http_client_config.api_key);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a non-success CoAP response is reported as an error.
 */
void test_xnrfcloudobsforward_coap_error(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	obs_post_result_code = 128; /* 4.00 Bad Request */
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: -1\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests the timeout of a forwarded chunk, and that the late response of the
 * timed-out request is not taken for the response of the next request. A shared
 * context without the outstanding guard would report the late result to whoever
 * re-armed it.
 */
void test_xnrfcloudobsforward_timeout_and_late_response(void)
{
	struct coap_client_response_data late = {
		.result_code = COAP_RESPONSE_CODE_CREATED,
		.last_block = true,
	};

	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake_no_cb);

	/* The request is accepted but never answered. */
	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n");
	k_sleep(K_MSEC(100));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	/* The timeout is reported as -1, like every other failure. */
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: -1\r\n", result);
	TEST_ASSERT_EQUAL_INT(1, obs_post_call_count);
	TEST_ASSERT_NOT_NULL(obs_post_saved_cb);

	/* While that response is missing, a new request is refused instead of reusing
	 * the context.
	 */
	clear_captured_response();
	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	/* The refusal is reported as -1 as well; the log distinguishes it. */
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: -1\r\n", result);
	/* Nothing was posted for the refused request. */
	TEST_ASSERT_EQUAL_INT(1, obs_post_call_count);

	/* The response of the first request finally arrives and is dropped. */
	clear_captured_response();
	obs_post_saved_cb(&late, obs_post_saved_user);
	k_sleep(K_MSEC(10));
	TEST_ASSERT_NULL(strstr(get_captured_response(), "#XNRFCLOUDOBS"));

	/* The context is usable again. */
	clear_captured_response();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: 0\r\n", result);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a second AT#XNRFCLOUDOBSFORWARD arriving before the work of the first one has
 * run is refused without disturbing the chunk that is already pending.
 *
 * Both commands are written in a single UART transfer, so sm_at_receive() dispatches both
 * from one at_pipe_rx_work_fn() invocation. sm_work_q is a single thread, so the work item
 * submitted by the first command cannot run until that invocation returns, which makes the
 * overlap deterministic rather than a matter of timing.
 *
 * The second command has to leave the state of the first one alone. It does not, because
 * obs_parse_chunk() allocates into the shared obs_chunk before obs_submit_async() checks
 * obs_busy: the pointer of the first chunk is overwritten and leaked, and the -EBUSY
 * cleanup then frees and clears the buffer that the pending work still has to post.
 */
void test_xnrfcloudobsforward_second_command_keeps_the_first_chunk(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\"\r\n"
			"AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK2_B64 "\"\r\n");
	k_sleep(K_MSEC(50));

	resp = get_captured_response();

	/* The second command is refused while the first one is ongoing. */
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	/* The first command reports success to the host... */
	result = strstr(resp, "#XNRFCLOUDOBSFORWARD");
	TEST_ASSERT_EQUAL_STRING("#XNRFCLOUDOBSFORWARD: 0\r\n", result);

	/* ...so exactly one chunk has to have been posted... */
	TEST_ASSERT_EQUAL_INT(1, obs_post_call_count);

	/* ...and it has to be the chunk of the first command, intact: neither cleared nor
	 * replaced by the chunk of the second one.
	 */
	TEST_ASSERT_EQUAL_UINT(strlen(OBS_CHUNK_BIN), obs_post_len);
	TEST_ASSERT_EQUAL_MEMORY(OBS_CHUNK_BIN, obs_post_payload, strlen(OBS_CHUNK_BIN));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that the project key of a refused second AT#XNRFCLOUDOBSFORWARD does not retarget
 * the chunk of the first one. The project key is saved in the same shared block as the
 * chunk, so parsing it before the refusal would send the data of the first command to the
 * Memfault project of the second.
 */
void test_xnrfcloudobsforward_second_command_keeps_the_first_key(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\",\"prj_FIRST\"\r\n"
			"AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK2_B64 "\",\"prj_SECOND\"\r\n");
	k_sleep(K_MSEC(50));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	/* The single post carries the key of the first command. */
	TEST_ASSERT_EQUAL_INT(1, obs_post_call_count);
	TEST_ASSERT_EQUAL_STRING("prj_FIRST", obs_post_api_key);

	/* And its own chunk. */
	TEST_ASSERT_EQUAL_UINT(strlen(OBS_CHUNK_BIN), obs_post_len);
	TEST_ASSERT_EQUAL_MEMORY(OBS_CHUNK_BIN, obs_post_payload, strlen(OBS_CHUNK_BIN));

	/* The key is still restored once the operation is over. */
	TEST_ASSERT_EQUAL_STRING("", g_mflt_http_client_config.api_key);

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/*
 * Tests that a different observability command is refused while a forward is pending, and
 * that it leaves the pending parameters alone. obs_busy is shared by every asynchronous
 * operation, so AT#XNRFCLOUDOBSUPLOAD parses into the same saved project key.
 */
void test_xnrfcloudobsupload_during_forward_keeps_the_forward(void)
{
	helper_xnrfcloud_connect_ok();
	clear_captured_response();

	obs_reset_post_capture();
	__cmock_nrf_cloud_coap_post_Stub(obs_post_fake);
	test_memfault_upload_return = 32;

	send_at_command("AT#XNRFCLOUDOBSFORWARD=\"" OBS_CHUNK_B64 "\",\"prj_FIRST\"\r\n"
			"AT#XNRFCLOUDOBSUPLOAD=\"prj_SECOND\"\r\n");
	k_sleep(K_MSEC(50));

	resp = get_captured_response();

	/* The upload is refused, and never runs. */
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
	TEST_ASSERT_NULL(strstr(resp, "#XNRFCLOUDOBSUPLOAD:"));
	TEST_ASSERT_NULL(test_memfault_upload_seen_api_key);

	/* The forward runs with its own key and chunk. */
	TEST_ASSERT_EQUAL_INT(1, obs_post_call_count);
	TEST_ASSERT_EQUAL_STRING("prj_FIRST", obs_post_api_key);
	TEST_ASSERT_EQUAL_UINT(strlen(OBS_CHUNK_BIN), obs_post_len);
	TEST_ASSERT_EQUAL_MEMORY(OBS_CHUNK_BIN, obs_post_payload, strlen(OBS_CHUNK_BIN));

	clear_captured_response();
	helper_xnrfcloud_disconnect_ok();
}

/* ---------------------------------------------------------------------------
 * AT#XNRFCLOUDOBSDEVINFO, AT#XNRFCLOUDOBSCRASH and AT#XNRFCLOUDOBSEXPORT
 * ---------------------------------------------------------------------------
 */

/*
 * Tests that the Memfault device information is returned.
 */
void test_xnrfcloudobsdevinfo(void)
{
	static const char expected[] = "#XNRFCLOUDOBSDEVINFO: \"test-device-id\","
				       "\"serial_modem\",\"1.2.3\",\"nrf9151dk\"\r\n";

	send_at_command("AT#XNRFCLOUDOBSDEVINFO\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
}

/*
 * Tests that the crash defaults to crash type 0.
 */
void test_xnrfcloudobscrash_default_type(void)
{
	send_at_command("AT#XNRFCLOUDOBSCRASH\r\n");
	k_sleep(K_MSEC(10));

	TEST_ASSERT_EQUAL_INT(1, test_memfault_crash_call_count);
	TEST_ASSERT_EQUAL_STRING("0", test_memfault_crash_type);
}

/*
 * Tests that the requested crash type is forwarded.
 */
void test_xnrfcloudobscrash_with_type(void)
{
	send_at_command("AT#XNRFCLOUDOBSCRASH=3\r\n");
	k_sleep(K_MSEC(10));

	TEST_ASSERT_EQUAL_INT(1, test_memfault_crash_call_count);
	TEST_ASSERT_EQUAL_STRING("3", test_memfault_crash_type);
}

/*
 * Tests that an invalid crash type is rejected. The real implementation does
 * not return when the type is valid.
 */
void test_xnrfcloudobscrash_invalid_type(void)
{
	test_memfault_crash_return = -1;

	send_at_command("AT#XNRFCLOUDOBSCRASH=9\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
}

/*
 * Tests that the buffered chunks are printed to the AT interface in the Memfault chunk
 * export format.
 */
void test_xnrfcloudobsexport_chunks(void)
{
	/* "hello" and "hi" encode to "aGVsbG8=" and "aGk=". */
	static const char * const chunks[] = {"hello", "hi"};

	test_memfault_set_chunks(chunks, ARRAY_SIZE(chunks));

	send_at_command("AT#XNRFCLOUDOBSEXPORT\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "MC:aGVsbG8=:"));
	TEST_ASSERT_NOT_NULL(strstr(resp, "MC:aGk=:"));
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
}

/*
 * Tests that the export succeeds when there is nothing buffered.
 */
void test_xnrfcloudobsexport_no_chunks(void)
{
	send_at_command("AT#XNRFCLOUDOBSEXPORT\r\n");
	k_sleep(K_MSEC(10));

	resp = get_captured_response();
	TEST_ASSERT_NULL(strstr(resp, "MC:"));
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
}

/* ---------------------------------------------------------------------------
 * Test and read commands
 * ---------------------------------------------------------------------------
 */

/*
 * Tests that the observability commands with parameters return the supported syntax.
 */
void test_xnrfcloudobs_test_cmds(void)
{
	static const struct {
		const char *cmd;
		const char *response;
	} test_cmds[] = {
		{"AT#XNRFCLOUDOBSAUTO=?\r\n",
		 "#XNRFCLOUDOBSAUTO: (0,1),(60-86400),<project_key>\r\n"},
		{"AT#XNRFCLOUDOBSUPLOAD=?\r\n", "#XNRFCLOUDOBSUPLOAD: <project_key>\r\n"},
		{"AT#XNRFCLOUDOBSFORWARD=?\r\n",
		 "#XNRFCLOUDOBSFORWARD: <base64_chunk>,<project_key>\r\n"},
		{"AT#XNRFCLOUDOBSCRASH=?\r\n", "#XNRFCLOUDOBSCRASH: <type>\r\n"},
	};

	for (size_t i = 0; i < ARRAY_SIZE(test_cmds); i++) {
		clear_captured_response();
		send_at_command(test_cmds[i].cmd);
		k_sleep(K_MSEC(10));

		resp = get_captured_response();
		TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "OK"), test_cmds[i].cmd);
		TEST_ASSERT_NULL_MESSAGE(strstr(resp, "ERROR"), test_cmds[i].cmd);
		TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, test_cmds[i].response),
					     test_cmds[i].cmd);
	}
}

/*
 * Tests that the commands without parameters do not support the test command, which would
 * have nothing to report.
 */
void test_xnrfcloudobs_test_cmds_not_supported(void)
{
	static const char *const test_cmds[] = {
		"AT#XNRFCLOUDOBSHEARTBEAT=?\r\n",
		"AT#XNRFCLOUDOBSDEVINFO=?\r\n",
		"AT#XNRFCLOUDOBSEXPORT=?\r\n",
	};

	for (size_t i = 0; i < ARRAY_SIZE(test_cmds); i++) {
		clear_captured_response();
		send_at_command(test_cmds[i]);
		k_sleep(K_MSEC(10));

		resp = get_captured_response();
		TEST_ASSERT_NOT_NULL_MESSAGE(strstr(resp, "ERROR"), test_cmds[i]);
	}
}

/*
 * Tests that the read command is only supported by AT#XNRFCLOUDOBSAUTO, which is the only
 * command with state to report.
 */
void test_xnrfcloudobs_read_not_supported(void)
{
	static const char * const cmds[] = {
		"AT#XNRFCLOUDOBSUPLOAD?\r\n",
		"AT#XNRFCLOUDOBSHEARTBEAT?\r\n",
		"AT#XNRFCLOUDOBSFORWARD?\r\n",
		"AT#XNRFCLOUDOBSDEVINFO?\r\n",
		"AT#XNRFCLOUDOBSCRASH?\r\n",
		"AT#XNRFCLOUDOBSEXPORT?\r\n",
	};

	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		clear_captured_response();
		send_at_command(cmds[i]);
		k_sleep(K_MSEC(10));

		TEST_ASSERT_NOT_NULL_MESSAGE(strstr(get_captured_response(), "ERROR"), cmds[i]);
	}
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
