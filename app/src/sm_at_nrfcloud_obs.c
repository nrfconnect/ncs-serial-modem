/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/base64.h>
#include <net/nrf_cloud_coap.h>
#include "nrf_cloud_coap_transport.h"
#include <modem/at_parser.h>
#include <memfault/core/log.h>
#include <memfault/core/platform/device_info.h>
#include <memfault/http/http_client.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
/* MEMFAULT_BASE64_MAX_DECODE_LEN(), used to size the buffer of a forwarded chunk. */
#include <memfault/util/base64.h>
#if defined(CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG)
#include <memfault/core/data_export.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/demo/cli.h>
#endif /* CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG */
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_nrfcloud.h"

LOG_MODULE_REGISTER(sm_nrfcloud_obs, CONFIG_SM_LOG_LEVEL);

/* AT#XNRFCLOUDOBSAUTO implements the automatic upload itself, because the Memfault
 * periodic upload port only offers a build-time interval and no way to change it at
 * runtime. That port is left out of the build, see Kconfig.memfault.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_MEMFAULT_PERIODIC_UPLOAD),
	     "The Memfault periodic upload would duplicate AT#XNRFCLOUDOBSAUTO");

/* Upload is host-driven for the same reason: the buffered data, including a core dump, is
 * sent when the host issues AT#XNRFCLOUDOBSUPLOAD, or on the next automatic upload once
 * the host enables it with AT#XNRFCLOUDOBSAUTO=1. The nRF Connect SDK post-core-dump state
 * machine would instead upload a stored core dump on its own, as soon as the network is
 * connected. It applies to both core dump backends, the RAM-backed one and the
 * flash-backed one, and it is unreachable in this application, whose dependencies on
 * LTE_LINK_CONTROL and LTE_LC_PDN_MODULE are not met; this catches it becoming reachable.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_MEMFAULT_NCS_POST_COREDUMP_ON_NETWORK_CONNECTED),
	     "Posting the core dump on network connected would bypass AT#XNRFCLOUDOBSUPLOAD");

/* Memfault project keys are 32 characters; 64 leaves ample margin. A key longer than
 * CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE is dropped by the CoAP port at send time, which
 * falls back to the server-side routing.
 */
#define OBS_PROJECT_KEY_MAX_LEN 64

/* Bounds of the automatic upload interval. Below a minute the uploads cost more than the
 * data they carry, and a day is the coarsest interval that still reports daily.
 */
#define OBS_AUTO_INTERVAL_MIN 60
#define OBS_AUTO_INTERVAL_MAX 86400

/* Parameter indices. Index 0 is the command itself. */
#define OBS_AUTO_PARAM_ENABLE		1
#define OBS_AUTO_PARAM_INTERVAL		2
#define OBS_AUTO_PARAM_PROJECT_KEY	3
#define OBS_UPLOAD_PARAM_PROJECT_KEY	1
#define OBS_FORWARD_PARAM_CHUNK		1
#define OBS_FORWARD_PARAM_PROJECT_KEY	2
#define OBS_CRASH_PARAM_TYPE		1

/* Configuration of the automatic upload, persisted under the "sm/obs" settings subtree. */
static bool obs_auto_enabled;
static uint32_t obs_auto_interval = CONFIG_SM_NRF_CLOUD_OBSERVABILITY_AUTO_INTERVAL_SECONDS;
static char obs_auto_key[OBS_PROJECT_KEY_MAX_LEN + 1];

/* Whether an operation that accesses the network is ongoing. */
static bool obs_busy;
static struct modem_pipe *obs_pipe;

/* Parameters saved before submitting the asynchronous work. An empty project key means
 * that the server-side routing is used.
 */
static char obs_project_key[OBS_PROJECT_KEY_MAX_LEN + 1];
static uint8_t *obs_chunk;
static size_t obs_chunk_len;

/* Overridable so that the unit tests can exercise the timeout path. */
#ifndef OBS_COAP_RESPONSE_TIMEOUT_MS
#define OBS_COAP_RESPONSE_TIMEOUT_MS 30000
#endif

/* Accessed from the work queue, serialised by obs_busy, except for outstanding, which
 * the CoAP client thread clears once the final response of a request has arrived.
 */
