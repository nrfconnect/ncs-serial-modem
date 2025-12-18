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

/* Whether the connection to nRF Cloud is ready. */
extern bool sm_nrf_cloud_ready;

/* Whether to send the device's location to nRF Cloud. */
extern bool sm_nrf_cloud_send_location;

/**
 * @brief Uninitialize nRF Cloud AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_nrfcloud_uninit(void);

/** @} */
#endif /* SM_AT_NRFCLOUD_ */
