/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_NRFCLOUD_
#define SM_AT_NRFCLOUD_

/** @file sm_at_nrfcloud.h
 *
 * @brief Vendor-specific AT command for nRF Cloud service.
 * @{
 */
#include <stdbool.h>
#include <modem/lte_lc.h>

/* Whether the connection to nRF Cloud is ready. */
extern bool sm_nrf_cloud_ready;

/* Whether to send the device's location to nRF Cloud. */
extern bool sm_nrf_cloud_send_location;

/**
 * @brief Execute cellular neighbor cell measurements.
 *
 * @param[in] cell_count Number of cells to search for.
 * @param[in] send_log_req Whether to send a location request to cloud after neighbor measurements.
 *
 * @return Cell search results allocated from the heap and caller must release it with free().
 */
 struct lte_lc_cells_info *sm_at_nrfcloud_ncellmeas(uint8_t cell_count, bool send_loc_req);

/**
 * @brief Cleanup the cellular neighbor cell measurement data.
 *
 * @param[in] cell_data Data to be cleaned up.
 */
void sm_at_nrfcloud_ncellmeas_cleanup(struct lte_lc_cells_info *cell_data);

#endif /* SM_AT_NRFCLOUD_ */

/** @} */
