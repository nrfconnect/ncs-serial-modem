/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file test_at_sms.c
 * Unit tests for sm_at_sms.c
 *
 * Regression test for the concatenated-SMS reassembly heap overflow (C-2):
 * a maliciously crafted concatenated SMS (attacker-controlled, arrives over
 * the air with no authentication) with maximal-length payload segments used
 * to overflow the heap buffer allocated in sms_concat_handle() due to an
 * off-by-one in the size calculation and unsafe strcat()/strncat() use
 * during the final compaction pass. CONFIG_ASAN=y (see prj.conf) turns any
 * such overflow into a hard test failure instead of silent corruption.
 */

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/sms.h>

LOG_MODULE_REGISTER(test_at_sms, LOG_LEVEL_DBG);

#include "sm_at_host.h"
#include "uart_stub.h"

/* CMock-generated mocks */
#include "cmock_nrf_modem_at.h"
#include "cmock_modem_jwt.h"

/* Custom AT command list symbols - needed by AT command dispatch */
extern int _nrf_modem_at_cmd_custom_list_start;
extern int _nrf_modem_at_cmd_custom_list_end;

int _nrf_modem_at_cmd_custom_list_start;
int _nrf_modem_at_cmd_custom_list_end;

/* Response capture - provided by sm_at_host_stubs.c */
extern void capture_response_data(const uint8_t *data, size_t len);
extern void clear_captured_response(void);
extern const char *get_captured_response(void);

/* Test helper provided by sms_stubs.c: directly invokes the listener
 * registered via sms_register_listener(), simulating message delivery
 * from the modem.
 */
extern void sms_stub_deliver(struct sms_data *data);

/* sms_concat_handle/sms_callback are declared STATIC in sm_at_sms.c, which
 * expands to plain "static" under production builds but to nothing under
 * CONFIG_UNITY, making them directly callable from this test binary.
 */
void sms_callback(struct sms_data *const data, void *context);

void setUp(void)
{
	clear_captured_response();
	/* Start the SMS listener so sms_ctx.pipe is set and sms_callback()
	 * is reachable via sms_stub_deliver().
	 */
	send_at_command("AT#XSMS=1\r\n");
	clear_captured_response();
}

void tearDown(void)
{
	send_at_command("AT#XSMS=0\r\n");
}

/* Build a single concatenated-SMS-part sms_data with a maximal-length
 * (SMS_MAX_PAYLOAD_LEN_CHARS) payload, at 1-based sequence number seq of
 * total_msgs total parts, ref_number ref.
 */
static void fill_concat_part(struct sms_data *data, uint16_t ref, uint8_t total_msgs,
			      uint8_t seq, char fill_char)
{
	memset(data, 0, sizeof(*data));
	data->type = SMS_TYPE_DELIVER;

	struct sms_deliver_header *header = &data->header.deliver;

	header->time.year = 25;
	header->time.month = 1;
	header->time.day = 1;
	header->time.hour = 12;
	header->time.minute = 0;
	header->time.second = 0;
	header->time.timezone = 4; /* UTC+01:00 */

	strcpy(header->originating_address.address_str, "1234567890");

	header->concatenated.present = true;
	header->concatenated.ref_number = ref;
	header->concatenated.total_msgs = total_msgs;
	header->concatenated.seq_number = seq;

	memset(data->payload, fill_char, SMS_MAX_PAYLOAD_LEN_CHARS);
	data->payload[SMS_MAX_PAYLOAD_LEN_CHARS] = '\0';
	data->payload_len = SMS_MAX_PAYLOAD_LEN_CHARS;
}

/*
 * Test: a maximal-size concatenated SMS (10 parts - the maximum supported,
 * MAX_CONCATENATED_MESSAGE - each with the maximal SMS_MAX_PAYLOAD_LEN_CHARS
 * payload) is reassembled without a heap buffer overflow (would previously
 * abort under ASAN) and the resulting URC contains all message parts,
 * correctly concatenated and NUL-terminated.
 */
void test_xsms_concat_max_size_no_overflow(void)
{
	struct sms_data data;
	const uint8_t total_msgs = 10;
	const uint16_t ref_number = 42;

	for (uint8_t seq = 1; seq <= total_msgs; seq++) {
		/* Use a distinct fill character per part so we can verify
		 * every part landed in the final, reassembled string.
		 */
		fill_concat_part(&data, ref_number, total_msgs, seq, (char)('A' + seq - 1));
		sms_stub_deliver(&data);
	}

	/* The final part triggers urc_send_to(), which is processed
	 * asynchronously via sm_work_q and captured via the wrapped
	 * sm_at_send(); give the work queue a moment to run.
	 */
	k_sleep(K_MSEC(10));

	const char *response = get_captured_response();

	TEST_ASSERT_TRUE(strstr(response, "#XSMS:") != NULL);
	TEST_ASSERT_TRUE(strstr(response, "1234567890") != NULL);

	/* Every segment's fill character should appear
	 * SMS_MAX_PAYLOAD_LEN_CHARS times, contiguously, in order.
	 */
	const char *p = response;

	for (uint8_t seq = 1; seq <= total_msgs; seq++) {
		char expected[SMS_MAX_PAYLOAD_LEN_CHARS + 1];

		memset(expected, (char)('A' + seq - 1), SMS_MAX_PAYLOAD_LEN_CHARS);
		expected[SMS_MAX_PAYLOAD_LEN_CHARS] = '\0';

		p = strstr(p, expected);
		TEST_ASSERT_NOT_NULL_MESSAGE(p, "Missing or out-of-order message segment");
		p += SMS_MAX_PAYLOAD_LEN_CHARS;
	}

	/* Response must be properly terminated, not truncated/garbled. */
	TEST_ASSERT_TRUE(strstr(response, "\"\r\n") != NULL);
}

/*
 * Test: an out-of-order arrival of parts (last part first) is still
 * reassembled correctly into the right order, and does not overflow.
 */
void test_xsms_concat_out_of_order_no_overflow(void)
{
	struct sms_data data;
	const uint8_t total_msgs = 3;
	const uint16_t ref_number = 7;

	fill_concat_part(&data, ref_number, total_msgs, 3, 'C');
	sms_stub_deliver(&data);
	fill_concat_part(&data, ref_number, total_msgs, 1, 'A');
	sms_stub_deliver(&data);
	fill_concat_part(&data, ref_number, total_msgs, 2, 'B');
	sms_stub_deliver(&data);

	k_sleep(K_MSEC(10));

	const char *response = get_captured_response();
	const char *pos_a = strstr(response, "AAA");
	const char *pos_b = strstr(response, "BBB");
	const char *pos_c = strstr(response, "CCC");

	TEST_ASSERT_NOT_NULL(pos_a);
	TEST_ASSERT_NOT_NULL(pos_b);
	TEST_ASSERT_NOT_NULL(pos_c);
	TEST_ASSERT_TRUE(pos_a < pos_b);
	TEST_ASSERT_TRUE(pos_b < pos_c);
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
