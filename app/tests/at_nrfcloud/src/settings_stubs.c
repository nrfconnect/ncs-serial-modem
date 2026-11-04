/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include "sm_at_fota.h"
#include "sm_settings.h"

bool sm_init_failed;
bool sm_bootloader_mode_requested;
int full_mfw_dfu_segment_type;
uint32_t sm_fota_bl_version_before;
enum sm_fota_image_type sm_fota_type;
bool sm_fota_nrfcloud;
uint8_t sm_modem_init_eio_retry_count;

int test_fault_save_error;
int test_fault_delete_error;
int test_fault_save_count;
int test_fault_delete_count;
uint8_t test_fault_record[sizeof(struct sm_modem_fault)];

static int fault_store_save(struct settings_store *cs, const char *name, const char *value,
			    size_t len)
{
	ARG_UNUSED(cs);

	if (strcmp(name, "sm/modem_fault")) {
		return -ENOENT;
	}
	if (!len) {
		test_fault_delete_count++;
		return test_fault_delete_error;
	}
	test_fault_save_count++;
	if (test_fault_save_error) {
		return test_fault_save_error;
	}
	if (len != sizeof(test_fault_record)) {
		return -EINVAL;
	}
	memcpy(test_fault_record, value, len);
	return 0;
}

static const struct settings_store_itf fault_store_itf = {
	.csi_save = fault_store_save,
};
static struct settings_store fault_store = {
	.cs_itf = &fault_store_itf,
};

void test_fault_settings_reset(void)
{
	settings_dst_register(&fault_store);
	test_fault_delete_error = 0;
	(void)sm_settings_modem_fault_clear();
	test_fault_save_error = 0;
	test_fault_save_count = 0;
	test_fault_delete_count = 0;
	memset(test_fault_record, 0, sizeof(test_fault_record));
}
