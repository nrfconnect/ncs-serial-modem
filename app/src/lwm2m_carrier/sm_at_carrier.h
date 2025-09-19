/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_CARRIER_
#define SM_AT_CARRIER_

#include <stdint.h>

/**@file sm_at_carrier.h
 *
 * @brief Vendor-specific AT command for LwM2M Carrier service.
 * @{
 */
/**
 * @brief Initialize Carrier AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_carrier_init(void);

/**
 * @brief Uninitialize Carrier AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_carrier_uninit(void);

/** @} */

#endif /* SM_AT_CARRIER_ */
