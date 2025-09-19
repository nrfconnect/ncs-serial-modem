/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_SMS_
#define SM_AT_SMS_

/**@file sm_at_sms.h
 *
 * @brief Vendor-specific AT command for SMS service.
 * @{
 */

/**
 * @brief Initialize SMS AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_sms_init(void);

/**
 * @brief Uninitialize SMS AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_sms_uninit(void);
/** @} */

#endif /* SM_AT_SMS_ */
