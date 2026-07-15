/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Minimal test stub for <memfault/http/http_client.h>.
 * Only the api_key field of sMfltHttpClientConfig is used by sm_at_nrfcloud.c.
 */

#ifndef STUB_MEMFAULT_HTTP_HTTP_CLIENT_H_
#define STUB_MEMFAULT_HTTP_HTTP_CLIENT_H_

typedef struct {
	const char *api_key;
} sMfltHttpClientConfig;

extern sMfltHttpClientConfig g_mflt_http_client_config;

#endif /* STUB_MEMFAULT_HTTP_HTTP_CLIENT_H_ */
