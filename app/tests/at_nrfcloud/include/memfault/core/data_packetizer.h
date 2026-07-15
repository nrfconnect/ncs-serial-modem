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

bool memfault_packetizer_get_chunk(void *buf, size_t *buf_len);

#endif /* STUB_MEMFAULT_CORE_DATA_PACKETIZER_H_ */
