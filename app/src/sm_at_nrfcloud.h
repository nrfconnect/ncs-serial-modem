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

struct modem_pipe;

/**
 * @brief Whether the connection to nRF Cloud is ready.
 *
 * Reflects the nRF Cloud CoAP connection, regardless of whether AT#XNRFCLOUD or another
 * command, such as AT#XNRFCLOUDFOTA, opened it.
 */
bool sm_nrf_cloud_is_ready(void);

/**
 * @brief Send the #XNRFCLOUD URC reporting that the connection to nRF Cloud is ready.
 *
 * For commands other than AT#XNRFCLOUD that open the connection.
 *
 * @param[in] pipe Pipe to send the URC to.
 */
void sm_nrf_cloud_notify_connected(struct modem_pipe *pipe);

/* Whether to send the device's location to nRF Cloud. Reset by AT#XNRFCLOUD=0. */
extern bool sm_nrf_cloud_send_location;

/**
 * @brief Callback invoked on completion of an async NCELLMEAS run.
 *
 * Called from sm_work_q context. The callback takes ownership of @p cell_data
 * and must release it with sm_at_nrfcloud_ncellmeas_cleanup() when done.
 * @p cell_data may be NULL if allocation failed.
 *
 * @param[in] cell_data Measurement results, or NULL on allocation failure.
 * @param[in] ctx       Opaque pointer passed to sm_at_nrfcloud_ncellmeas_start().
 */
typedef void (*sm_at_nrfcloud_ncellmeas_done_cb_t)(struct lte_lc_cells_info *cell_data, void *ctx);

/**
 * @brief Start asynchronous cellular neighbor cell measurements.
 *
 * Issues the first AT%%NCELLMEAS command and returns immediately.
 * Progress is URC-driven; @p cb is invoked from sm_work_q when all phases
 * are complete.  Pass @p cb = NULL with @p send_loc_req = true to have the
 * implementation submit the nRF Cloud location request automatically.
 *
 * @param[in] cell_count    Number of cells to search for.
 * @param[in] send_loc_req  If true, submit the nRF Cloud location request on
 *                          completion (cb is ignored).
 * @param[in] cb            Completion callback (used when send_loc_req is false).
 * @param[in] ctx           Opaque pointer forwarded to @p cb.
 *
 * @return 0 on success, negative errno on error.
 */
int sm_at_nrfcloud_ncellmeas_start(uint8_t cell_count, bool send_loc_req,
				   sm_at_nrfcloud_ncellmeas_done_cb_t cb, void *ctx);

/**
 * @brief Cleanup the cellular neighbor cell measurement data.
 *
 * @param[in] cell_data Data to be cleaned up.
 */
void sm_at_nrfcloud_ncellmeas_cleanup(struct lte_lc_cells_info *cell_data);

#endif /* SM_AT_NRFCLOUD_ */

/** @} */
