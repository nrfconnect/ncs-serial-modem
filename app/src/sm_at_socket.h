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
 * @brief Bind socket to a local network address.
 *
 * @param socket Socket to bind.
 * @param family Socket family (AF_INET/AF_INET6).
 * @param port Port to bind to.
 *
 * @retval 0 If the operation was successful.
 *         Otherwise, a (negative) error code is returned.
 */
int sm_bind_to_local_addr(int socket, int family, uint16_t port);

/**
 * @brief Notify socket AT command parser that the data mode has been exited.
 */
void sm_at_socket_notify_datamode_exit(void);

/**
 * @brief Initialize socket AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_socket_init(void);

/**
 * @brief Uninitialize socket AT command parser.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int sm_at_socket_uninit(void);
/** @} */

#endif /* SM_AT_SOCKET_ */
