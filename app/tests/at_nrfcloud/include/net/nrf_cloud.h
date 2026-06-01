/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Minimal stub for nrf_cloud.h used in unit tests. */

#ifndef NRF_CLOUD_TEST_STUB_H_
#define NRF_CLOUD_TEST_STUB_H_

#include <stddef.h>

/** Maximum length of the nRF Cloud client ID. */
#define NRF_CLOUD_CLIENT_ID_MAX_LEN 64

/**
 * @brief Get the nRF Cloud client identifier (device ID).
 *
 * @param[out] id_buf    Buffer to receive the null-terminated device ID.
 * @param[in]  id_buf_sz Size of id_buf; must be at least
 *                       NRF_CLOUD_CLIENT_ID_MAX_LEN + 1.
 * @return 0 on success, negative errno on error.
 */
int nrf_cloud_client_id_get(char *id_buf, size_t id_buf_sz);

#endif /* NRF_CLOUD_TEST_STUB_H_ */
