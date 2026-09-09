/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <modem/at_parser.h>
#include <net/fota_download.h>
#include <memfault/ports/zephyr/fota.h>
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_fota.h"
#include "sm_at_nrfcloud.h"
#include "sm_settings.h"

LOG_MODULE_REGISTER(sm_nrfcloud_fota, CONFIG_SM_LOG_LEVEL);

/* AT#XNRFCLOUDFOTA and AT#XNRFCLOUDFOTAAUTO share the FOTA session state (sm_fota_type,
 * sm_fota_stage, ...) and the #XFOTA progress/activation URC with AT#XFOTA: an application
 * update is staged the same way as AT#XFOTA=1 and activated by the host with AT#XRESET, and a
 * modem update is staged the same way as AT#XFOTA=2 and activated with AT#XMODEMRESET. Only the
 * source of the download URL differs: Memfault release management instead of a host-supplied
 * URL.
 */

/* Memfault project keys are 32 characters; 64 leaves ample margin, matching
 * AT#XNRFCLOUDOBS*'s <project_key> parameter.
 */
#define FOTA_MODEM_KEY_MAX_LEN 64

/* Bounds of the automatic check interval, matching AT#XNRFCLOUDOBSAUTO's <interval_seconds>. */
#define FOTA_AUTO_INTERVAL_MIN 60
#define FOTA_AUTO_INTERVAL_MAX 86400

#define FOTA_PARAM_OP		1
#define FOTA_PARAM_MODEM_KEY	2

#define FOTA_AUTO_PARAM_ENABLE		1
#define FOTA_AUTO_PARAM_TARGET		2
#define FOTA_AUTO_PARAM_INTERVAL	3

enum fota_auto_target {
	FOTA_AUTO_TARGET_APP,
	FOTA_AUTO_TARGET_MODEM,
	FOTA_AUTO_TARGET_ALL,
};

/* Configuration of the automatic check, persisted under the "sm/nfota" settings subtree. */
static bool fota_auto_enabled;
static uint8_t fota_auto_target = FOTA_AUTO_TARGET_ALL;
static uint32_t fota_auto_interval = CONFIG_SM_NRF_CLOUD_FOTA_AUTO_INTERVAL_SECONDS;

/* Pipe that a #XFOTA or #XNRFCLOUDFOTA URC is sent to: the pipe of the command that started
 * the ongoing check/download, or the URC pipe for one started by the automatic check.
 */
static struct modem_pipe *fota_pipe;

/* Modem project key override for the ongoing AT#XNRFCLOUDFOTA=modem check. Empty means that
 * CONFIG_MEMFAULT_FOTA_MODEM_PROJECT_KEY is used.
 */
static char fota_modem_key[FOTA_MODEM_KEY_MAX_LEN + 1];

/*************************************************/
/* Memfault FOTA check                           */
/*************************************************/

/* Query Memfault release management and, if an update is available, start the download.
 * sm_fota_type selects app or modem; sm_fota_stage is already FOTA_STAGE_DOWNLOAD and
 * fota_pipe already set by the caller.
 *
 * Note: for sm_fota_type == SM_FOTA_TYPE_APP, memfault_zephyr_fota_start() itself falls back to
 * checking the modem project when CONFIG_MEMFAULT_FOTA_MODEM_UPDATE is set (selected here) and
 * no application update is pending. In that case a modem update may start while sm_fota_type
 * still reads SM_FOTA_TYPE_APP; the modem firmware is still updated correctly, but the
 * AT#XMODEMRESET completion report is skipped since it only fires for SM_FOTA_TYPE_MFW. This
 * matches the SDK's documented behaviour (see CONFIG_MEMFAULT_FOTA_MODEM_UPDATE's help) and is
 * only reachable when no application update is pending.
 */
