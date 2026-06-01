/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Stub for nrf_cloud_coap.h.
 *
 * Compilation requirement: full type definitions are pulled in through the
 * normal include chain (lte_lc.h, wifi_location_common.h, nrf_cloud_location.h)
 * exactly as in production code.
 *
 * CMock requirement: CMock's Ruby parser runs with WORKING_DIRECTORY set to
 * the project source root and therefore cannot resolve the relative include
 * paths.  The parser skips includes it cannot find and parses only the
 * function declarations that appear directly in this file — which is
 * sufficient to generate correct mock stubs.
 */

#ifndef NRF_CLOUD_COAP_TEST_STUB_H_
#define NRF_CLOUD_COAP_TEST_STUB_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

/*
 * Pull in the full type definitions that sm_at_nrfcloud.c needs when it
 * accesses struct members (wifi_scan_info, lte_lc_cells_info, etc.).
 * CMock's Ruby parser will skip these includes if it cannot find them; the
 * function signatures below are enough for mock generation.
 */
#include <modem/lte_lc.h>
#include "net/wifi_location_common.h"
#include "net/nrf_cloud_location.h"

/* ----------------------------------------------------------------------
 * CoAP content-format constant.
 * In production code this comes from <zephyr/net/coap.h> when
 * CONFIG_NRF_CLOUD_COAP is set.  Without that Kconfig the real header
 * defines a stub enum without the actual values, so we define it here.
 * ----------------------------------------------------------------------
 */
#ifndef COAP_CONTENT_FORMAT_APP_JSON
#define COAP_CONTENT_FORMAT_APP_JSON 50
#endif

/* ----------------------------------------------------------------------
 * CoAP response callback – matches the real coap_client_response_cb_t.
 * ----------------------------------------------------------------------
 */
#ifndef COAP_CLIENT_RESPONSE_CB_T_DEFINED
#define COAP_CLIENT_RESPONSE_CB_T_DEFINED
typedef void (*coap_client_response_cb_t)(int16_t result_code, size_t offset,
					  const uint8_t *payload, size_t len,
					  bool last_block, void *user_data);
#endif

/* ----------------------------------------------------------------------
 * Wi-Fi and location constants (also defined in the sub-stubs; guards
 * prevent redefinition when this header is included first).
 * ----------------------------------------------------------------------
 */
#ifndef NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN
#define NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN  2
#endif
#ifndef NRF_CLOUD_LOCATION_WIFI_OMIT_RSSI
#define NRF_CLOUD_LOCATION_WIFI_OMIT_RSSI   (INT8_MAX)
#endif

/* ----------------------------------------------------------------------
 * nRF Cloud CoAP location request – matches struct nrf_cloud_coap_location_request
 * in the real nrf_cloud_coap.h.
 * ----------------------------------------------------------------------
 */
#ifndef NRF_CLOUD_COAP_LOCATION_REQUEST_DEFINED
#define NRF_CLOUD_COAP_LOCATION_REQUEST_DEFINED
/* Forward declaration; the test always passes config = NULL. */
struct nrf_cloud_location_config;

struct nrf_cloud_coap_location_request {
	struct lte_lc_cells_info *cell_info;
	struct wifi_scan_info *wifi_info;
	const struct nrf_cloud_location_config *config;
};
#endif /* NRF_CLOUD_COAP_LOCATION_REQUEST_DEFINED */

/* ----------------------------------------------------------------------
 * nRF Cloud CoAP API – mocked by CMock
 * ----------------------------------------------------------------------
 */
int nrf_cloud_coap_init(void);
int nrf_cloud_coap_connect(const char *const app_ver);
int nrf_cloud_coap_disconnect(void);
int nrf_cloud_coap_location_get(struct nrf_cloud_coap_location_request const *const request,
				struct nrf_cloud_location_result *const result);
int nrf_cloud_coap_post(const char *resource, const char *query,
			const uint8_t *buf, size_t len,
			int fmt, bool reliable,
			coap_client_response_cb_t cb, void *user);

#endif /* NRF_CLOUD_COAP_TEST_STUB_H_ */
