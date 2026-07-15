/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Minimal test stub for <memfault/core/platform/device_info.h>. */

#ifndef STUB_MEMFAULT_CORE_PLATFORM_DEVICE_INFO_H_
#define STUB_MEMFAULT_CORE_PLATFORM_DEVICE_INFO_H_

typedef struct MemfaultDeviceInfo {
	const char *device_serial;
	const char *software_type;
	const char *software_version;
	const char *hardware_version;
} sMemfaultDeviceInfo;

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info);

#endif /* STUB_MEMFAULT_CORE_PLATFORM_DEVICE_INFO_H_ */
