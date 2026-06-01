/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Minimal stub for nrf_cloud_location.h used in unit tests. */

#ifndef NRF_CLOUD_LOCATION_TEST_STUB_H_
#define NRF_CLOUD_LOCATION_TEST_STUB_H_

#include <stdint.h>
#include <limits.h>

/** Minimum number of access points required for a Wi-Fi location request. */
#define NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN   2

/** Sentinel RSSI value indicating that RSSI should be omitted from the request. */
#define NRF_CLOUD_LOCATION_WIFI_OMIT_RSSI    (INT8_MAX)

/** @brief Location result type from nRF Cloud (matches real SDK enum order) */
#ifndef NRF_CLOUD_LOCATION_TYPE_DEFINED
#define NRF_CLOUD_LOCATION_TYPE_DEFINED
enum nrf_cloud_location_type {
	LOCATION_TYPE_SINGLE_CELL = 0,
	LOCATION_TYPE_MULTI_CELL,
	LOCATION_TYPE_WIFI,
	LOCATION_TYPE__INVALID,
};

struct nrf_cloud_location_result {
	enum nrf_cloud_location_type type;
	double lat;
	double lon;
	uint32_t unc;
};
#endif /* NRF_CLOUD_LOCATION_TYPE_DEFINED */

#endif /* NRF_CLOUD_LOCATION_TEST_STUB_H_ */
