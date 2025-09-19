/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_GNSS_
#define SM_AT_GNSS_

/**@file sm_at_gnss.h
 *
 * @brief Vendor-specific AT command for GNSS service.
 * @{
 */

/**
 * @brief Initialize GNSS AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_gnss_init(void);

/**
 * @brief Uninitialize GNSS AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_gnss_uninit(void);

/** @} */

#endif /* SM_AT_GNSS_ */
