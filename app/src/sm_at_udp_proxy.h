/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_UDP_PROXY_
#define SM_AT_UDP_PROXY_

/**@file sm_at_udp_proxy.h
 *
 * @brief Vendor-specific AT command for UDP proxy service.
 * @{
 */

/**
 * @brief Initialize UDP proxy AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_udp_proxy_init(void);

/**
 * @brief Uninitialize UDP proxy AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_udp_proxy_uninit(void);
/** @} */
#endif /* SM_AT_UDP_PROXY_ */
