/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <dfu/dfu_target.h>
#include <errno.h>
#include "sm_at_fota.h"
#include "sm_at_dfu.h"
#include "sm_settings.h"
#include "sm_defines.h"

LOG_MODULE_REGISTER(sm_settings, CONFIG_SM_LOG_LEVEL);

/* Reads a setting of exactly @p expect_len bytes into @p dst.
 *
 * The value is staged in the caller-provided destination only after the read
 * has fully succeeded, so a truncated or failing read can never leave a
 * partially-updated setting behind (CERT ERR33-C).
 */
static int settings_read_checked(const char *name, size_t len, settings_read_cb read_cb,
				 void *cb_arg, void *dst, size_t expect_len)
{
	ssize_t read_len;

	if (len != expect_len) {
		LOG_ERR("Setting \"%s\" has unexpected length %zu (expected %zu). Keeping default.",
			name, len, expect_len);
		return -EINVAL;
	}

	read_len = read_cb(cb_arg, dst, expect_len);
	if (read_len != (ssize_t)expect_len) {
		LOG_ERR("Failed to read setting \"%s\": %zd. Keeping default.", name, read_len);
		return -EIO;
	}

	return 0;
}

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (!strcmp(name, "bootloader_mode_requested")) {
		bool value;

		if (settings_read_checked(name, len, read_cb, cb_arg, &value, sizeof(value))) {
			return 0;
		}
		sm_bootloader_mode_requested = value;
		return 0;
	}
	if (!strcmp(name, "full_mfw_dfu_segment_type")) {
		int value;

		if (settings_read_checked(name, len, read_cb, cb_arg, &value, sizeof(value))) {
			return 0;
		}
		/* Reject out-of-range values from a corrupt or downgraded record so
		 * that the DFU code never dispatches on an unknown segment type.
		 */
		if (value != DFU_FULL_MFW_SEGMENT_BOOTLOADER &&
		    value != DFU_FULL_MFW_SEGMENT_FIRMWARE) {
			LOG_ERR("Invalid stored full MFW segment type %d. Keeping default.", value);
			return 0;
		}
		full_mfw_dfu_segment_type = value;
		return 0;
	}
	if (!strcmp(name, "bl_fota_ver")) {
		uint32_t value;

		if (settings_read_checked(name, len, read_cb, cb_arg, &value, sizeof(value))) {
			return 0;
		}
		sm_fota_bl_version_before = value;
		return 0;
	}
	if (!strcmp(name, "fota_type")) {
		enum sm_fota_image_type value;

		if (settings_read_checked(name, len, read_cb, cb_arg, &value, sizeof(value))) {
			return 0;
		}
		if (value < SM_FOTA_TYPE_NONE || value > SM_FOTA_TYPE_FULL_MFW) {
			LOG_ERR("Invalid stored FOTA type %d. Keeping default.", (int)value);
			return 0;
		}
		sm_fota_type = value;
		return 0;
	}
	/* Simply ignore obsolete settings that are not in use anymore.
	 * settings_delete() does not completely remove settings.
	 */
	return 0;
}

/* Restores every setting-backed variable to its compiled-in default. Called
 * before settings_load_subtree() so that a partial or corrupt NVS record can
 * never leave a variable in an indeterminate state.
 */
static void settings_set_defaults(void)
{
	sm_bootloader_mode_requested = false;
	full_mfw_dfu_segment_type = DFU_FULL_MFW_SEGMENT_BOOTLOADER;
	sm_fota_bl_version_before = 0;
	sm_fota_type = SM_FOTA_TYPE_NONE;
}

static struct settings_handler sm_settings_conf = {
	.name = "sm",
	.h_set = settings_set
};

static int sm_settings_init(void)
{
	int ret;

	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("Init setting failed: %d", ret);
		sm_init_failed = true;
		return ret;
	}
	ret = settings_register(&sm_settings_conf);
	if (ret) {
		LOG_ERR("Register setting failed: %d", ret);
		sm_init_failed = true;
		return ret;
	}
	settings_set_defaults();
	ret = settings_load_subtree("sm");
	if (ret) {
		LOG_ERR("Load setting failed: %d", ret);
		sm_init_failed = true;
	}

	return ret;
}
/* Run before APPLICATION init functions, so modules can use settings that are load from
 * flash
 */
SYS_INIT(sm_settings_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

int sm_settings_fota_save(void)
{
	int err;

	err = settings_save_one("sm/bl_fota_ver", &sm_fota_bl_version_before,
				 sizeof(sm_fota_bl_version_before));
	if (err) {
		return err;
	}
	return settings_save_one("sm/fota_type", &sm_fota_type, sizeof(sm_fota_type));
}

int sm_settings_bootloader_mode_save(void)
{
	return settings_save_one("sm/bootloader_mode_requested",
		&sm_bootloader_mode_requested, sizeof(sm_bootloader_mode_requested));
}

int sm_settings_full_mfw_dfu_segment_type_save(void)
{
	return settings_save_one("sm/full_mfw_dfu_segment_type",
		&full_mfw_dfu_segment_type, sizeof(full_mfw_dfu_segment_type));
}
