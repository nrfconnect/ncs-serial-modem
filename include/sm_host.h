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
 * @typedef sm_ri_handler_t
 *
 * Handler to handle the Ring Indicate (RI) signal from Serial Modem.
 */
typedef void (*sm_ri_handler_t)(void);

/**@brief Initialize Serial Modem Host library.
 *
 * @param handler Pointer to a handler function of type @ref sm_data_handler_t.
 * @param automatic_uart If true, DTR and UART are automatically managed by the library.
 * @param inactivity_timeout Inactivity timeout for DTR and UART disablement. Only used if @p
 * automatic is true.
 *
 * @return Zero on success, non-zero otherwise.
 */
int sm_host_init(sm_data_handler_t handler, bool automatic_uart, k_timeout_t inactivity_timeout);

/**@brief Un-initialize Serial Modem Host
 */
int sm_host_uninit(void);

/**
 * @brief Register callback for Ring Indicate (RI) pin.
 *
 * @param handler Pointer to a handler function of type @ref sm_ri_handler_t.
 *
 * @retval Zero on success. Otherwise, a (negative) error code is returned.
 */
int sm_host_register_ri_handler(sm_ri_handler_t handler);

/**
 * @brief Configure automatic DTR UART handling
 *
 * If automatic DTR UART handling is enabled, the library will enable DTR UART when RI
 * signal is detected, and disable it after inactivity timeout.
 *
 * @param automatic If true, DTR UART is automatically managed by the library.
 * @param inactivity Inactivity timeout for DTR UART disablement. Only used if @p
 * automatic is true.
 *
 * @return Zero on success, non-zero otherwise.
 */
void sm_host_configure_dtr_uart(bool automatic, k_timeout_t inactivity);

/**
 * @brief Disable DTR UART
 *
 * Disables DTR UART. Disables automatic DTR UART handling.
 */
void sm_host_disable_dtr_uart(void);

/** @brief Enable DTR UART
 *
 * Enables DTR UART. Disables automatic DTR UART handling.
 */
void sm_host_enable_dtr_uart(void);

/**
 * @brief Function to send an AT command in Serial Modem command mode
 *
 * This function wait until command result is received. The response of the AT command is received
 * through the sm_ind_handler_t registered in @ref sm_host_init.
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
