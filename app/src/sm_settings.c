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

LOG_MODULE_REGISTER(sm_settings, CONFIG_SM_LOG_LEVEL);

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (!strcmp(name, "modem_full_fota")) {
		if (len != sizeof(sm_modem_full_fota))
			return -EINVAL;
		if (read_cb(cb_arg, &sm_modem_full_fota, len) > 0)
			return 0;
	}
	if (!strcmp(name, "bootloader_mode_requested")) {
		if (len != sizeof(sm_bootloader_mode_requested))
			return -EINVAL;
		if (read_cb(cb_arg, &sm_bootloader_mode_requested, len) > 0)
			return 0;
	}
	if (!strcmp(name, "full_mfw_dfu_segment_type")) {
		if (len != sizeof(full_mfw_dfu_segment_type))
			return -EINVAL;
		if (read_cb(cb_arg, &full_mfw_dfu_segment_type, len) > 0)
			return 0;
	}
	/* Simply ignore obsolete settings that are not in use anymore.
	 * settings_delete() does not completely remove settings.
	 */
	return 0;
}

static struct settings_handler sm_settings_conf = {
	.name = "sm",
	.h_set = settings_set
};

int sm_settings_init(void)
{
	int ret;

	ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("Init setting failed: %d", ret);
		return ret;
	}
	ret = settings_register(&sm_settings_conf);
	if (ret) {
		LOG_ERR("Register setting failed: %d", ret);
		return ret;
	}
	ret = settings_load_subtree("sm");
	if (ret) {
		LOG_ERR("Load setting failed: %d", ret);
	}

	return ret;
}

int sm_settings_fota_save(void)
{
	return settings_save_one("sm/modem_full_fota",
		&sm_modem_full_fota, sizeof(sm_modem_full_fota));
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
