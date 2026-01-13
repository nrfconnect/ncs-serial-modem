/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_SOCKET_
#define SM_AT_SOCKET_

/**@file sm_at_socket.h
 *
 * @brief Vendor-specific AT command for Socket service.
 * @{
 */

/**
 * @brief Uninitialize socket AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_socket_uninit(void);
/** @} */

#endif /* SM_AT_SOCKET_ */
