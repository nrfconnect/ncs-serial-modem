/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_HOST_
#define SM_AT_HOST_

/** @file sm_at_host.h
 *
 * @brief AT host for Serial Modem
 * @{
 */

#include <zephyr/types.h>
#include <ctype.h>
#include <nrf_modem_at.h>
#include <modem/at_monitor.h>
#include <modem/at_cmd_custom.h>
#include <modem/at_parser.h>
#include "sm_defines.h"
#include <zephyr/modem/pipe.h>

/* This delay is necessary to send AT responses at low baud rates. */
#define SM_UART_RESPONSE_DELAY K_MSEC(50)

#define SM_DATAMODE_FLAGS_NONE	0
#define SM_DATAMODE_FLAGS_MORE_DATA (1 << 0)
#define SM_DATAMODE_FLAGS_EXIT_HANDLER (1 << 1)

extern uint16_t sm_datamode_time_limit; /* Send trigger by time in data mode. */

/** @brief Operations in data mode. */
enum sm_datamode_operation {
	DATAMODE_SEND,  /* Send data in datamode */
	DATAMODE_EXIT   /* Exit data mode */
};

/** Forward declaration */
struct sm_at_host_ctx;

/** @brief Data mode sending handler type.
 *
 * @retval 0 means all data is sent successfully.
 *         Positive value means the actual size of bytes that has been sent.
 *         Negative value means error occurrs in sending.
 */
typedef int (*sm_datamode_handler_t)(uint8_t op, const uint8_t *data, int len, uint8_t flags);

/** @brief Sends the given string via the current AT backend. */
int sm_at_send_str(const char *str);

/**
 * @brief Powers the UART down.
 *
 * @retval 0 on success, or a (negative) error code.
 */
int sm_at_host_power_off(void);

/** @brief Counterpart to @c sm_at_host_power_off(). */
int sm_at_host_power_on(void);

/**
 * @brief Uninitialize AT host for Serial Modem
 */
void sm_at_host_uninit(void);

/**
 * @brief Send AT command response
 *
 * @param fmt Response message format string
 *
 */
void rsp_send(const char *fmt, ...);

/**
 * @brief Send URC message
 *
 * URC messages are queued and sent when possible.
 *
 * @param fmt URC message format string
 *
 */
void urc_send(const char *fmt, ...);

/**
 * @brief Send AT command response of OK
 */
void rsp_send_ok(void);

/**
 * @brief Send AT command response of ERROR
 */
void rsp_send_error(void);

/**
 * @brief Send raw data received in data mode
 *
 * @param data Raw data received
 * @param len Length of raw data
 *
 */
void data_send(const uint8_t *data, size_t len);

/**
 * @brief Request Serial Modem AT host to enter data mode
 *
 * No AT unsolicited message or command response allowed in data mode.
 *
 * @param handler Data mode handler provided by requesting module
 * @param data_len Expected data length to be sent in data mode. 0 means unknown length and
 *        that the termination command is required to exit the data mode.
 *
 * @retval 0 If the operation was successful.
 *         Otherwise, a (negative) error code is returned.
 */
int enter_datamode(sm_datamode_handler_t handler, size_t data_len);

/**
 * @brief Check whether Serial Modem AT host is in data mode
 *
 * @retval true if yes, false if no.
 */
bool in_datamode(void);

/**
 * @brief Check whether Serial Modem AT host is in AT command mode
 *
 * @retval true if yes, false if no.
 */
bool in_at_mode(void);

/**
 * @brief Exit the data mode handler
 *
 * Remove the callback to the data mode handler and start dropping the incoming data, until
 * the data mode is exited.
 *
 * @param result Result of sending in data mode.
 *
 * @retval true If handler has closed successfully.
 *         false If not in data mode.
 */
bool exit_datamode_handler(int result);

/** @brief Serial Modem AT command callback type. */
typedef int sm_at_callback(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			    uint32_t param_count);

/**
 * @brief Generic wrapper for a custom Serial Modem AT command callback.
 *
 * This will call the AT command handler callback, which is declared with SM_AT_CMD_CUSTOM.
 *
 * @param buf Response buffer.
 * @param len Response buffer size.
 * @param at_cmd AT command.
 * @param cb AT command callback.

 * @retval 0 on success.
 */
int sm_at_cb_wrapper(char *buf, size_t len, char *at_cmd, sm_at_callback cb);

/**
 * @brief Enable or disable echo of received characters.
 *
 * @param enable True to enable echo, false to disable.
 */
