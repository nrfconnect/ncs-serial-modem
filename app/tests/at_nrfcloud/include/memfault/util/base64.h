/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Minimal test stub for <memfault/util/base64.h>. */

#ifndef STUB_MEMFAULT_UTIL_BASE64_H_
#define STUB_MEMFAULT_UTIL_BASE64_H_

#include <stddef.h>

#define MEMFAULT_BASE64_ENCODE_LEN(bin_len) (4 * (((bin_len) + 2) / 3))
#define MEMFAULT_BASE64_MAX_DECODE_LEN(base64_len) ((3 * (base64_len)) / 4)

void memfault_base64_encode(const void *buf, size_t buf_len, void *base64_out);

#endif /* STUB_MEMFAULT_UTIL_BASE64_H_ */
