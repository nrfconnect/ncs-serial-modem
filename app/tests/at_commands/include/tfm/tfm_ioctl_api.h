/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef TFM_IOCTL_API_H__
#define TFM_IOCTL_API_H__

#include <stdbool.h>
#include <stdint.h>
#include <fw_info.h>

/** Check if S0 is the active MCUboot slot. */
int tfm_platform_s0_active(uint32_t s0_address, uint32_t s1_address, bool *s0_active);

/** Read firmware info from a given address. */
int tfm_platform_firmware_info(uint32_t fw_address, struct fw_info *info);

#endif /* TFM_IOCTL_API_H__ */
