/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/at_parser.h>
#include <net/fota_download.h>
#include <nrf_cloud_download.h>
#include <memfault/ports/zephyr/fota.h>
#include <memfault/http/http_client.h>
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_fota.h"
#include "sm_settings.h"

LOG_MODULE_REGISTER(sm_nrfcloud_fota, CONFIG_SM_LOG_LEVEL);

/* AT#XNRFCLOUDFOTA shares the FOTA session state (sm_fota_type, sm_fota_stage, ...) and the
 * #XFOTA progress/activation URC with AT#XFOTA: an application update is staged the same way as
 * AT#XFOTA=1 and activated by the host with AT#XRESET, and a modem update is staged the same way
 * as AT#XFOTA=2 and activated with AT#XMODEMRESET. Only the source of the download URL differs:
 * Memfault release management instead of a host-supplied URL.
 */

#define FOTA_KEY_MAX_LEN	32
#define FOTA_PARAM_OP		1
#define FOTA_PARAM_KEY		2

/* <op> values for AT#XNRFCLOUDFOTA. */
#define FOTA_STOP	0
#define FOTA_APP	1
#define FOTA_MODEM	2

/* Pipe that a #XFOTA or #XNRFCLOUDFOTA URC is sent to: the pipe of the command that started
 * the ongoing check/download.
 */
static struct modem_pipe *fota_pipe;

/* Optional <project_key> override for the ongoing AT#XNRFCLOUDFOTA check. Empty means the
 * build-time key is used: CONFIG_MEMFAULT_PROJECT_KEY for an application check (op 1) and
 * CONFIG_MEMFAULT_FOTA_MODEM_PROJECT_KEY for a modem check (op 2).
 */
static char fota_project_key[FOTA_KEY_MAX_LEN + 1];

/*************************************************/
/* Memfault FOTA check                           */
/*************************************************/

/* Query Memfault release management and, if an update is available, start the download.
 * sm_fota_type selects app or modem; sm_fota_stage is already FOTA_STAGE_DOWNLOAD and
 * fota_pipe already set by the caller.
 *
 * Note: for sm_fota_type == SM_FOTA_TYPE_APP, memfault_zephyr_fota_start() itself falls back to
 * checking the modem project when no application update is pending. In that case a modem
 * update may start while sm_fota_type still reads SM_FOTA_TYPE_APP; the modem firmware is
 * still updated correctly, but the AT#XMODEMRESET completion report is skipped since it only
 * fires for SM_FOTA_TYPE_MFW. This is only reachable when no application update is pending, and
 * only when a modem project key is configured via CONFIG_MEMFAULT_FOTA_MODEM_PROJECT_KEY or
 * memfault_zephyr_fota_modem_project_key_set() (the <project_key> override of this op=1 check,
 * fota_project_key, is not used for this fallback).
 */
static void nrfcloud_fota_check(void)
{
	int rv;

	if (sm_fota_type == SM_FOTA_TYPE_APP) {
		/* memfault_zephyr_fota_start() uses g_mflt_http_client_config.api_key as the
		 * project key; swap in the override for the check, like the modem path does.
		 */
		const char *saved_key = g_mflt_http_client_config.api_key;

		if (fota_project_key[0] != '\0') {
			g_mflt_http_client_config.api_key = fota_project_key;
		}
		rv = memfault_zephyr_fota_start();
		g_mflt_http_client_config.api_key = saved_key;
	} else {
		memfault_zephyr_fota_modem_project_key_set(
			fota_project_key[0] != '\0' ? fota_project_key : NULL);
		rv = memfault_zephyr_fota_modem_start();
		memfault_zephyr_fota_modem_project_key_set(NULL);
	}

	if (rv < 0) {
		LOG_ERR("FOTA check failed: %d", rv);
		sm_fota_init_state();
		urc_send_to(fota_pipe, "\r\n#XNRFCLOUDFOTA: -1,%d\r\n", rv);
	} else if (rv == 0) {
		sm_fota_init_state();
		urc_send_to(fota_pipe, "\r\n#XNRFCLOUDFOTA: 0\r\n");
	}
	/* rv == 1: a download started. memfault_fota_download_callback() reports the rest
	 * through the shared #XFOTA URC.
	 */
}

static void nrfcloud_fota_check_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	nrfcloud_fota_check();
}
K_WORK_DEFINE(nrfcloud_fota_check_work, nrfcloud_fota_check_work_fn);

/* Releases the nRF Cloud download layer at the end of an AT#XNRFCLOUDFOTA session and restores
 * AT#XFOTA's fota_download callback, which nrf_cloud_download_start() overwrote for the
 * session. Without this, a subsequent AT#XNRFCLOUDFOTA would get -EBUSY, and a subsequent
 * AT#XFOTA download would misroute its events to this file's callback instead of
 * sm_at_fota.c's fota_dl_handler().
 */