static struct obs_coap_context {
	struct k_sem response_sem;
	int result_code;
	/* Set while a request has no final response yet, even after the caller gave up
	 * waiting for it. The context cannot be re-armed until it is cleared, otherwise a
	 * late response would signal the request that re-armed it.
	 */
	atomic_t outstanding;
} obs_coap_ctx;

/*************************************************/
/* Project key and upload helpers                */
/*************************************************/

/* Override the server-side project-key routing by injecting the key as CoAP option 2429.
 * An empty or absent key leaves the routing alone. Returns the key that was installed
 * before, which the caller restores once the operation is done.
 */
static const char *obs_project_key_install(const char *key)
{
	const char *previous = g_mflt_http_client_config.api_key;

	if (key != NULL && key[0] != '\0') {
		g_mflt_http_client_config.api_key = key;
	}

	return previous;
}

/* Drain the buffered Memfault data into nRF Cloud. Returns the number of bytes uploaded,
 * or a negative error code. The Memfault CoAP port returns 0 without opening a socket
 * when there is nothing buffered.
 */
static ssize_t obs_upload(void)
{
	/* Freeze the captured logs so that they are drained as well. */
	memfault_log_trigger_collection();

	return memfault_zephyr_port_post_data_return_size();
}

/*************************************************/
/* Persistent configuration of the auto upload   */
/*************************************************/

static void obs_auto_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(obs_auto_work, obs_auto_work_fn);

/* Arm or disarm the automatic upload according to the current configuration. */
static void obs_auto_rearm(void)
{
	if (obs_auto_enabled) {
		k_work_reschedule_for_queue(&sm_work_q, &obs_auto_work,
					    K_SECONDS(obs_auto_interval));
	} else {
		k_work_cancel_delayable(&obs_auto_work);
	}
}

static int obs_settings_save(void)
{
	int err;

	err = settings_save_one("sm/obs/auto", &obs_auto_enabled, sizeof(obs_auto_enabled));
	if (err) {
		return err;
	}

	err = settings_save_one("sm/obs/interval", &obs_auto_interval, sizeof(obs_auto_interval));
	if (err) {
		return err;
	}

	/* A zero length deletes the entry, which is what an empty key means: no project key
	 * is stored and the server-side routing is used.
	 */
	return settings_save_one("sm/obs/key", obs_auto_key, strlen(obs_auto_key));
}

/* STATIC so that the unit tests can replay a stored configuration without a backend. */
STATIC int obs_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ssize_t ret;

	if (!strcmp(name, "auto")) {
		if (len != sizeof(obs_auto_enabled)) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &obs_auto_enabled, len) > 0) {
			return 0;
		}
	} else if (!strcmp(name, "interval")) {
		if (len != sizeof(obs_auto_interval)) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &obs_auto_interval, len) > 0) {
			return 0;
		}
	} else if (!strcmp(name, "key")) {
		if (len >= sizeof(obs_auto_key)) {
			return -EINVAL;
		}
		ret = read_cb(cb_arg, obs_auto_key, len);
		if (ret >= 0) {
			obs_auto_key[ret] = '\0';
			return 0;
		}
	}

	/* Ignore anything else, so that an obsolete setting does not fail the load. */
	return 0;
}

/* Arm the automatic upload once the stored configuration has been loaded. Runs before
 * main() starts the Serial Modem work queue, which is safe because only the expiry of the
 * timeout submits the work, and the shortest interval is a minute.
 */
STATIC int obs_settings_commit(void)
{
	obs_auto_rearm();

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(sm_obs, "sm/obs", NULL, obs_settings_set, obs_settings_commit,
			       NULL);

/*************************************************/
/* Work handlers                                 */
/*************************************************/

static void obs_auto_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	const char *previous_key;
	ssize_t result;

	if (!obs_auto_enabled) {
		return;
	}

	/* Rearm first, so that the interval does not drift with the duration of the upload
	 * and a failed upload does not stop the automatic upload.
	 */
	k_work_reschedule_for_queue(&sm_work_q, &obs_auto_work, K_SECONDS(obs_auto_interval));

	if (!sm_nrf_cloud_ready) {
		LOG_DBG("Not connected to nRF Cloud, skipping the automatic upload.");
		return;
	}

	previous_key = obs_project_key_install(obs_auto_key);
	result = obs_upload();
	g_mflt_http_client_config.api_key = previous_key;

	if (result < 0) {
		LOG_WRN("Automatic upload failed: %zd", result);
	} else {
		LOG_DBG("Automatic upload sent %zd bytes", result);
	}
}