static void nrfcloud_fota_check(void)
{
	int rv;

	if (sm_fota_type == SM_FOTA_TYPE_APP) {
		rv = memfault_zephyr_fota_start();
	} else {
		memfault_zephyr_fota_modem_project_key_set(
			fota_modem_key[0] != '\0' ? fota_modem_key : NULL);
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

/* Custom implementation of the Memfault NCS FOTA backend's download callback (selected via
 * CONFIG_MEMFAULT_FOTA_DOWNLOAD_CALLBACK_CUSTOM), so that progress and completion are reported
 * over the #XFOTA URC instead of the SDK's default of rebooting immediately. This mirrors
 * sm_at_fota.c's fota_dl_handler(); the two are never active at the same time because they
 * share the sm_fota_stage busy gate.
 *
 * memfault_fota.c forward-declares this itself; there is no public header to include it from.
 */
void memfault_fota_download_callback(const struct fota_download_evt *evt);
void memfault_fota_download_callback(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_PROGRESS:
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
		sm_fota_init_state();
		break;
	case FOTA_DOWNLOAD_EVT_CANCELLED:
		sm_fota_status = FOTA_STATUS_CANCELLED;
		sm_fota_info = 0;
		urc_send_to(fota_pipe, "\r\n#XFOTA: %d,%d\r\n", sm_fota_stage, sm_fota_status);
		sm_fota_init_state();
		break;
	default:
		break;
	}
}

/*************************************************/
/* Persistent configuration of the automatic check */
/*************************************************/

static void fota_auto_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(fota_auto_work, fota_auto_work_fn);

/* Arm or disarm the automatic check according to the current configuration. */
static void fota_auto_rearm(void)
{
	if (fota_auto_enabled) {
		k_work_reschedule_for_queue(&sm_work_q, &fota_auto_work,
					    K_SECONDS(fota_auto_interval));
	} else {
		k_work_cancel_delayable(&fota_auto_work);
	}
}

static int fota_auto_settings_save(void)
{
	int err;

	err = settings_save_one("sm/nfota/auto", &fota_auto_enabled, sizeof(fota_auto_enabled));
	if (err) {
		return err;
	}

	err = settings_save_one("sm/nfota/target", &fota_auto_target, sizeof(fota_auto_target));
	if (err) {
		return err;
	}

	return settings_save_one("sm/nfota/interval", &fota_auto_interval,
				 sizeof(fota_auto_interval));
}

/* STATIC so that the unit tests can replay a stored configuration without a backend. */
STATIC int fota_auto_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				  void *cb_arg)
{
	if (!strcmp(name, "auto")) {
		if (len != sizeof(fota_auto_enabled)) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &fota_auto_enabled, len) > 0) {
			return 0;
		}
	} else if (!strcmp(name, "target")) {
		if (len != sizeof(fota_auto_target)) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &fota_auto_target, len) > 0) {
			return 0;
		}
	} else if (!strcmp(name, "interval")) {
		if (len != sizeof(fota_auto_interval)) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &fota_auto_interval, len) > 0) {
			return 0;
		}
	}

	/* Ignore anything else, so that an obsolete setting does not fail the load. */
	return 0;
}

/* Arm the automatic check once the stored configuration has been loaded. Runs before main()
 * starts the Serial Modem work queue, which is safe because only the expiry of the timeout
 * submits the work, and the shortest interval is a minute.
 */
STATIC int fota_auto_settings_commit(void)
{
	fota_auto_rearm();

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(sm_nfota, "sm/nfota", NULL, fota_auto_settings_set,
			       fota_auto_settings_commit, NULL);

static void fota_auto_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!fota_auto_enabled) {
		return;
	}

	/* Rearm first, so that the interval does not drift with the duration of the check and
	 * a failed check does not stop the automatic check.
	 */
	k_work_reschedule_for_queue(&sm_work_q, &fota_auto_work, K_SECONDS(fota_auto_interval));

	if (sm_fota_stage != FOTA_STAGE_INIT) {
		LOG_DBG("A FOTA session is already in progress, skipping the automatic check.");
		return;
	}
	if (!sm_nrf_cloud_ready) {
		LOG_DBG("Not connected to nRF Cloud, skipping the automatic FOTA check.");
		return;
	}

	/* FOTA_AUTO_TARGET_ALL reuses memfault_zephyr_fota_start()'s own app-then-modem
	 * fallback, which is exactly what "all" means.
	 */
	sm_fota_type = (fota_auto_target == FOTA_AUTO_TARGET_MODEM) ? SM_FOTA_TYPE_MFW
								     : SM_FOTA_TYPE_APP;
	sm_fota_stage = FOTA_STAGE_DOWNLOAD;
	fota_modem_key[0] = '\0';
	fota_pipe = sm_at_host_get_urc_pipe();

	nrfcloud_fota_check();
}

static const char *fota_auto_target_str(uint8_t target)
{
	switch (target) {
	case FOTA_AUTO_TARGET_APP:
		return "app";
	case FOTA_AUTO_TARGET_MODEM:
		return "modem";
	default:
		return "all";
	}
}

