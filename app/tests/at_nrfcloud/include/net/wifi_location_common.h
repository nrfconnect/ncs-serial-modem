/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Minimal stub for wifi_location_common.h used in unit tests. */

#ifndef WIFI_LOCATION_COMMON_TEST_STUB_H_
#define WIFI_LOCATION_COMMON_TEST_STUB_H_

#include <stdint.h>

#ifndef WIFI_MAC_ADDR_LEN
#define WIFI_MAC_ADDR_LEN 6
#endif

#ifndef WIFI_MAC_ADDR_STR_LEN
/* 2 chars per byte + 5 colons */
#define WIFI_MAC_ADDR_STR_LEN ((WIFI_MAC_ADDR_LEN * 2) + 5)
#endif

#ifndef WIFI_MAC_ADDR_TEMPLATE
#define WIFI_MAC_ADDR_TEMPLATE "%x:%x:%x:%x:%x:%x"
#endif

#ifndef WIFI_SECURITY_TYPE_UNKNOWN
#define WIFI_SECURITY_TYPE_UNKNOWN 0
#endif

#ifndef WIFI_MFP_UNKNOWN
#define WIFI_MFP_UNKNOWN 0
#endif

/** @brief Access point information from a Wi-Fi scan. */
struct wifi_scan_result {
	uint8_t mac[WIFI_MAC_ADDR_LEN];
	uint8_t mac_length;
	int8_t rssi;
	uint8_t band;
	int security;
	int mfp;
};

/** @brief Access points found during a Wi-Fi scan. */
struct wifi_scan_info {
	struct wifi_scan_result *ap_info;
	uint16_t cnt;
};

#endif /* WIFI_LOCATION_COMMON_TEST_STUB_H_ */