static void obs_upload_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	const char *previous_key;
	ssize_t result;

	previous_key = obs_project_key_install(obs_project_key);
	result = obs_upload();
	g_mflt_http_client_config.api_key = previous_key;

	if (result < 0) {
		LOG_ERR("Upload failed: %zd", result);
		urc_send_to(obs_pipe, "\r\n#XNRFCLOUDOBSUPLOAD: -1\r\n");
	} else {
		urc_send_to(obs_pipe, "\r\n#XNRFCLOUDOBSUPLOAD: 0,%zd\r\n", result);
	}

	obs_busy = false;
}
K_WORK_DEFINE(obs_upload_work, obs_upload_work_fn);

static void obs_coap_response_cb(const struct coap_client_response_data *data, void *user_data)
{
	struct obs_coap_context *ctx = user_data;

	if (!data->last_block && data->result_code >= 0) {
		/* Not the final response of this request yet. */
		return;
	}

	/* When the caller timed out it is no longer waiting, so only release the context.
	 * k_sem_init() in obs_forward_chunk() covers the case where the timeout wins the
	 * race with this callback.
	 */
	if (ctx->result_code != -ETIMEDOUT) {
		ctx->result_code = data->result_code;
		k_sem_give(&ctx->response_sem);
	}

	atomic_clear(&ctx->outstanding);
}

/* Forward a chunk produced by the host MCU to the nRF Cloud "chunks" resource. The
 * Memfault CoAP port only drains the local packetizer, so the chunk is posted directly
 * here.
 *
 * Note that nRF Cloud attributes the chunk to the authenticated device, so the data of the
 * host is reported under the device serial of this device. A host that wants its own device
 * serial has to be built with MEMFAULT_EVENT_INCLUDE_DEVICE_SERIAL set to 1, which makes
 * every chunk it produces carry that serial for Memfault to attribute the data to.
 */
static int obs_forward_chunk(void)
{
	int err;

	if (!atomic_cas(&obs_coap_ctx.outstanding, 0, 1)) {
		/* A previous post timed out and its response has still not arrived. The
		 * CoAP client gives up after COAP_MAX_RETRANSMIT retransmissions.
		 */
		LOG_ERR("A previous chunk post is still outstanding");
		return -EBUSY;
	}

	k_sem_init(&obs_coap_ctx.response_sem, 0, 1);
	obs_coap_ctx.result_code = -1;

	err = nrf_cloud_coap_post("chunks", NULL, obs_chunk, obs_chunk_len,
				  COAP_CONTENT_FORMAT_APP_OCTET_STREAM, true,
				  obs_coap_response_cb, &obs_coap_ctx);
	if (err) {
		LOG_ERR("Failed to post chunk: %d", err);
		/* The request was never made, so no response callback is coming. */
		atomic_clear(&obs_coap_ctx.outstanding);
		return err;
	}

	if (k_sem_take(&obs_coap_ctx.response_sem, K_MSEC(OBS_COAP_RESPONSE_TIMEOUT_MS)) != 0) {
		LOG_ERR("Timeout waiting for the chunk response");
		/* Tell the callback to leave the context alone. It clears outstanding. */
		obs_coap_ctx.result_code = -ETIMEDOUT;
		return -ETIMEDOUT;
	}

	if (obs_coap_ctx.result_code != COAP_RESPONSE_CODE_CREATED) {
		LOG_ERR("Unexpected CoAP response code: %d", obs_coap_ctx.result_code);
		return -EIO;
	}

	return 0;
}

