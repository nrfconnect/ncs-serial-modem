/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_TWI_
#define SM_AT_TWI_

/**@file sm_at_twi.h
 *
 * @brief Vendor-specific AT command for TWI service.
 * @{
 */

/**
 * @brief Initialize TWI AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_twi_init(void);

/**
 * @brief Uninitialize TWI AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_twi_uninit(void);

/** @} */

#endif /* SM_AT_TWI_ */
