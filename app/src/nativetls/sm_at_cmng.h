/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_CMNG_
#define SM_AT_CMNG_

/**@file sm_at_cmng.h
 *
 * @brief Serial Modem specific AT command for credential storing.
 * @{
 */

enum sm_cmng_type {
	SM_AT_CMNG_TYPE_CA_CERT,
	SM_AT_CMNG_TYPE_CLIENT_CERT,
	SM_AT_CMNG_TYPE_CLIENT_KEY,
	SM_AT_CMNG_TYPE_PSK,
	SM_AT_CMNG_TYPE_PSK_ID,
	SM_AT_CMNG_TYPE_COUNT
};

/** @} */

#endif /* SM_AT_CMNG_ */