static void obs_forward_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	const char *previous_key;
	int err;

	previous_key = obs_project_key_install(obs_project_key);
	err = obs_forward_chunk();
	g_mflt_http_client_config.api_key = previous_key;

	free(obs_chunk);
	obs_chunk = NULL;
	obs_chunk_len = 0;

	if (err) {
		urc_send_to(obs_pipe, "\r\n#XNRFCLOUDOBSFORWARD: -1\r\n");
	} else {
		urc_send_to(obs_pipe, "\r\n#XNRFCLOUDOBSFORWARD: 0\r\n");
	}

	obs_busy = false;
}
K_WORK_DEFINE(obs_forward_work, obs_forward_work_fn);

/*************************************************/
/* Parameter parsing                             */
/*************************************************/

/* Parse an optional <project_key> at @p index into @p key, which must hold
 * OBS_PROJECT_KEY_MAX_LEN + 1 bytes. It is emptied when the parameter is absent, empty or
 * omitted, all of which mean that the server-side routing is used.
 */
static int obs_parse_project_key(struct at_parser *parser, uint32_t param_count, size_t index,
				 char *key)
{
	size_t len = OBS_PROJECT_KEY_MAX_LEN + 1;
	int err;

	key[0] = '\0';

	if (param_count <= index) {
		return 0;
	}

	err = util_string_get(parser, index, key, &len);
	if (err == -ENODATA) {
		/* The parameter was left out, as in AT#XNRFCLOUDOBSAUTO=1,,"<key>". */
		return 0;
	}

	return err;
}

/* Decode the <base64_chunk> of AT#XNRFCLOUDOBSFORWARD into a heap buffer. */
static int obs_parse_chunk(struct at_parser *parser)
{
	size_t b64_len = 0;
	size_t bin_len;
	const char *b64;
	int err;

	/* Point into the AT command buffer to avoid copying a potentially large chunk. */
	err = at_parser_string_ptr_get(parser, OBS_FORWARD_PARAM_CHUNK, &b64, &b64_len);
	if (err) {
		return err;
	}
	if (b64_len == 0) {
		return -EINVAL;
	}

	obs_chunk = malloc(MEMFAULT_BASE64_MAX_DECODE_LEN(b64_len));
	if (obs_chunk == NULL) {
		return -ENOMEM;
	}

	err = base64_decode(obs_chunk, MEMFAULT_BASE64_MAX_DECODE_LEN(b64_len), &bin_len,
			    (const uint8_t *)b64, b64_len);
	if (err || bin_len == 0) {
		LOG_ERR("Failed to decode the chunk: %d", err);
		free(obs_chunk);
		obs_chunk = NULL;
		return -EBADMSG;
	}

	obs_chunk_len = bin_len;

	return 0;
}

/* Whether an operation that accesses the network can be started.
 *
 * This has to be checked before any of the saved parameters is written, because the
 * parameters are shared: the work queue posts whatever obs_project_key, obs_chunk and
 * obs_chunk_len hold when it runs. A command that parses into them before finding out that
 * it cannot run would overwrite the parameters of the operation that is already pending,
 * and leak the chunk buffer that it replaced.
 */
static int obs_check_ready(void)
{
	if (!sm_nrf_cloud_ready) {
		LOG_ERR("Not connected to nRF Cloud.");
		return -ENOTCONN;
	}
	if (obs_busy) {
		LOG_ERR("Observability operation already ongoing.");
		return -EBUSY;
	}

	return 0;
}

/* Submit an operation that accesses the network to the work queue so that the OK response
 * is sent before it runs. This hands the saved parameters to the work queue, which owns
 * them, and frees obs_chunk, until it clears obs_busy.
 */
static void obs_submit(struct k_work *work)
{
	obs_pipe = sm_at_host_get_current_pipe();
	obs_busy = true;
	k_work_submit_to_queue(&sm_work_q, work);
}