static void nrfcloud_fota_session_end(void)
{
	int err;

	nrf_cloud_download_end();

	err = sm_at_fota_register_callback();
	if (err) {
		LOG_ERR("Failed to restore AT#XFOTA's fota_download callback: %d", err);
	}
}

/* Custom implementation of the Memfault NCS FOTA backend's download callback (selected via
 * CONFIG_MEMFAULT_FOTA_DOWNLOAD_CALLBACK_CUSTOM), so that progress and completion are reported
 * over the #XFOTA URC instead of the SDK's default of rebooting immediately. This mirrors
 * sm_at_fota.c's fota_dl_handler(); the two are never active at the same time because they
 * share the sm_fota_stage busy gate.
 *
 * memfault_fota.c forward-declares this itself; there is no public header to include it from.
 */
void memfault_fota_download_callback(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		/* Re-establish the stage/status: an earlier ERASE_DONE resets the stage to
		 * FOTA_STAGE_INIT, and the download continues afterwards.
		 */
		sm_fota_stage = FOTA_STAGE_DOWNLOAD;
		sm_fota_status = FOTA_STATUS_OK;
		sm_fota_info = evt->progress;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d,%d\r\n", sm_fota_stage, sm_fota_status,
			   sm_fota_info);
		break;
	case FOTA_DOWNLOAD_EVT_FINISHED:
		sm_fota_stage = FOTA_STAGE_ACTIVATE;
		sm_fota_info = 0;
		/* Save, in case activation happens by reset. */
		sm_settings_fota_save();
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		/* The app is staged but not yet rebooted; end the session now instead of
		 * waiting for AT#XRESET.
		 */
		nrfcloud_fota_session_end();
		break;
	case FOTA_DOWNLOAD_EVT_ERASE_TIMEOUT:
		LOG_INF("Erasure timeout reached. Erasure continues.");
		break;
	case FOTA_DOWNLOAD_EVT_ERASE_PENDING:
		sm_fota_stage = FOTA_STAGE_DOWNLOAD_ERASE_PENDING;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		break;
	case FOTA_DOWNLOAD_EVT_ERASE_DONE:
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", FOTA_STAGE_DOWNLOAD_ERASED,
			   sm_fota_status);
		/* Back to init now that the erasure is complete so that potential pre-start
		 * error codes are printed with the same stage than if there had been no
		 * erasure.
		 */
		sm_fota_stage = FOTA_STAGE_INIT;
		break;
	case FOTA_DOWNLOAD_EVT_ERROR:
		sm_fota_status = FOTA_STATUS_ERROR;
		sm_fota_info = evt->cause;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d,%d\r\n", sm_fota_stage, sm_fota_status,
			   sm_fota_info);
		nrfcloud_fota_session_end();
		sm_fota_init_state();
		break;
	case FOTA_DOWNLOAD_EVT_CANCELLED:
		sm_fota_status = FOTA_STATUS_CANCELLED;
		sm_fota_info = 0;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		nrfcloud_fota_session_end();
		sm_fota_init_state();
		break;
	default:
		break;
	}
}

/*************************************************/

/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudfota, "AT#XNRFCLOUDFOTA", handle_at_nrf_cloud_fota);
STATIC int handle_at_nrf_cloud_fota(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				    uint32_t param_count)
{
	uint16_t op;
	char key[FOTA_KEY_MAX_LEN + 1];
	size_t key_len;
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, FOTA_PARAM_OP, &op);
		if (err) {
			return err;
		}

		if (op == FOTA_STOP) {
			return fota_download_cancel();
		}

		/* One FOTA session at a time, shared with AT#XFOTA. */
		if (sm_fota_stage != FOTA_STAGE_INIT) {
			return -EBUSY;
		}

		if (op == FOTA_APP) {
			sm_fota_type = SM_FOTA_TYPE_APP;
		} else if (op == FOTA_MODEM) {
			sm_fota_type = SM_FOTA_TYPE_MFW;
		} else {
			return -EINVAL;
		}

		/* Optional <project_key> override, applied to the app or modem check. */
		key[0] = '\0';
		if (param_count > FOTA_PARAM_KEY) {
			key_len = sizeof(key);
			err = util_string_get(parser, FOTA_PARAM_KEY, key, &key_len);
			/* The parameter was left out, as in "1," or "2,". */
			if (err && err != -ENODATA) {
				return err;
			}
		}
		strcpy(fota_project_key, key);

		sm_fota_stage = FOTA_STAGE_DOWNLOAD;
		fota_pipe = sm_at_host_get_current_pipe();

		/* Run on the blocking work queue */
		sm_k_work_submit_blocking(&nrfcloud_fota_check_work);

		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDFOTA: (%d,%d,%d)[,<project_key>]\r\n",
			FOTA_STOP, FOTA_APP, FOTA_MODEM);
		return 0;

	default:
		return -ENOTSUP;
	}
}
