/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Minimal test stub for <memfault/core/data_packetizer.h>. */

#ifndef STUB_MEMFAULT_CORE_DATA_PACKETIZER_H_
#define STUB_MEMFAULT_CORE_DATA_PACKETIZER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	kMfltDataSourceMask_None = 0,
	kMfltDataSourceMask_Coredump = (1U << 0),
	kMfltDataSourceMask_Event = (1U << 1),
	kMfltDataSourceMask_Log = (1U << 2),
	kMfltDataSourceMask_Cdr = (1U << 3),
	kMfltDataSourceMask_All = (kMfltDataSourceMask_Coredump | kMfltDataSourceMask_Event |
				   kMfltDataSourceMask_Log | kMfltDataSourceMask_Cdr)
} eMfltDataSourceMask;

bool memfault_packetizer_get_chunk(void *buf, size_t *buf_len);
void memfault_packetizer_set_active_sources(uint32_t mask);

#endif /* STUB_MEMFAULT_CORE_DATA_PACKETIZER_H_ */
