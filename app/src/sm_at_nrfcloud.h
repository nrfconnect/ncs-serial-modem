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
 * @brief Execute cellular positioning.
 *
 * @param[in] cell_count Number of cells to search for.
 * @param[in] cell_data Cell data structure to be filled.
 *            NULL means that the function will allocate the memory.
 */
void scan_cellular_execute(uint8_t cell_count, struct lte_lc_cells_info *cell_data);

/** @} */
#endif /* SM_AT_NRFCLOUD_ */
