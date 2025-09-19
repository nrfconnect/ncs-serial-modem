/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_HOST_H_
#define SM_HOST_H_

/**
 * @file sm_host.h
 *
 * @defgroup sm_host Serial Modem Host library
 *
 * @{
 *
 * @brief Public APIs for the Serial Modem Host library.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/toolchain.h>

/** Max size of AT command response is 2100 bytes. */
#define SM_AT_CMD_RESPONSE_MAX_LEN 2100

/**
 * @brief AT command result codes
 */
enum at_cmd_state {
	AT_CMD_OK,
	AT_CMD_ERROR,
	AT_CMD_ERROR_CMS,
	AT_CMD_ERROR_CME,
	AT_CMD_PENDING
};

/**
 * @typedef sm_data_handler_t
 *
 * Handler to handle data received from Serial Modem, which could be AT response, AT notification
 * or simply raw data (for example DFU image).
 *
 * @param data    Data received from Serial Modem.
 * @param datalen Length of the data received.
 *
 * @note The handler runs from uart callback. It must not call @ref sm_host_send_cmd. The data
 * should be copied out by the application as soon as called.
 */
typedef void (*sm_data_handler_t)(const uint8_t *data, size_t datalen);

/**
 * @typedef sm_ind_handler_t
 *
 * Handler to handle @kconfig{CONFIG_SM_HOST_INDICATE_PIN} signal from Serial Modem.
 */
typedef void (*sm_ind_handler_t)(void);

/**@brief Initialize Serial Modem Host library.
 *
 * @param handler Pointer to a handler function of type @ref sm_data_handler_t.
 *
 * @return Zero on success, non-zero otherwise.
 */
int sm_host_init(sm_data_handler_t handler);

/**@brief Un-initialize Serial Modem Host
 */
int sm_host_uninit(void);

/**
 * @brief Register callback for @kconfig{CONFIG_SM_HOST_INDICATE_PIN} indication
 *
 * @param handler Pointer to a handler function of type @ref sm_ind_handler_t.
 * @param wakeup  Enable/disable System Off wakeup by GPIO Sense.
 *
 * @retval Zero    Success.
 * @retval -EFAULT if @kconfig{CONFIG_SM_HOST_INDICATE_PIN} is not defined.
 */
int sm_host_register_ind(sm_ind_handler_t handler, bool wakeup);

/**
 * @brief Toggle power pin of the nRF91 Series device configured with
 * @kconfig{CONFIG_SM_HOST_POWER_PIN}.
 *
 * The pin is enabled for the time specified in @kconfig{CONFIG_SM_HOST_POWER_PIN_TIME}
 * and then disabled.
 *
 * @return Zero on success, non-zero otherwise.
 */
int sm_host_power_pin_toggle(void);

/**
 * @brief Function to send an AT command in Serial Modem command mode
 *
 * This function wait until command result is received. The response of the AT command is received
 * via the @ref sm_ind_handler_t registered in @ref sm_host_init.
 *
 * @param command Pointer to null terminated AT command string without command terminator
 * @param timeout Response timeout for the command in seconds, Zero means infinite wait
 *
 * @retval state @ref at_cmd_state if command execution succeeds.
 * @retval -EAGAIN if command execution times out.
 * @retval other if command execution fails.
 */
int sm_host_send_cmd(const char *const command, uint32_t timeout);

/**
 * @brief Function to send raw data in Serial Modem data mode
 *
 * @param data    Raw data to send
 * @param datalen Length of the raw data
 *
 * @return Zero on success, non-zero otherwise.
 */
int sm_host_send_data(const uint8_t *const data, size_t datalen);

/**
 * @brief Serial Modem monitor callback.
 *
 * @param notif The AT notification callback.
 */
typedef void (*sm_monitor_handler_t)(const char *notif);

/**
 * @brief Serial Modem monitor entry.
 */
struct sm_monitor_entry {
	/** The filter for this monitor. */
	const char *filter;
	/** Monitor callback. */
	const sm_monitor_handler_t handler;
	/** Monitor is paused. */
	uint8_t paused;
};

/** Wildcard. Match any notifications. */
#define MON_ANY NULL
/** Monitor is paused. */
#define MON_PAUSED 1
/** Monitor is active, default */
#define MON_ACTIVE 0

/**
 * @brief Define an Serial Modem monitor to receive notifications in the system workqueue thread.
 *
 * @param name The monitor's name.
 * @param _filter The filter for AT notification the monitor should receive,
 *		  or @c MON_ANY to receive all notifications.
 * @param _handler The monitor callback.
 * @param ... Optional monitor initial state (@c MON_PAUSED or @c MON_ACTIVE).
 *	      The default initial state of a monitor is @c MON_ACTIVE.
 */
#define SM_MONITOR(name, _filter, _handler, ...)                                                  \
	static void _handler(const char *);                                                        \
	static STRUCT_SECTION_ITERABLE(sm_monitor_entry, name) = {                                \
		.filter = _filter,                                                                 \
		.handler = _handler,                                                               \
		COND_CODE_1(__VA_ARGS__, (.paused = __VA_ARGS__,), ())                             \
	}

/**
 * @brief Pause monitor.
 *
 * Pause monitor @p mon from receiving notifications.
 *
 * @param mon The monitor to pause.
 */
static inline void sm_monitor_pause(struct sm_monitor_entry *mon)
{
	mon->paused = MON_PAUSED;
}

/**
 * @brief Resume monitor.
 *
 * Resume forwarding notifications to monitor @p mon.
 *
 * @param mon The monitor to resume.
 */
static inline void sm_monitor_resume(struct sm_monitor_entry *mon)
{
	mon->paused = MON_ACTIVE;
}

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* SM_HOST_H_ */