void sm_at_host_echo(bool enable);

/**
 * @brief Check whether echo URC delay is in progress.
 *
 * @retval true if echo URC delay is in progress, false otherwise.
 */
bool sm_at_host_echo_urc_delay(void);

/** @brief Events which can be notified by the AT host. */
enum sm_event {
	SM_EVENT_NONE,		 /**< No event. */
	SM_EVENT_URC = 0x01,     /**< URC can be sent. */
	SM_EVENT_AT_MODE = 0x02, /**< Entered AT command mode. */
};

/** @brief Event callback structure. */
struct sm_event_callback {
	sys_snode_t node;
	void (*cb)(struct k_work *work);
	enum sm_event events;
};

/**
 * @brief Register an event callback to be notified when the specified event occurs.
 * @param cb Pointer to the event callback structure.
 * @param event Event to register for.
 */
void sm_at_host_register_event_cb(struct sm_event_callback *cb, enum sm_event event);

/**
 * @brief Set the active modem pipe for AT communication.
 *
 * This allows switching between UART pipe (direct mode) and CMUX DLCI pipe.
 *
 * @param pipe Pointer to the modem_pipe to use for AT traffic.
 */
int sm_at_host_set_pipe(struct sm_at_host_ctx *ctx, struct modem_pipe *pipe);

/**
 * @brief Release the conxtext from associated pipe and mark for descruction.
 *
 * This detaches and releases the modem pipe from the AT host context
 * and mark the conxtext for descruction.
 *
 * @param ctx AT host context
 */
void sm_at_host_release(struct sm_at_host_ctx *ctx);

/**
 * @brief Attach a modem pipe to AT host module.
 *
 * If the pipe is already open, triggers the context creation immediately.
 * Otherwise, the context will be created when the pipe is opened.
 *
 * @param pipe Modem pipe to attach.
 */
void sm_at_host_attach(struct modem_pipe *pipe);

/**
 * @brief Get the modem pipe associated with the given AT host context.
 *
 * @param ctx AT host context
 * @return struct modem_pipe* or NULL if none is set.
 */
struct modem_pipe *sm_at_host_get_pipe(struct sm_at_host_ctx *ctx);


/** @brief Get the AT host context associated with a given modem pipe. */
struct sm_at_host_ctx *sm_at_host_get_ctx_from(struct modem_pipe *pipe);

/**
 * @brief Get the AT host context for URC transmission.
 *
 * URCs are always sent to the first (primary) instance.
 *
 * @return Pointer to first AT host context
 */
struct sm_at_host_ctx *sm_at_host_get_urc_ctx(void);

/**
 * @brief Get the current AT host context.
 *
 * Returns the context of the currently executing AT command/work.
 * Falls back to first instance if no context is currently active.
 *
 * The returned context is only valid during the execution of an AT command
 *
 * @return Pointer to current AT host context
 */
struct sm_at_host_ctx *sm_at_host_get_current(void);

/**
 * @brief Get the modem pipe associated with the current AT host context.
 *
 * @return struct modem_pipe* or NULL if no active context or pipe is set.
 */
static inline struct modem_pipe *sm_at_host_get_current_pipe(void)
{
	return sm_at_host_get_pipe(sm_at_host_get_current());
}

/**
 * @brief Get the modem pipe associated with the URC AT host context.
 *
 * @return struct modem_pipe*
 */
static inline struct modem_pipe *sm_at_host_get_urc_pipe(void)
{
	return sm_at_host_get_pipe(sm_at_host_get_urc_ctx());
}

/**
 * @brief Define a wrapper for a Serial Modem custom AT command callback.
 *
 * Wrapper will call the generic wrapper, which will call the actual AT command handler.
 *
 * @param entry The entry name.
 * @param _filter The (partial) AT command on which the callback should trigger.
 * @param _callback The AT command handler callback.
 *
 */
#define SM_AT_CMD_CUSTOM(entry, _filter, _callback)                                               \
	static int _callback(enum at_parser_cmd_type cmd_type, struct at_parser *parser,           \
			     uint32_t);                                                            \
	static int _callback##_wrapper_##entry(char *buf, size_t len, char *at_cmd)                \
	{                                                                                          \
		return sm_at_cb_wrapper(buf, len, at_cmd, _callback);                             \
	}                                                                                          \
	AT_CMD_CUSTOM(entry, _filter, _callback##_wrapper_##entry);

/** @} */

#endif /* SM_AT_HOST_ */
