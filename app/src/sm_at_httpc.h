/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_HTTPC_H_
#define SM_AT_HTTPC_H_

#include <stdbool.h>
#include <stdint.h>

/**@file sm_at_httpc.h
 *
 * @brief HTTP client AT command interface.
 * @{
 */

/**
 * @brief Notify HTTP client of poll events (called from socket layer)
 *
 * @param fd Socket file descriptor
 * @param events Poll events (ZSOCK_POLLIN, etc.)
 * @return true if HTTP client needs POLLIN re-armed after callback
 */
bool sm_at_httpc_poll_event(int fd, uint8_t events);

/**
 * @brief Notify HTTP client that a socket has been closed (e.g. via AT#XCLOSE).
 *
 * If a request is active on @p fd, it is cancelled: a cancel status URC
 * (#XHTTPCSTAT with -1 status) is sent to the host and the request is freed.
 * No-op if no request is active on @p fd.
 *
 * @param fd Socket file descriptor that was closed.
 */
void sm_at_httpc_socket_closed(int fd);

/** @} */

#endif /* SM_AT_HTTPC_H_ */