/*************************************************/
/* AT#XNRFCLOUDOBSAUTO                           */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudobsauto, "AT#XNRFCLOUDOBSAUTO", handle_at_nrf_cloud_obs_auto);
STATIC int handle_at_nrf_cloud_obs_auto(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
					uint32_t param_count)
{
	char key[OBS_PROJECT_KEY_MAX_LEN + 1];
	uint32_t interval = obs_auto_interval;
	uint16_t enable;
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, OBS_AUTO_PARAM_ENABLE, &enable);
		if (err) {
			return err;
		}
		if (enable > 1) {
			return -EINVAL;
		}

		if (param_count > OBS_AUTO_PARAM_INTERVAL) {
			err = at_parser_num_get(parser, OBS_AUTO_PARAM_INTERVAL, &interval);
			/* An omitted interval keeps the stored one. */
			if (err && err != -ENODATA) {
				return err;
			}
			if (!err && (interval < OBS_AUTO_INTERVAL_MIN ||
				     interval > OBS_AUTO_INTERVAL_MAX)) {
				return -EINVAL;
			}
		}

		err = obs_parse_project_key(parser, param_count, OBS_AUTO_PARAM_PROJECT_KEY, key);
		if (err) {
			return err;
		}

		obs_auto_enabled = enable;
		obs_auto_interval = interval;
		if (param_count > OBS_AUTO_PARAM_PROJECT_KEY) {
			/* An empty key clears the stored one, back to server-side routing. */
			strcpy(obs_auto_key, key);
		}

		obs_auto_rearm();

		err = obs_settings_save();
		if (err) {
			/* Applied either way; a partial save may store new and old values mixed. */
			LOG_WRN("Failed to store the automatic upload configuration: %d", err);
		}

		return 0;

	case AT_PARSER_CMD_TYPE_READ:
		rsp_send("\r\n#XNRFCLOUDOBSAUTO: %d,%u,\"%s\"\r\n", obs_auto_enabled,
			 obs_auto_interval, obs_auto_key);
		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDOBSAUTO: (0,1),(%d-%d),<project_key>\r\n",
			 OBS_AUTO_INTERVAL_MIN, OBS_AUTO_INTERVAL_MAX);
		return 0;

	default:
		return -ENOTSUP;
	}
}

/*************************************************/
/* AT#XNRFCLOUDOBSUPLOAD                         */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudobsupload, "AT#XNRFCLOUDOBSUPLOAD", handle_at_nrf_cloud_obs_upload);
STATIC int handle_at_nrf_cloud_obs_upload(enum at_parser_cmd_type cmd_type,
					  struct at_parser *parser, uint32_t param_count)
{
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = obs_check_ready();
		if (err) {
			return err;
		}

		err = obs_parse_project_key(parser, param_count, OBS_UPLOAD_PARAM_PROJECT_KEY,
					    obs_project_key);
		if (err) {
			return err;
		}

		obs_submit(&obs_upload_work);

		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDOBSUPLOAD: <project_key>\r\n");
		return 0;

	default:
		return -ENOTSUP;
	}
}

/*************************************************/
/* AT#XNRFCLOUDOBSHEARTBEAT                      */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudobsheartbeat, "AT#XNRFCLOUDOBSHEARTBEAT",
		 handle_at_nrf_cloud_obs_heartbeat);
STATIC int handle_at_nrf_cloud_obs_heartbeat(enum at_parser_cmd_type cmd_type,
					     struct at_parser *parser, uint32_t param_count)
{
	ARG_UNUSED(parser);
	ARG_UNUSED(param_count);

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		memfault_metrics_heartbeat_debug_trigger();
		return 0;

	default:
		return -ENOTSUP;
	}
}

/*************************************************/
/* AT#XNRFCLOUDOBSFORWARD                        */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudobsforward, "AT#XNRFCLOUDOBSFORWARD", handle_at_nrf_cloud_obs_forward);
STATIC int handle_at_nrf_cloud_obs_forward(enum at_parser_cmd_type cmd_type,
					   struct at_parser *parser, uint32_t param_count)
{
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count <= OBS_FORWARD_PARAM_CHUNK) {
			return -EINVAL;
		}

		err = obs_check_ready();
		if (err) {
			return err;
		}

		err = obs_parse_project_key(parser, param_count, OBS_FORWARD_PARAM_PROJECT_KEY,
					    obs_project_key);
		if (err) {
			return err;
		}

		/* Allocates obs_chunk, and frees it again if it fails. Nothing is pending, so
		 * the parameters are this command's to write.
		 */
		err = obs_parse_chunk(parser);
		if (err) {
			return err;
		}

		obs_submit(&obs_forward_work);

		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDOBSFORWARD: <base64_chunk>,<project_key>\r\n");
		return 0;

	default:
		return -ENOTSUP;
	}
}

