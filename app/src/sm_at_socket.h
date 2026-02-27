/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_SOCKET_H_
#define SM_AT_SOCKET_H_

/** @file sm_at_socket.h
 *
 * @brief Definitions for AT commands related to socket operations in Serial Modem
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include "sm_at_host.h"

/**
 * @brief Polling context for asynchronous socket events.
 *
 * This structure holds flags and work item and its going to be
 * defined for each AT host context.
 */
struct async_poll_ctx {
	struct k_work poll_work;         /**< Work to handle poll */
	struct k_work idle_work;         /**< Work to send poll URCs. */
	uint8_t xapoll_events_requested; /**< Events requested for all the sockets for async poll */
	uint8_t adr_flags;               /**< Auto reception flags for all sockets. */
	bool adr_hex: 1;                 /**< Auto reception hex mode for all sockets. */
};

/**
 * @brief Get the asynchronous poll context associated with the given modem pipe.
 *
 * This returns the async_poll_ctx associated with the AT host context that is attached to the given
 * modem pipe.
 *
 * When there is no AT host context attached to the given pipe, or the pipe is NULL, this returns
 * NULL.
 *
 * @param pipe The modem pipe for which to get the async poll context.
 * @return struct async_poll_ctx* poll context for the given modem pipe or NULL if no AT host
 * context is attached to the pipe.
 */
struct async_poll_ctx *sm_at_host_get_async_poll_ctx(struct modem_pipe *pipe);

/**
 * @brief Get associated modem pipe from the asynchronous poll context.
 *
 * @param poll_ctx poll context
 * @return struct modem_pipe*
 */
struct modem_pipe *sm_at_host_get_pipe_from_poll_ctx(struct async_poll_ctx *poll_ctx);

/**
 * @brief Handler for asynchronous socket poll work.
 *
 * This function is called when the poll work item is executed. It processes
 * asynchronous socket events for the associated AT host context.
 * It should not be called directly, but will be initialized as a work item handler in the AT host
 * context.
 *
 * @param work Work item associated with the asynchronous poll.
 */
void sm_at_socket_poll_work_handler(struct k_work *work);

/** Small idle wrapper for the proper poll-work */
void sm_at_socket_poll_idle_handler(struct k_work *work);

#endif /* SM_AT_SOCKET_H_ */