/*************************************************/
/* AT#XNRFCLOUDFOTA                              */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudfota, "AT#XNRFCLOUDFOTA", handle_at_nrf_cloud_fota);
STATIC int handle_at_nrf_cloud_fota(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				    uint32_t param_count)
{
	char op[8];
	size_t op_len = sizeof(op);
	char key[FOTA_MODEM_KEY_MAX_LEN + 1];
	size_t key_len;
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		/* One FOTA session at a time, shared with AT#XFOTA. */
		if (sm_fota_stage != FOTA_STAGE_INIT) {
			return -EBUSY;
		}

		err = util_string_get(parser, FOTA_PARAM_OP, op, &op_len);
		if (err) {
			return err;
		}

		key[0] = '\0';
		if (!strcmp(op, "app")) {
			sm_fota_type = SM_FOTA_TYPE_APP;
		} else if (!strcmp(op, "modem")) {
			if (param_count > FOTA_PARAM_MODEM_KEY) {
				key_len = sizeof(key);
				err = util_string_get(parser, FOTA_PARAM_MODEM_KEY, key,
						      &key_len);
				/* The parameter was left out, as in "modem,". */
				if (err && err != -ENODATA) {
					return err;
				}
			}
			sm_fota_type = SM_FOTA_TYPE_MFW;
		} else {
			return -EINVAL;
		}
		strcpy(fota_modem_key, key);

		sm_fota_stage = FOTA_STAGE_DOWNLOAD;
		fota_pipe = sm_at_host_get_current_pipe();

		k_work_submit_to_queue(&sm_work_q, &nrfcloud_fota_check_work);

		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDFOTA: (\"app\",\"modem\")[,<project_key>]\r\n");
		return 0;

	default:
		return -ENOTSUP;
	}
}

/*************************************************/
/* AT#XNRFCLOUDFOTAAUTO                          */
/*************************************************/

SM_AT_CMD_CUSTOM(xnrfcloudfotaauto, "AT#XNRFCLOUDFOTAAUTO", handle_at_nrf_cloud_fota_auto);
STATIC int handle_at_nrf_cloud_fota_auto(enum at_parser_cmd_type cmd_type,
					 struct at_parser *parser, uint32_t param_count)
{
	char target_str[8];
	size_t target_len;
	uint32_t interval = fota_auto_interval;
	uint8_t target = fota_auto_target;
	uint16_t enable;
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, FOTA_AUTO_PARAM_ENABLE, &enable);
		if (err) {
			return err;
		}
		if (enable > 1) {
			return -EINVAL;
		}

		if (param_count > FOTA_AUTO_PARAM_TARGET) {
			target_len = sizeof(target_str);
			err = util_string_get(parser, FOTA_AUTO_PARAM_TARGET, target_str,
					      &target_len);
			/* An omitted target keeps the stored one. */
			if (err && err != -ENODATA) {
				return err;
			}
			if (!err) {
				if (!strcmp(target_str, "app")) {
					target = FOTA_AUTO_TARGET_APP;
				} else if (!strcmp(target_str, "modem")) {
					target = FOTA_AUTO_TARGET_MODEM;
				} else if (!strcmp(target_str, "all")) {
					target = FOTA_AUTO_TARGET_ALL;
				} else {
					return -EINVAL;
				}
			}
		}

		if (param_count > FOTA_AUTO_PARAM_INTERVAL) {
			err = at_parser_num_get(parser, FOTA_AUTO_PARAM_INTERVAL, &interval);
			/* An omitted interval keeps the stored one. */
			if (err && err != -ENODATA) {
				return err;
			}
			if (!err && (interval < FOTA_AUTO_INTERVAL_MIN ||
				     interval > FOTA_AUTO_INTERVAL_MAX)) {
				return -EINVAL;
			}
		}

		fota_auto_enabled = enable;
		fota_auto_target = target;
		fota_auto_interval = interval;

		fota_auto_rearm();

		err = fota_auto_settings_save();
		if (err) {
			/* Applied either way; a partial save may store new and old values
			 * mixed.
			 */
			LOG_WRN("Failed to store the automatic FOTA check configuration: %d",
				err);
		}

		return 0;

	case AT_PARSER_CMD_TYPE_READ:
		rsp_send("\r\n#XNRFCLOUDFOTAAUTO: %d,\"%s\",%u\r\n", fota_auto_enabled,
			 fota_auto_target_str(fota_auto_target), fota_auto_interval);
		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUDFOTAAUTO: (0,1),(\"app\",\"modem\",\"all\"),(%d-%d)\r\n",
			 FOTA_AUTO_INTERVAL_MIN, FOTA_AUTO_INTERVAL_MAX);
		return 0;

	default:
		return -ENOTSUP;
	}
}