#if defined(CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG)

/*************************************************/
/* AT#XNRFCLOUDOBSDEVINFO                        */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudobsdevinfo, "AT#XNRFCLOUDOBSDEVINFO", handle_at_nrf_cloud_obs_devinfo);
STATIC int handle_at_nrf_cloud_obs_devinfo(enum at_parser_cmd_type cmd_type,
					   struct at_parser *parser, uint32_t param_count)
{
	ARG_UNUSED(parser);
	ARG_UNUSED(param_count);

	sMemfaultDeviceInfo device_info;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		memfault_platform_get_device_info(&device_info);
		rsp_send("\r\n#XNRFCLOUDOBSDEVINFO: \"%s\",\"%s\",\"%s\",\"%s\"\r\n",
			 device_info.device_serial ? device_info.device_serial : "",
			 device_info.software_type ? device_info.software_type : "",
			 device_info.software_version ? device_info.software_version : "",
			 device_info.hardware_version ? device_info.hardware_version : "");
		return 0;

	default:
		return -ENOTSUP;
	}
}

/*************************************************/
/* AT#XNRFCLOUDOBSCRASH                          */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudobscrash, "AT#XNRFCLOUDOBSCRASH", handle_at_nrf_cloud_obs_crash);
STATIC int handle_at_nrf_cloud_obs_crash(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
					 uint32_t param_count)
{
	/* Crash types are documented by memfault_demo_cli_cmd_crash(). */
	char type_str[12] = "0";
	char cmd_name[] = "crash";
	char *argv[2];
	uint16_t type;
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count > OBS_CRASH_PARAM_TYPE) {
			err = at_parser_num_get(parser, OBS_CRASH_PARAM_TYPE, &type);
			if (err) {
				return err;
			}
			snprintf(type_str, sizeof(type_str), "%u", type);
		}

		argv[0] = cmd_name;
		argv[1] = type_str;

		/* Only returns if the crash type is invalid. */
		return memfault_demo_cli_cmd_crash(ARRAY_SIZE(argv), argv) ? -EINVAL : 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDOBSCRASH: <type>\r\n");
		return 0;

	default:
		return -ENOTSUP;
	}
}

/*************************************************/
/* AT#XNRFCLOUDOBSEXPORT                         */
/*************************************************/

/* Print the buffered chunks to the AT interface using the Memfault chunk export format
 * ("MC:<base64>:"), which the Memfault tooling can parse. Note that this consumes the
 * chunks, so they are no longer available for upload.
 */
static void obs_export_chunks(void)
{
	uint8_t chunk[MEMFAULT_DATA_EXPORT_CHUNK_MAX_LEN];
	char base64[MEMFAULT_BASE64_ENCODE_LEN(MEMFAULT_DATA_EXPORT_CHUNK_MAX_LEN) + 1];

	while (true) {
		size_t chunk_len = sizeof(chunk);

		if (!memfault_packetizer_get_chunk(chunk, &chunk_len)) {
			return;
		}

		memfault_base64_encode(chunk, chunk_len, base64);
		base64[MEMFAULT_BASE64_ENCODE_LEN(chunk_len)] = '\0';
		rsp_send("\r\n%s%s%s\r\n", MEMFAULT_DATA_EXPORT_BASE64_CHUNK_PREFIX, base64,
			 MEMFAULT_DATA_EXPORT_BASE64_CHUNK_SUFFIX);
	}
}

SM_AT_CMD_CUSTOM(xnrfcloudobsexport, "AT#XNRFCLOUDOBSEXPORT", handle_at_nrf_cloud_obs_export);
STATIC int handle_at_nrf_cloud_obs_export(enum at_parser_cmd_type cmd_type,
					  struct at_parser *parser, uint32_t param_count)
{
	ARG_UNUSED(parser);
	ARG_UNUSED(param_count);

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		obs_export_chunks();
		return 0;

	default:
		return -ENOTSUP;
	}
}

#endif /* CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG */
