/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <fw_info.h>

/* Stub for tfm_platform_s0_active */
int tfm_platform_s0_active(uint32_t s0_address, uint32_t s1_address, bool *s0_active)
{
	(void)s0_address;
	(void)s1_address;
	*s0_active = true;
	return 0;
}

/* Stub for tfm_platform_firmware_info */
int tfm_platform_firmware_info(uint32_t fw_address, struct fw_info *info)
{
	(void)fw_address;
	info->version = 0;
	return 0;
}
