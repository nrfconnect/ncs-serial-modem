/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file memfault_stubs.c
 *
 * Manual stubs for the Memfault symbols used by the AT#XNRFCLOUDOBS* commands in
 * sm_at_nrfcloud_obs.c.
 *
 * The stubs hold the state the tests assert on:
 *   - the order of memfault_log_trigger_collection() vs the upload,
 *   - the number of heartbeat triggers,
 *   - the crash type forwarded to the Memfault demo CLI,
 *   - the chunks handed out by the packetizer (for the dump operation),
 * and let the tests inject the upload return value.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "memfault/core/data_export.h"
#include "memfault/core/data_packetizer.h"
#include "memfault/core/log.h"
#include "memfault/core/platform/device_info.h"
#include "memfault/http/http_client.h"
#include "memfault/metrics/metrics.h"
#include "memfault/ports/zephyr/http.h"
#include "memfault/util/base64.h"

/* ---------------------------------------------------------------------------
 * Recorded state, inspected by the tests
 * ---------------------------------------------------------------------------
 */

/* Heartbeat */
int test_memfault_heartbeat_trigger_count;

/* Upload: injected return value, and what was observed while uploading */
ssize_t test_memfault_upload_return;
int test_memfault_log_trigger_count;
/* True when the log collection was triggered before the upload ran. */
bool test_memfault_log_triggered_before_upload;
const char *test_memfault_upload_seen_api_key;

/* Crash: type string forwarded to memfault_demo_cli_cmd_crash() */
char test_memfault_crash_type[12];
int test_memfault_crash_call_count;
/* Injected: return value of memfault_demo_cli_cmd_crash() (non-zero = invalid type) */
int test_memfault_crash_return;

/* Packetizer: chunks handed out by memfault_packetizer_get_chunk() */
#define TEST_MAX_CHUNKS 4
static const char *test_chunks[TEST_MAX_CHUNKS];
static size_t test_chunk_count;
static size_t test_chunk_index;

/* Device info reported by memfault_platform_get_device_info() */
static const sMemfaultDeviceInfo test_device_info = {
	.device_serial = "test-device-id",
	.software_type = "serial_modem",
	.software_version = "1.2.3",
	.hardware_version = "nrf9151dk",
};

sMfltHttpClientConfig g_mflt_http_client_config = {
	.api_key = "",
};

/* ---------------------------------------------------------------------------
 * Stub implementations
 * ---------------------------------------------------------------------------
 */

void memfault_metrics_heartbeat_debug_trigger(void)
{
	test_memfault_heartbeat_trigger_count++;
}

void memfault_log_trigger_collection(void)
{
	test_memfault_log_trigger_count++;
}

ssize_t memfault_zephyr_port_post_data_return_size(void)
{
	/* Capture what the handler installed for this upload. */
	test_memfault_upload_seen_api_key = g_mflt_http_client_config.api_key;
	test_memfault_log_triggered_before_upload = (test_memfault_log_trigger_count > 0);

	return test_memfault_upload_return;
}

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	*info = test_device_info;
}

int memfault_demo_cli_cmd_crash(int argc, char *argv[])
{
	test_memfault_crash_call_count++;
	if (argc >= 2 && argv[1] != NULL) {
		strncpy(test_memfault_crash_type, argv[1], sizeof(test_memfault_crash_type) - 1);
		test_memfault_crash_type[sizeof(test_memfault_crash_type) - 1] = '\0';
	}

	/* Unlike the real one, this stub returns instead of crashing. */
	return test_memfault_crash_return;
}

bool memfault_packetizer_get_chunk(void *buf, size_t *buf_len)
{
	if (test_chunk_index >= test_chunk_count) {
		return false;
	}

	const char *chunk = test_chunks[test_chunk_index++];
	size_t len = strlen(chunk);

	if (len > *buf_len) {
		return false;
	}

	memcpy(buf, chunk, len);
	*buf_len = len;

	return true;
}

/* Minimal base64 encoder; the real one is part of the Memfault SDK. */
void memfault_base64_encode(const void *buf, size_t buf_len, void *base64_out)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const uint8_t *in = buf;
	char *out = base64_out;

	for (size_t i = 0; i < buf_len; i += 3) {
		uint32_t triple = (uint32_t)in[i] << 16;
		size_t remaining = buf_len - i;

		if (remaining > 1) {
			triple |= (uint32_t)in[i + 1] << 8;
		}
		if (remaining > 2) {
			triple |= in[i + 2];
		}

		*out++ = tbl[(triple >> 18) & 0x3f];
		*out++ = tbl[(triple >> 12) & 0x3f];
		*out++ = (remaining > 1) ? tbl[(triple >> 6) & 0x3f] : '=';
		*out++ = (remaining > 2) ? tbl[triple & 0x3f] : '=';
	}
}

/* ---------------------------------------------------------------------------
 * Test helpers
 * ---------------------------------------------------------------------------
 */

/* Queue the chunks that memfault_packetizer_get_chunk() hands out. */
void test_memfault_set_chunks(const char * const chunks[], size_t count)
{
	test_chunk_count = (count > TEST_MAX_CHUNKS) ? TEST_MAX_CHUNKS : count;
	for (size_t i = 0; i < test_chunk_count; i++) {
		test_chunks[i] = chunks[i];
	}
	test_chunk_index = 0;
}

/* Reset all recorded state; call from setUp(). */
void test_memfault_stubs_reset(void)
{
	test_memfault_heartbeat_trigger_count = 0;
	test_memfault_upload_return = 0;
	test_memfault_log_trigger_count = 0;
	test_memfault_log_triggered_before_upload = false;
	test_memfault_upload_seen_api_key = NULL;
	memset(test_memfault_crash_type, 0, sizeof(test_memfault_crash_type));
	test_memfault_crash_call_count = 0;
	test_memfault_crash_return = 0;
	test_chunk_count = 0;
	test_chunk_index = 0;
	g_mflt_http_client_config.api_key = "";
}
