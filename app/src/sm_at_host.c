/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sm_at_host.h"
#include "sm_at_fota.h"
#include "sm_uart_handler.h"
#include "sm_util.h"
#include "sm_ctrl_pin.h"
#include "sm_at_dfu.h"
#include "sm_ppp.h"
#include "sm_at_socket.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/modem/pipe.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

/* Added for XRESET command */
extern void final_call(void (*func)(void));
extern FUNC_NORETURN void sm_reset(void);

LOG_MODULE_REGISTER(sm_at_host, CONFIG_SM_LOG_LEVEL);

#define HEXDUMP_LIMIT     16
#define URC_RETRY_DELAY   K_MSEC(100)
#define AT_BUF_MIN_SIZE   128
#define AT_BUF_MAX_SIZE   8192
#define AT_XDFU_INIT_CMD  "AT#XDFUINIT"
#define AT_XDFU_WRITE_CMD "AT#XDFUWRITE"
#define AT_XDFU_APPLY_CMD "AT#XDFUAPPLY"
#define AT_XRESET_CMD     "AT#XRESET"

/* Operation mode variables */
enum sm_operation_mode {
	SM_AT_COMMAND_MODE, /* AT command host or bridge */
	SM_DATA_MODE,       /* Raw data sending */
	SM_NULL_MODE,       /* Discard incoming until next command */
	SM_INVALID_MODE     /* Invalid mode, used for error handling */
};

/** @brief Events which can be notified by the AT host. */
enum sm_event {
	SM_EVENT_NONE,           /**< No event. */
	SM_EVENT_URC = 0x01,     /**< URC can be sent. */
	SM_EVENT_AT_MODE = 0x02, /**< Entered AT command mode. */
};

enum sm_debug_print {
	SM_DEBUG_PRINT_NONE,
	SM_DEBUG_PRINT_SHORT,
	SM_DEBUG_PRINT_FULL
};

struct data_mode {
	sm_datamode_handler_t handler;
	int handler_result;
	uint16_t time_limit; /* Time limit for idle period before sending in ms. */
	size_t data_len;     /* Expected data length in data mode. */
};

/* Forward declarations */
static void idle_timer_handler(struct k_timer *timer);
static void idle_work(struct sm_at_host_ctx *ctx);
static void raw_send_scheduled(struct k_work *work);
static void at_pipe_rx_work_fn(struct k_work *work);
static void at_pipe_event_handler(struct modem_pipe *pipe, enum modem_pipe_event event,
				  void *user_data);
static size_t sm_at_receive(struct sm_at_host_ctx *ctx, uint8_t c);
static void sm_at_host_work_fn(struct k_work *work);
static enum sm_operation_mode get_sm_mode(struct sm_at_host_ctx *ctx);
static bool sm_at_ctx_check(struct sm_at_host_ctx *ctx);
static void sm_at_host_event_notify(struct sm_at_host_ctx *ctx, enum sm_event event);
static void check_idle_timer(struct sm_at_host_ctx *ctx, bool reschedule);

/**
 * @brief AT host context structure.
 *
 * Contains all state variables, buffers, and synchronization primitives
 * for a single AT command pipe instance.
 *
 * Lifecycle:
 *
 * @startuml
 * [*] --> Attached: sm_at_host_attach(pipe)
 * Attached: modem_pipe_attach(pipe, null_pipe_handler, NULL)
 * Attached --> Create: MODEM_PIPE_EVENT_OPENED
 * state allocated as "Memory is allocated for context" {
 * Create: sm_at_host_create()
 * Create: modem_pipe_attach(pipe, at_pipe_event_handler, ctx)
 * Create --> Open
 * Open --> Open: MODEM_PIPE_EVENT_RECEIVE_READY\nk_work_submit(...,at_pipe_rx_work_fn)
 * Open --> Closing: MODEM_PIPE_EVENT_CLOSED
 * Open --> Closing: sm_at_host_release(ctx)
 * Closing: sm_at_host_destroy(ctx)
 * state c <<choice>>
 * Closing --> c
 * c --> Destroy: [not last one]
 * c -> Idle: [last one]
 * Idle: modem_pipe_attach(pipe, null_pipe_handler, NULL)
 * Idle -up-> Create: MODEM_PIPE_EVENT_OPENED
 * }
 * Destroy: free(ctx->data_rb_buf)\nfree(ctx->at_buf)\nfree(ctx)
 * Destroy --> [*]
 * @enduml
 *
 */
struct sm_at_host_ctx {
	/* List node for instance management */
	sys_snode_t node;

	/* Pipe instance reference */
	atomic_ptr_t pipe;
	struct k_event pipe_event;

	/* Operation mode variables */
	enum sm_operation_mode at_mode;
	struct data_mode data_mode;

	/* Data buffers */
	uint8_t *at_buf;
	size_t at_buf_size;
	struct ring_buf data_rb;
	uint8_t *data_rb_buf;
	uint8_t quit_str_partial_match;
	struct k_mutex mutex_data;

	/* Work items and timers */
	struct k_work rx_work;
	struct k_work raw_send_scheduled_work;
	struct k_timer data_inactivity_timer;
	struct k_timer idle_timer;

	/* AT command reception state (for cmd_rx_handler) */
	bool inside_quotes;
	size_t at_cmd_len;
	size_t echo_len;
	uint8_t prev_character;

	/* null_handler state */
	size_t null_dropped_count;
	uint8_t null_match_count;

	/* Echo context */
	bool echo_enabled;

	/* Event callback mechanism */
	/* TODO: combine event work and idle_work_list */
	struct {
		sys_slist_t event_cbs;
		atomic_t events;
	} event_ctx;
	sys_slist_t idle_work_list;

	/* Asynchronous poll context for sockets */
	struct async_poll_ctx poll_ctx;
};

enum sm_pipe_event {
	SM_PIPE_EVENT_NONE,
	SM_PIPE_EVENT_OPENED,
	SM_PIPE_EVENT_CLOSED,
};

struct sm_at_host_msg {
	struct sm_at_host_ctx *ctx;
	struct modem_pipe *pipe;
	enum sm_pipe_event pipe_event;
	enum sm_event sm_event;
};
K_MSGQ_DEFINE(sm_at_host_msgq, sizeof(struct sm_at_host_msg), 10, 1);

static sys_slist_t instance_list = SYS_SLIST_STATIC_INIT(instance_list);
static K_WORK_DEFINE(sm_at_host_work, sm_at_host_work_fn);
RING_BUF_DECLARE(urc_buf, CONFIG_SM_URC_BUFFER_SIZE);
uint8_t sm_response_buf[CONFIG_SM_AT_BUF_SIZE + 1];
/* Current executing context (set by entry points) */
static struct sm_at_host_ctx *current_ctx;
static struct k_spinlock sm_at_host_lock;
uint16_t sm_datamode_time_limit;

/**
 * @brief Create a new AT host instance.
 *
 * Allocates and initializes a new AT host context for the given modem pipe.
 * Used by CMUX to create instances for additional DLC channels.
 *
 * @param pipe Modem pipe to attach to the new AT host instance.
 * @return Pointer to the created context, or NULL on failure.
 */
static struct sm_at_host_ctx *sm_at_host_create(struct modem_pipe *pipe);

/**
 * @brief Destroy an AT host instance.
 *
 * Cleans up and frees an AT host instance.
 * Cannot be used while executing an AT command on the context.
 * Use sm_at_host_release() instead.
 *
 * @param ctx AT host context to destroy.
 * @return 0 on success, negative error code on failure.
 */
static int sm_at_host_destroy(struct sm_at_host_ctx *ctx);

static void send_msg(struct sm_at_host_msg msg)
{
	int ret;

	while (k_msgq_put(&sm_at_host_msgq, &msg, K_NO_WAIT) != 0) {
		LOG_ERR("AT host message queue full, purging old data");
		k_msgq_purge(&sm_at_host_msgq);
	}

	ret = k_work_submit_to_queue(&sm_work_q, &sm_at_host_work);
	if (ret < 0) {
		if (ret == -ENODEV) {
			/* sm_work_q is not yet running */
			if (msg.ctx) {
				check_idle_timer(msg.ctx, true);
				return;
			}
		}
		LOG_ERR("Failed to schedule AT host work: %d", ret);
	}
}

static void sm_at_pipe_opened(struct sm_at_host_ctx *ctx, struct modem_pipe *pipe)
{
	struct sm_at_host_msg msg = {
		.ctx = ctx,
		.pipe = pipe,
		.pipe_event = SM_PIPE_EVENT_OPENED,
	};

	/* Don't trigger event if pipe is re-attached to existing context
	 * it was previously attached to
	 * or if the already attached pipe is re-opened.
	 */
	if (ctx) {
		if (atomic_ptr_cas(&ctx->pipe, NULL, pipe)) {
			return;
		}

		if (atomic_ptr_get(&ctx->pipe) == pipe) {
			return;
		}
	}

	/* Other cases need handling from worker queue, we might be inside
	 * spinlock from pipe callback, so cannot modify the pipe itself.
	 */
	send_msg(msg);
}

static void sm_at_pipe_closed(struct sm_at_host_ctx *ctx, struct modem_pipe *pipe)
{
	struct sm_at_host_msg msg = {
		.ctx = ctx,
		.pipe = pipe,
		.pipe_event = SM_PIPE_EVENT_CLOSED,
	};

	if (ctx) {
		if (atomic_ptr_cas(&ctx->pipe, pipe, NULL)) {
			send_msg(msg);
		}
	}
}

/**
 * @brief Set the current AT host context.
 *
 * Called by entry points to set the active context for the current execution.
 *
 * @param ctx Context to set as current (can be NULL to clear)
 */
static inline void sm_at_host_set_current_ctx(struct sm_at_host_ctx *ctx)
{
	current_ctx = ctx;
}

/**
 * @brief Get the current AT host context.
 *
 * Returns the context of the currently executing AT command/work.
 * Falls back to first/URC instance if no context is currently active.
 *
 * @return Pointer to current AT host context
 */
struct sm_at_host_ctx *sm_at_host_get_current(void)
{
	struct sm_at_host_ctx *ctx;

	ctx = current_ctx ? current_ctx : sm_at_host_get_urc_ctx();

	return ctx;
}

struct sm_at_host_ctx *sm_at_host_get_ctx_from(struct modem_pipe *pipe)
{
	struct sm_at_host_ctx *ctx;

	if (!pipe) {
		return NULL;
	}

	K_SPINLOCK(&sm_at_host_lock) {
		SYS_SLIST_FOR_EACH_CONTAINER(&instance_list, ctx, node) {
			if (atomic_ptr_get(&ctx->pipe) == pipe) {
				break;
			}
		}
	}
	return ctx;
}

struct sm_at_host_ctx *sm_at_host_get_urc_ctx(void)
{
	struct sm_at_host_ctx *ctx;

	ctx = SYS_SLIST_PEEK_HEAD_CONTAINER(&instance_list, ctx, node);
	return ctx;
}

struct modem_pipe *sm_at_host_get_pipe(struct sm_at_host_ctx *ctx)
{
	return ctx ? atomic_ptr_get(&ctx->pipe) : NULL;
}

struct async_poll_ctx *sm_at_host_get_async_poll_ctx(struct modem_pipe *pipe)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	return sm_at_ctx_check(ctx) ? &ctx->poll_ctx : NULL;
}

struct modem_pipe *sm_at_host_get_pipe_from_poll_ctx(struct async_poll_ctx *poll_ctx)
{
	if (!poll_ctx) {
		return NULL;
	}

	struct sm_at_host_ctx *ctx = CONTAINER_OF(poll_ctx, struct sm_at_host_ctx, poll_ctx);

	return sm_at_ctx_check(ctx) ? atomic_ptr_get(&ctx->pipe) : NULL;
}

/**
 * @brief Check if ctx pointer is still valid.
 *
 * @param ctx
 * @return true ctx is valid
 * @return false ctx is already destroyed
 */
static bool sm_at_ctx_check(struct sm_at_host_ctx *ctx)
{
	struct sm_at_host_ctx *p;
	bool found = false;

	if (!ctx) {
		return false;
	}

	K_SPINLOCK(&sm_at_host_lock) {
		SYS_SLIST_FOR_EACH_CONTAINER(&instance_list, p, node) {
			if (p == ctx) {
				found = true;
				break;
			}
		}
	}
	return found;
}

static void check_idle_timer(struct sm_at_host_ctx *ctx, bool reschedule)
{
	if (reschedule || k_timer_remaining_ticks(&ctx->idle_timer) == 0) {
		k_timeout_t delay = ctx->echo_enabled && (reschedule || ctx->at_cmd_len != 0)
					    ? K_MSEC(CONFIG_SM_URC_DELAY_WITH_INCOMPLETE_ECHO_MS)
					    : URC_RETRY_DELAY;

		k_timer_start(&ctx->idle_timer, delay, K_NO_WAIT);
	}
}

/* Process received data from pipe */
static void at_pipe_rx_work_fn(struct k_work *work)
{
	struct sm_at_host_ctx *ctx = CONTAINER_OF(work, struct sm_at_host_ctx, rx_work);
	int ret;
	uint8_t rx_buf;

	if (!sm_at_ctx_check(ctx)) {
		LOG_ERR("AT pipe RX work: context destroyed");
		return;
	}

	struct modem_pipe *pipe = atomic_ptr_get(&ctx->pipe);

	if (!pipe) {
		LOG_WRN("AT pipe RX work: no pipe assigned (ctx: %p)", (void *)ctx);
		return;
	}
	/* Set as current context */
	sm_at_host_set_current_ctx(ctx);

	/* Read data from ctx->pipe */
	do {
		ret = modem_pipe_receive(pipe, &rx_buf, sizeof(rx_buf));
		if (ret < 0) {
			LOG_ERR("Pipe receive failed: %d (ctx %p, pipe %p)", ret, (void *)ctx,
				(void *)pipe);
			break;
		}

		if (ret > 0) {
			/* Process received AT data */
			sm_at_receive(ctx, rx_buf);
		}
	} while (ret > 0 && atomic_ptr_get(&ctx->pipe) == pipe);

	/* Clear current context */
	sm_at_host_set_current_ctx(NULL);
}

/* Pipe event handler for AT communication */
static void at_pipe_event_handler(struct modem_pipe *pipe, enum modem_pipe_event event,
				  void *user_data)
{
	int ret;
	struct sm_at_host_ctx *ctx = (struct sm_at_host_ctx *)user_data;

	if (!ctx || !sm_at_ctx_check(ctx)) {
		LOG_ERR("Invalid context in pipe event handler");
		return;
	}

	k_event_post(&ctx->pipe_event, BIT(event));

	switch (event) {
	case MODEM_PIPE_EVENT_RECEIVE_READY:
		/* Ensure pipe have not changed */
		if (atomic_ptr_get(&ctx->pipe) != pipe) {
			LOG_ERR("Received data on pipe %p not assigned to ctx %p", (void *)pipe,
				(void *)ctx);
			break;
		}
		ret = k_work_submit_to_queue(&sm_work_q, &ctx->rx_work);
		if (ret < 0) {
			LOG_ERR("Failed to submit RX work: %d", ret);
		}
		break;

	case MODEM_PIPE_EVENT_CLOSED:
		sm_at_pipe_closed(ctx, pipe);
		break;

	case MODEM_PIPE_EVENT_OPENED:
		sm_at_pipe_opened(ctx, pipe);
		break;
	default:
		break;
	}
}

static int sm_at_host_pipe_tx_blocking(struct sm_at_host_ctx *ctx, const uint8_t *buf, size_t size)
{
	struct modem_pipe *pipe = ctx ? atomic_ptr_get(&ctx->pipe) : NULL;
	int ret;

	if (!pipe) {
		LOG_WRN("No pipe assigned for transmission (ctx: %p)", (void *)ctx);
		return -EINVAL;
	}

	for (size_t len = size; len > 0;) {
		ret = modem_pipe_transmit(pipe, buf, len);
		if (ret < 0) {
			LOG_ERR("Pipe transmit failed: %d (ctx %p, pipe %p)", ret, (void *)ctx,
				(void *)pipe);
			return ret;
		} else if (ret == 0) {
			/* No data transmitted, wait for transmit idle event */
			if (k_event_wait(&ctx->pipe_event, BIT(MODEM_PIPE_EVENT_TRANSMIT_IDLE),
					 true, K_SECONDS(1)) == 0) {
				LOG_ERR("Failed to wait for transmit idle event");
				return -EIO;
			}
		} else {
			buf += ret;
			len -= ret;
		}
	}
	return size;
}

static void null_pipe_handler(struct modem_pipe *pipe, enum modem_pipe_event event, void *user_data)
{
	switch (event) {
	case MODEM_PIPE_EVENT_OPENED:
		LOG_DBG("Null pipe(%p) handler event: %d", (void *)pipe, event);
		sm_at_pipe_opened(NULL, pipe);
		break;
	default:
		break;
	}
}

int sm_at_host_set_pipe(struct sm_at_host_ctx *ctx, struct modem_pipe *pipe)
{
	if (!ctx || !pipe) {
		LOG_ERR("sm_at_host_set_pipe(%p, %p) invalid", (void *)ctx, (void *)pipe);
		return -EINVAL;
	}

	if (!sm_at_ctx_check(ctx)) {
		LOG_ERR("sm_at_host_set_pipe: context destroyed");
		return -EINVAL;
	}

	struct modem_pipe *old_pipe = atomic_ptr_set(&ctx->pipe, pipe);

	LOG_DBG("Setting AT host pipe: %p (old: %p, ctx: %p)", (void *)pipe, (void *)old_pipe,
		(void *)ctx);

	/* Release old pipe if attached */
	if (old_pipe) {
		modem_pipe_attach(old_pipe, null_pipe_handler, NULL);
	}

	/* Release old CTX if the pipe had one */
	struct sm_at_host_ctx *old_ctx = sm_at_host_get_ctx_from(pipe);

	if (old_ctx && old_ctx != ctx && atomic_ptr_cas(&old_ctx->pipe, pipe, (void *)0xdeadbeef)) {
		LOG_DBG("Pipe %p already attached to another context %p, destroying old context",
			(void *)pipe, (void *)old_ctx);
		sm_at_host_destroy(old_ctx);
	}

	/* Attach to new pipe */
	modem_pipe_attach(pipe, at_pipe_event_handler, ctx);
	return 0;
}

void sm_at_host_release(struct sm_at_host_ctx *ctx)
{
	struct modem_pipe *pipe;

	if (!sm_at_ctx_check(ctx)) {
		return;
	}

	pipe = atomic_ptr_get(&ctx->pipe);
	modem_pipe_release(pipe);
	sm_at_pipe_closed(ctx, pipe);

	LOG_DBG("Releasing AT host pipe: %p (ctx: %p)", (void *)pipe, (void *)ctx);
}

void sm_at_host_attach(struct modem_pipe *pipe)
{
	modem_pipe_attach(pipe, null_pipe_handler, NULL);
	if (sm_pipe_is_open(pipe)) {
		sm_at_pipe_opened(NULL, pipe);
	}
}

static enum sm_operation_mode get_sm_mode(struct sm_at_host_ctx *ctx)
{
	if (!ctx || !sm_at_ctx_check(ctx)) {
		return SM_INVALID_MODE;
	}
	return ctx->at_mode;
}

/* Lock mutex_mode, before calling. */
static bool set_sm_mode(struct sm_at_host_ctx *ctx, enum sm_operation_mode mode)
{
	bool ret = false;

	if (ctx->at_mode == SM_AT_COMMAND_MODE) {
		if (mode == SM_DATA_MODE) {
			if (ctx->data_rb_buf == NULL) {
				LOG_DBG("Allocating datamode buffer of size %d",
					CONFIG_SM_DATAMODE_BUF_SIZE);
				ctx->data_rb_buf = malloc(CONFIG_SM_DATAMODE_BUF_SIZE);
				if (ctx->data_rb_buf == NULL) {
					LOG_ERR("Failed to allocate datamode buffer");
					return false;
				}
				ring_buf_init(&ctx->data_rb, CONFIG_SM_DATAMODE_BUF_SIZE,
					      ctx->data_rb_buf);
			}
			ret = true;
		}
	} else if (ctx->at_mode == SM_DATA_MODE) {
		if (mode == SM_NULL_MODE || mode == SM_AT_COMMAND_MODE) {
			ret = true;
		}
	} else if (ctx->at_mode == SM_NULL_MODE) {
		if (mode == SM_AT_COMMAND_MODE || mode == SM_NULL_MODE) {
			ret = true;
		}
	}

	if (ret) {
		LOG_DBG("SM mode changed: %d -> %d", ctx->at_mode, mode);
		ctx->at_mode = mode;
	} else {
		LOG_ERR("Failed to change SM mode: %d -> %d", ctx->at_mode, mode);
	}

	return ret;
}

static void sm_at_host_event_notify(struct sm_at_host_ctx *ctx, enum sm_event event)
{
	atomic_or(&ctx->event_ctx.events, event);
	send_msg((struct sm_at_host_msg){
		.ctx = ctx,
		.sm_event = event,
	});
}

static bool exit_datamode(void)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();
	bool ret = false;

	if (set_sm_mode(ctx, SM_AT_COMMAND_MODE)) {
		if (ctx->data_mode.handler) {
			(void)ctx->data_mode.handler(DATAMODE_EXIT, NULL, 0,
						     SM_DATAMODE_FLAGS_NONE);
		}
		ctx->data_mode.handler = NULL;
		ctx->data_mode.data_len = 0;
		ctx->quit_str_partial_match = 0;

		k_mutex_lock(&ctx->mutex_data, K_FOREVER);
		ring_buf_reset(&ctx->data_rb);
		k_mutex_unlock(&ctx->mutex_data);

		if (ctx->data_mode.handler_result) {
			LOG_ERR("Datamode handler error: %d", ctx->data_mode.handler_result);
			ctx->data_mode.handler_result = -1;
		}
		rsp_send("\r\n#XDATAMODE: %d\r\n", ctx->data_mode.handler_result);
		ctx->data_mode.handler_result = 0;

		sm_at_host_event_notify(ctx, SM_EVENT_AT_MODE);

		LOG_INF("Exit data mode");
		ret = true;
	}

	return ret;
}

/* Lock mutex_data, before calling. */
static void raw_send(uint8_t flags)
{
	int claim;
	int ret;
	bool retry = true;
	uint8_t *data = NULL;
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	do {
		/* ring_buf_get_claim() might not return full size.
		 * If we have more data in the buffer, set the MORE_DATA flag.
		 */
		uint8_t send_flags = flags;

		ret = ring_buf_size_get(&ctx->data_rb);
		claim = ring_buf_get_claim(&ctx->data_rb, &data, CONFIG_SM_DATAMODE_BUF_SIZE);
		if (claim <= 0) {
			break;
		}
		if (claim != ret) {
			send_flags |= SM_DATAMODE_FLAGS_MORE_DATA;
		}

		/* Send received data onward. */
		LOG_HEXDUMP_DBG(data, MIN(claim, HEXDUMP_LIMIT), "RX");
		LOG_INF("Send: %d bytes, Data: %p", claim, (void *)data);

		if (ctx->data_mode.handler) {
			ret = ctx->data_mode.handler(DATAMODE_SEND, data, claim, send_flags);
			if (ret < 0 || (ret == 0 && !retry)) {
				/* Exit data mode on send failure. Further data is dropped. */
				LOG_ERR("Send failed: %d, Dropped: %d bytes", ret, claim);
				exit_datamode_handler(ret ? ret : -EAGAIN);
				ret = claim;
			} else if (ret == 0 && retry) {
				retry = false;
				LOG_WRN("Send returned 0, retrying once");
			} else {
				LOG_DBG("Sent %d bytes", ret);
				retry = true;
			}
			ring_buf_get_finish(&ctx->data_rb, ret);
		} else {
			LOG_ERR("No handler. Dropped %d bytes", claim);
			ring_buf_get_finish(&ctx->data_rb, claim);
		}

	} while (true);
}

/* Lock mutex_data, before calling. */
static void write_data_buf(const uint8_t *buf, size_t len)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();
	size_t ret;
	size_t index = 0;

	/* Reset the ring buffer so that UDP packets have enough continuous space. */
	if (ring_buf_is_empty(&ctx->data_rb)) {
		ring_buf_reset(&ctx->data_rb);
	}

	while (index < len) {
		ret = ring_buf_put(&ctx->data_rb, buf + index, len - index);
		if (ret) {
			index += ret;
		} else {
			/* Buffer is full. Send data.*/
			raw_send(SM_DATAMODE_FLAGS_MORE_DATA);
		}
	}
}

static void raw_send_scheduled(struct k_work *work)
{
	struct sm_at_host_ctx *ctx =
		CONTAINER_OF(work, struct sm_at_host_ctx, raw_send_scheduled_work);

	/* Set as current context */
	sm_at_host_set_current_ctx(ctx);

	k_mutex_lock(&ctx->mutex_data, K_FOREVER);

	/* Interpret partial quit_str as data, if we send due to timeout. */
	if (ctx->quit_str_partial_match > 0) {
		write_data_buf(CONFIG_SM_DATAMODE_TERMINATOR, ctx->quit_str_partial_match);
		ctx->quit_str_partial_match = 0;
	}

	raw_send(SM_DATAMODE_FLAGS_NONE);

	k_mutex_unlock(&ctx->mutex_data);

	/* Clear current context */
	sm_at_host_set_current_ctx(NULL);
}

static void inactivity_timer_handler(struct k_timer *timer)
{
	struct sm_at_host_ctx *ctx =
		CONTAINER_OF(timer, struct sm_at_host_ctx, data_inactivity_timer);

	LOG_DBG("Time limit reached");
	if (!ring_buf_is_empty(&ctx->data_rb)) {
		k_work_submit_to_queue(&sm_work_q, &ctx->raw_send_scheduled_work);
	} else {
		LOG_DBG("data buffer empty");
	}
}

/* Search for quit_str and send data prior to that. Tracks quit_str over several calls. */
static size_t raw_rx_handler(struct sm_at_host_ctx *ctx, uint8_t c)
{
	k_mutex_lock(&ctx->mutex_data, K_FOREVER);

	const char *const quit_str = CONFIG_SM_DATAMODE_TERMINATOR;
	bool quit_str_match = false;
	uint8_t quit_str_match_count = ctx->quit_str_partial_match;
	uint8_t prev_quit_str_match_count = ctx->quit_str_partial_match;
	uint8_t prev_quit_str_match_count_original = ctx->quit_str_partial_match;

	/* If <data_len> is set in datamode, skip searching for quit_str. Just send data until
	 * length is reached.
	 */
	if (ctx->data_mode.data_len > 0) {
		write_data_buf(&c, 1);
		ctx->data_mode.data_len--;
		if (ctx->data_mode.data_len == 0) {
			raw_send(SM_DATAMODE_FLAGS_NONE);
			(void)exit_datamode();
		}
	} else {
		/* Find quit_str or partial match at the end of the buffer. */
		if (c == quit_str[quit_str_match_count]) {
			quit_str_match_count++;
			if (quit_str_match_count == strlen(quit_str)) {
				quit_str_match = true;
			}
		} else if (quit_str_match_count > 0) {
			/* Check if we match a beginning of a new quit_str.
			 * We either match the first character, or in the edge case of
			 * quit_str starting with multiple same characters, e.g. "aaabbb",
			 * we match all but the current character (with input aaaa).
			 */
			for (int i = 0; i < quit_str_match_count; i++) {
				if (c != quit_str[i]) {
					quit_str_match_count = i;
					break;
				}
			}
			if (quit_str_match_count == 0) {
				/* No match.
				 * Previous partial quit_str is data.
				 */
				prev_quit_str_match_count = 0;
			} else if (prev_quit_str_match_count > 0) {
				/* Partial match.
				 * Part of the previous partial quit_str is data.
				 */
				prev_quit_str_match_count--;
			}
		}

		/* Write data which was previously interpreted as a possible partial quit_str. */
		write_data_buf(quit_str,
			       prev_quit_str_match_count_original - prev_quit_str_match_count);

		/* Write data from buf until the start of the possible (partial) quit_str. */
		write_data_buf(&c, 1 - (quit_str_match_count - prev_quit_str_match_count));

		if (quit_str_match) {
			raw_send(SM_DATAMODE_FLAGS_NONE);
			(void)exit_datamode();
			ctx->quit_str_partial_match = 0;
		} else {
			ctx->quit_str_partial_match = quit_str_match_count;
		}
	}
	k_mutex_unlock(&ctx->mutex_data);

	return 1;
}

/*
 * Check AT command grammar based on below.
 *  AT<NULL>
 *  ATE0<NULL>
 *  ATE1<NULL>
 *  AT<separator><body><NULL>
 *  AT<separator><body>=<NULL>
 *  AT<separator><body>?<NULL>
 *  AT<separator><body>=?<NULL>
 *  AT<separator><body>=<parameters><NULL>
 * In which
 * <separator>: +, %, #
 * <body>: alphanumeric char or '_' only, size > 0
 * <parameters>: arbitrary, size > 0
 */
static int cmd_grammar_check(const char *cmd, size_t length)
{
	const char *body;

	/* check AT (if not, no check) */
	if (length < 2 || toupper((int)cmd[0]) != 'A' || toupper((int)cmd[1]) != 'T') {
		return -EINVAL;
	}

	/* check AT<NULL> */
	cmd += 2;
	if (*cmd == '\0') {
		return 0;
	}

	/* Check ATE0 and ATE1 */
	if (toupper(*cmd) == 'E' && (*(cmd + 1) == '0' || *(cmd + 1) == '1') &&
	    *(cmd + 2) == '\0') {
		return 0;
	}

	/* check AT<separator> */
	if ((*cmd != '+') && (*cmd != '%') && (*cmd != '#')) {
		return -EINVAL;
	}

	/* check AT<separator><body> */
	cmd += 1;
	body = cmd;
	while (true) {
		/* check body is alphanumeric or '_' */
		if (!isalpha((int)*cmd) && !isdigit((int)*cmd) && *cmd != '_') {
			break;
		}
		cmd++;
	}

	/* check body size > 0 */
	if (cmd == body) {
		return -EINVAL;
	}

	/* check AT<separator><body><NULL> */
	if (*cmd == '\0') {
		return 0;
	}

	/* check AT<separator><body>= or check AT<separator><body>? */
	if (*cmd != '=' && *cmd != '?') {
		return -EINVAL;
	}

	/* check AT<separator><body>?<NULL> */
	if (*cmd == '?') {
		cmd += 1;
		if (*cmd == '\0') {
			return 0;
		} else {
			return -EINVAL;
		}
	}

	/* check AT<separator><body>=<NULL> */
	cmd += 1;
	if (*cmd == '\0') {
		return 0;
	}

	/* check AT<separator><body>=?<NULL> */
	if (*cmd == '?') {
		cmd += 1;
		if (*cmd == '\0') {
			return 0;
		} else {
			return -EINVAL;
		}
	}

	/* no need to check AT<separator><body>=<parameters><NULL> */
	return 0;
}

static char *strrstr(const char *str1, const char *str2)
{
	size_t len1;
	size_t len2;

	if (str1 == NULL || str2 == NULL) {
		return NULL;
	}

	len1 = strlen(str1);
	len2 = strlen(str2);

	if (len2 > len1 || len1 == 0 || len2 == 0) {
		return NULL;
	}

	for (int i = len1 - len2; i >= 0; i--) {
		if (strncmp(str1 + i, str2, len2) == 0) {
			return (char *)str1 + i;
		}
	}

	return NULL;
}

static void format_final_result(char *buf, size_t buf_len, size_t buf_max_len)
{
	static const char ok_str[] = "OK\r\n";
	static const char error_str[] = "ERROR\r\n";
	static const char cme_error_str[] = "+CME ERROR:";
	static const char cms_error_str[] = "+CMS ERROR:";
	char *result = NULL;

	result = strrstr(buf, ok_str);
	if (result == NULL) {
		result = strrstr(buf, error_str);
	}

	if (result == NULL) {
		result = strrstr(buf, cme_error_str);
	}

	if (result == NULL) {
		result = strrstr(buf, cms_error_str);
	}

	if (result == NULL) {
		LOG_WRN("Final result not found");
		return;
	}

	/* insert CRLF before final result if there is information response before it */
	if (result != buf + strlen(CRLF_STR)) {
		if (buf_len + strlen(CRLF_STR) < buf_max_len) {
			memmove((void *)(result + strlen(CRLF_STR)), (void *)result,
				strlen(result));
			result[0] = CR;
			result[1] = LF;
			buf_len += strlen(CRLF_STR);
			buf[buf_len] = '\0';
		} else {
			LOG_WRN("No room to insert CRLF");
		}
	}
}

static int sm_at_send_internal(struct sm_at_host_ctx *ctx, const uint8_t *data, size_t len,
			       bool urc, enum sm_debug_print print_debug)
{
	int ret;

	if (k_is_in_isr()) {
		LOG_ERR("FIXME: Attempt to send AT response (of size %u) in ISR.", len);
		return -EINTR;
	}

	if (urc) {
		ret = ring_buf_put(&urc_buf, data, len);
		if (ret < len) {
			LOG_ERR("URC buffer full, dropped %d bytes", len - ret);
		}
		if (!is_idle(ctx)) {
			LOG_DBG("AT command in progress, delaying URC processing");
			check_idle_timer(ctx, false);
		} else {
			sm_at_host_event_notify(ctx, SM_EVENT_URC);
		}
		return 0;
	}

	ret = sm_at_host_pipe_tx_blocking(ctx, data, len);
	if (ret < 0) {
		return ret;
	}

	if (print_debug == SM_DEBUG_PRINT_FULL) {
		LOG_HEXDUMP_DBG(data, len, "TX");
	} else if (print_debug == SM_DEBUG_PRINT_SHORT) {
		LOG_HEXDUMP_DBG(data, MIN(HEXDUMP_LIMIT, len), "TX");
	}

	return (ret == len) ? 0 : -EIO;
}

static void handle_bootloader_at_cmd(uint8_t *buf, size_t buf_size, char *at_cmd)
{
	int err;

	if (strncasecmp(at_cmd, AT_XDFU_INIT_CMD, sizeof(AT_XDFU_INIT_CMD) - 1) == 0) {
		err = sm_at_handle_xdfu_init(buf + strlen(CRLF_STR), buf_size - strlen(CRLF_STR),
					     at_cmd);
		if (err) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
		} else {
			rsp_send_ok();
		}
	} else if (strncasecmp(at_cmd, AT_XDFU_WRITE_CMD, sizeof(AT_XDFU_WRITE_CMD) - 1) == 0) {
		err = sm_at_handle_xdfu_write(buf + strlen(CRLF_STR), buf_size - strlen(CRLF_STR),
					      at_cmd);
		if (err) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
		} else {
			rsp_send_ok();
		}
	} else if (strncasecmp(at_cmd, AT_XDFU_APPLY_CMD, sizeof(AT_XDFU_APPLY_CMD) - 1) == 0) {
		err = sm_at_handle_xdfu_apply(buf + strlen(CRLF_STR), buf_size - strlen(CRLF_STR),
					      at_cmd);
		if (err) {
			LOG_ERR("AT command failed: %d", err);
			rsp_send_error();
		} else {
			rsp_send_ok();
		}
	} else if (strncasecmp(at_cmd, AT_XRESET_CMD, sizeof(AT_XRESET_CMD) - 1) == 0) {
		LOG_INF("Rebooting device via %s command", AT_XRESET_CMD);
		LOG_PANIC();
		final_call(sm_reset);
	} else {
		LOG_ERR("AT command not supported in bootloader mode: %s", at_cmd);
		rsp_send_error();
	}
}

static void cmd_send(struct sm_at_host_ctx *ctx, uint8_t *buf, size_t cmd_length)
{
	int err;
	size_t offset = 0;
	char *at_cmd = buf;

	LOG_HEXDUMP_DBG(buf, cmd_length, "RX");

	/* UART can send additional characters when the device is powered on.
	 * We ignore everything before the start of the AT-command.
	 */
	while (offset + 1 < cmd_length) {
		if (toupper(buf[offset]) == 'A' && toupper(buf[offset + 1]) == 'T') {
			at_cmd += offset;
			cmd_length -= offset;
			break;
		}
		offset++;
	}

	err = cmd_grammar_check(at_cmd, cmd_length);
	if (err < 0) {
		LOG_ERR("AT command syntax invalid: %s", at_cmd);
		if (err == -ENOENT) {
			/* Not an AT command, ignore silently. */
			return;
		}
		rsp_send_error();
		return;
	}

	/* If bootloader mode is enabled, handle custom AT commands. */
	if (sm_bootloader_mode_enabled) {
		handle_bootloader_at_cmd(sm_response_buf, sizeof(sm_response_buf), at_cmd);
		return;
	}

	/* Send to modem.
	 * Reserve space for CRLF in the response buffer.
	 */
	err = nrf_modem_at_cmd(sm_response_buf + strlen(CRLF_STR),
			       sizeof(sm_response_buf) - strlen(CRLF_STR), "%s", at_cmd);
	if (err == -SILENT_AT_COMMAND_RET) {
		return;
	} else if (err < 0) {
		LOG_ERR("AT command failed: %d", err);
		rsp_send_error();
		return;
	} else if (err > 0) {
		LOG_ERR("AT command error (%d), type: %d: value: %d", err,
			nrf_modem_at_err_type(err), nrf_modem_at_err(err));
	}

	/** Format as TS 27.007 command V1 with verbose response format,
	 *  based on current return of API nrf_modem_at_cmd() and MFWv1.3.x
	 */
	sm_response_buf[0] = CR;
	sm_response_buf[1] = LF;
	if (strlen(sm_response_buf) > strlen(CRLF_STR)) {
		format_final_result(sm_response_buf, strlen(sm_response_buf),
				    sizeof(sm_response_buf));
		err = sm_at_send_internal(ctx, sm_response_buf, strlen(sm_response_buf), false,
					  SM_DEBUG_PRINT_FULL);
		if (err) {
			LOG_ERR("AT command response failed: %d", err);
		}
	}
}

static size_t cmd_rx_handler(struct sm_at_host_ctx *ctx, uint8_t c)
{
	bool send = false;

	check_idle_timer(ctx, true);

	/* Don't buffer anything until "AT" is received */
	if ((ctx->at_cmd_len == 0 && toupper(c) != 'A') ||
	    (ctx->at_cmd_len == 1 && toupper(c) != 'T')) {
		ctx->inside_quotes = false;
		ctx->at_cmd_len = 0;
		goto handle_echo;
	}
	/* Ensure enough space in the AT command buffer */
	if (ctx->at_cmd_len >= ctx->at_buf_size - 2) {
		size_t new_size = ctx->at_buf_size * 2;

		if (new_size > AT_BUF_MAX_SIZE) {
			LOG_ERR("AT command buffer overflow, max size reached");
			rsp_send_error();
			goto cmd_finnish_or_fail;
		}
		uint8_t *new_buf = realloc(ctx->at_buf, new_size);

		if (!new_buf) {
			LOG_ERR("Failed to expand AT command buffer");
			rsp_send_error();
			goto cmd_finnish_or_fail;
		}
		ctx->at_buf = new_buf;
		ctx->at_buf_size = new_size;
		LOG_DBG("Expanded AT command buffer to size %zu", new_size);
	}

	/* Handle control characters */
	switch (c) {
	case 0x08: /* Backspace. */
		   /* Fall through. */
	case 0x7F: /* DEL character */
		if (ctx->at_cmd_len == 0) {
			break;
		}

		ctx->at_cmd_len--;
		/* If the removed character was a quote, need to toggle the flag. */
		if (ctx->prev_character == '"') {
			ctx->inside_quotes = !ctx->inside_quotes;
		}
		if (ctx->at_cmd_len > 0) {
			ctx->prev_character = ctx->at_buf[ctx->at_cmd_len - 1];
		} else {
			ctx->prev_character = '\0';
		}
		break;
	default:
		break;
	}

	/* Handle termination characters, if outside quotes. */
	if (!ctx->inside_quotes) {
		switch (c) {
		case '\r':
			if (IS_ENABLED(CONFIG_SM_CR_TERMINATION)) {
				send = true;
			}
			break;
		case '\n':
			if (IS_ENABLED(CONFIG_SM_LF_TERMINATION)) {
				send = true;
			} else if (IS_ENABLED(CONFIG_SM_CR_LF_TERMINATION)) {
				if (ctx->at_cmd_len > 0 && ctx->prev_character == '\r') {
					ctx->at_cmd_len--; /* trim the CR char */
					send = true;
				}
			}
			break;
		}
	}

	if (send == false) {
		/* Write character to AT buffer, leave space for null */
		if (ctx->at_cmd_len < ctx->at_buf_size - 1) {
			ctx->at_buf[ctx->at_cmd_len] = c;
		}
		ctx->at_cmd_len++;

		/* Handle special written character */
		if (c == '"') {
			ctx->inside_quotes = !ctx->inside_quotes;
		}

		ctx->prev_character = c;
	}
handle_echo:
	if (ctx->echo_enabled) {
		const uint8_t terminator_len = IS_ENABLED(CONFIG_SM_CR_LF_TERMINATION) ? 2 : 1;
		bool truncate = false;
		size_t echo_fragment_len = 1;

		/* Check if echo should be truncated. */
		if (ctx->echo_len + echo_fragment_len + (send ? 0 : terminator_len) >
		    CONFIG_SM_AT_ECHO_MAX_LEN) {
			truncate = true;
			echo_fragment_len =
				CONFIG_SM_AT_ECHO_MAX_LEN - ctx->echo_len - terminator_len;
		}

		(void)sm_at_send_internal(ctx, (uint8_t *)&c, echo_fragment_len, false,
					  SM_DEBUG_PRINT_NONE);
		ctx->echo_len += echo_fragment_len;

		/* Send truncated termination characters.*/
		if (send && truncate) {
			if (IS_ENABLED(CONFIG_SM_CR_TERMINATION)) {
				(void)sm_at_send_internal(ctx, (uint8_t *)"\r", 1, false,
							  SM_DEBUG_PRINT_NONE);
			} else if (IS_ENABLED(CONFIG_SM_LF_TERMINATION)) {
				(void)sm_at_send_internal(ctx, (uint8_t *)"\n", 1, false,
							  SM_DEBUG_PRINT_NONE);
			} else {
				(void)sm_at_send_internal(ctx, (uint8_t *)"\r\n", 2, false,
							  SM_DEBUG_PRINT_NONE);
			}
		}
	}

	if (send) {
		if (ctx->at_cmd_len > ctx->at_buf_size - 1) {
			LOG_ERR("AT command buffer overflow, %d dropped", ctx->at_cmd_len);
			rsp_send_error();
		} else if (ctx->at_cmd_len > 0) {
			ctx->at_buf[ctx->at_cmd_len] = '\0';
			cmd_send(ctx, ctx->at_buf, ctx->at_cmd_len);
		} else {
			/* Ignore 0 size command. */
		}
cmd_finnish_or_fail:
		ctx->inside_quotes = false;
		ctx->at_cmd_len = 0;
		ctx->echo_len = 0;
		/* Release extra AT buffer */
		if (ctx->at_buf_size > AT_BUF_MIN_SIZE) {
			uint8_t *new_buf = realloc(ctx->at_buf, AT_BUF_MIN_SIZE);

			if (new_buf) {
				ctx->at_buf = new_buf;
				ctx->at_buf_size = AT_BUF_MIN_SIZE;
				LOG_DBG("Released AT command buffer to size %zu", AT_BUF_MIN_SIZE);
			}
		}
		k_timer_stop(&ctx->idle_timer);
		sm_at_host_event_notify(ctx, SM_EVENT_URC);
	}

	return 1;
}

/* Search for quit_str and exit datamode when one is found. */
static size_t null_handler(struct sm_at_host_ctx *ctx, uint8_t c)
{
	const char *const quit_str = CONFIG_SM_DATAMODE_TERMINATOR;
	bool match = false;

	if (ctx->null_dropped_count == 0) {
		LOG_WRN("Data pipe broken. Dropping data until data mode is terminated.");
	}

	if (c == quit_str[ctx->null_match_count]) {
		ctx->null_match_count++;
		if (ctx->null_match_count == strlen(quit_str)) {
			match = true;
		}
	} else {
		ctx->null_match_count = 0;
	}
	ctx->null_dropped_count++;

	if (match) {
		ctx->null_dropped_count -= strlen(quit_str);
		ctx->null_dropped_count += ring_buf_size_get(&ctx->data_rb);
		LOG_WRN("Terminating data mode. Dropped %d bytes", ctx->null_dropped_count);
		(void)exit_datamode();

		ctx->null_match_count = 0;
		ctx->null_dropped_count = 0;
	}

	return 1;
}

static size_t sm_at_receive(struct sm_at_host_ctx *ctx, uint8_t c)
{
	size_t ret = 0;

	k_timer_stop(&ctx->data_inactivity_timer);

	switch (get_sm_mode(ctx)) {
	case SM_AT_COMMAND_MODE:
		ret = cmd_rx_handler(ctx, c);
		break;
	case SM_DATA_MODE:
		ret = raw_rx_handler(ctx, c);
		break;
	case SM_NULL_MODE:
		ret = null_handler(ctx, c);
		break;
	default:
		break;
	}

	/* start inactivity timer in datamode */
	if (get_sm_mode(ctx) == SM_DATA_MODE) {
		k_timer_start(&ctx->data_inactivity_timer, K_MSEC(ctx->data_mode.time_limit), K_NO_WAIT);
	}

	return ret;
}

AT_MONITOR(at_notify, ANY, notification_handler);

static void notification_handler(const char *notification)
{
#if defined(CONFIG_SM_PPP)
	if (!sm_fwd_cgev_notifs && !strncmp(notification, "+CGEV: ", strlen("+CGEV: "))) {
		/* CGEV notifications are silenced. Do not forward them. */
		return;
	}
#endif

	urc_send(CRLF_STR "%s", notification);
}

void rsp_send_ok(void)
{
	rsp_send(OK_STR);
}

void rsp_send_error(void)
{
	rsp_send(ERROR_STR);
}

static void rsp_send_internal(struct sm_at_host_ctx *ctx, bool urc, const char *fmt, va_list arg_ptr)
{
	static K_MUTEX_DEFINE(mutex_rsp_buf);
	static char rsp_buf[SM_AT_MAX_RSP_LEN];
	int rsp_len;

	k_mutex_lock(&mutex_rsp_buf, K_FOREVER);

	rsp_len = vsnprintf(rsp_buf, sizeof(rsp_buf), fmt, arg_ptr);
	rsp_len = MIN(rsp_len, sizeof(rsp_buf) - 1);

	sm_at_send_internal(ctx, rsp_buf, rsp_len, urc, SM_DEBUG_PRINT_FULL);

	k_mutex_unlock(&mutex_rsp_buf);
}

void rsp_send_to(struct modem_pipe *pipe, const char *fmt, ...)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	rsp_send_internal(ctx, false, fmt, arg_ptr);
	va_end(arg_ptr);
}


void rsp_send(const char *fmt, ...)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	rsp_send_internal(ctx, false, fmt, arg_ptr);
	va_end(arg_ptr);
}

void urc_send(const char *fmt, ...)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_urc_ctx();
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	rsp_send_internal(ctx, true, fmt, arg_ptr);
	va_end(arg_ptr);
}

void data_send(struct modem_pipe *pipe, const uint8_t *data, size_t len)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	sm_at_send_internal(ctx, data, len, false, SM_DEBUG_PRINT_SHORT);
}

static uint16_t get_min_data_mode_idle_timeout_ms(void)
{
	uint16_t min_time_limit;

	if (sm_uart_baudrate == 0) {
		LOG_ERR("Baudrate not set");
		return 1000;
	}

	/* Calculate minimum time limit based on UART RX buffer size and baudrate.
	 * Each byte consists of 8 data bits, 1 start bit and 1 stop bit.
	 * We will wait this amount of time before emptying data mode buffer.
	 * This ensures that a possible datagram is not split into multiple sends due to
	 * inactivity timeout.
	 */
	min_time_limit =
		(uint32_t)CONFIG_SM_UART_RX_BUF_SIZE * (8 + 1 + 1) * 1000 / sm_uart_baudrate;
	min_time_limit += UART_RX_MARGIN_MS;

	return min_time_limit;
}

int enter_datamode(sm_datamode_handler_t handler, size_t data_len)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	if (handler == NULL || ctx->data_mode.handler != NULL ||
	    set_sm_mode(ctx, SM_DATA_MODE) == false) {
		LOG_ERR("Failed to enter data mode");
		return -EINVAL;
	}

	k_mutex_lock(&ctx->mutex_data, K_FOREVER);
	ring_buf_reset(&ctx->data_rb);
	k_mutex_unlock(&ctx->mutex_data);

	ctx->data_mode.handler = handler;
	ctx->data_mode.data_len = data_len;
	if (ctx->data_mode.time_limit == 0) {
		ctx->data_mode.time_limit = get_min_data_mode_idle_timeout_ms();
	}
	LOG_INF("Enter data mode");

	return 0;
}

bool in_datamode_ctx(struct sm_at_host_ctx *ctx)
{
	return (get_sm_mode(ctx) == SM_DATA_MODE);
}

bool in_datamode_pipe(struct modem_pipe *pipe)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	return in_datamode_ctx(ctx);
}

bool in_at_mode_ctx(struct sm_at_host_ctx *ctx)
{
	return (get_sm_mode(ctx) == SM_AT_COMMAND_MODE);
}

bool in_at_mode_pipe(struct modem_pipe *pipe)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	return in_at_mode_ctx(ctx);
}

bool is_idle_ctx(struct sm_at_host_ctx *ctx)
{
	return (in_at_mode_ctx(ctx) &&
		k_timer_remaining_ticks(&ctx->idle_timer) == 0);
}

bool is_idle_pipe(struct modem_pipe *pipe)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	return is_idle_ctx(ctx);
}

bool is_open_pipe(struct modem_pipe *pipe)
{
	return pipe ? sm_pipe_is_open(pipe) : false;
}

bool is_open_ctx(struct sm_at_host_ctx *ctx)
{
	return sm_at_host_get_pipe(ctx);
}

void exit_datamode_handler(int result)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	if (set_sm_mode(ctx, SM_NULL_MODE)) {
		if (ctx->data_mode.handler) {
			ctx->data_mode.handler(DATAMODE_EXIT, NULL, 0,
					       SM_DATAMODE_FLAGS_EXIT_HANDLER);
		}
		ctx->data_mode.handler = NULL;
		ctx->data_mode.handler_result = result;
		ctx->data_mode.data_len = 0;
	}
}

int sm_at_cb_wrapper(char *buf, size_t len, char *at_cmd, sm_at_callback *cb)
{
	int err;
	struct at_parser parser;
	size_t valid_count = 0;
	enum at_parser_cmd_type type;

	assert(cb);

	err = at_parser_init(&parser, at_cmd);
	if (err) {
		return err;
	}

	err = at_parser_cmd_count_get(&parser, &valid_count);
	if (err) {
		return err;
	}

	err = at_parser_cmd_type_get(&parser, &type);
	if (err) {
		return err;
	}

	err = cb(type, &parser, valid_count);
	if (!err) {
		err = at_cmd_custom_respond(buf, len, "OK\r\n");
		if (err) {
			LOG_ERR("Failed to set OK response: %d", err);
		}
	} else if (err > 0) {
		int at_cmd_err = err;

		/* Reconstruct 'ERROR', 'CME ERROR' and 'CMS ERROR' response from
		 * nrf_modem_at_cmd() return value, which is returned by some Serial Modem specific
		 * AT commands, such as AT#XSMS
		 */
		switch (nrf_modem_at_err_type(err)) {
		case NRF_MODEM_AT_CME_ERROR:
			err = at_cmd_custom_respond(buf, len, "+CME ERROR: %d\r\n",
						    nrf_modem_at_err(err));
			break;
		case NRF_MODEM_AT_CMS_ERROR:
			err = at_cmd_custom_respond(buf, len, "+CMS ERROR: %d\r\n",
						    nrf_modem_at_err(err));
			break;
		case NRF_MODEM_AT_ERROR:
		default:
			err = at_cmd_custom_respond(buf, len, "ERROR\r\n");
			break;
		}
		if (err) {
			LOG_ERR("Failed to set error response: %d", err);
		} else {
			/* Return the original error code from 'cb()' */
			err = at_cmd_err;
		}
	}

	return err;
}

static int at_host_power_off(bool shutting_down)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();
	int err = 0;
	struct modem_pipe *pipe = ctx ? atomic_ptr_get(&ctx->pipe) : NULL;

	if (shutting_down) {
		if (!pipe) {
			LOG_WRN("Failed to disable UART. (no pipe)");
		} else {
			err = modem_pipe_close(pipe, K_FOREVER);
		}
		if (err) {
			LOG_WRN("Failed to disable UART. (%d)", err);
		}
	}

	err = pm_device_action_run(sm_uart_dev, PM_DEVICE_ACTION_SUSPEND);
	if (err) {
		LOG_WRN("Failed to suspend UART. (%d)", err);
	}

	return err;
}

int sm_at_host_power_off(void)
{
	const int err = at_host_power_off(false);

	/* Write sync str to buffer so it is sent first when resuming, do not flush. */
	urc_send(SM_SYNC_STR);

	return err;
}

int sm_at_host_power_on(void)
{
	const int err = pm_device_action_run(sm_uart_dev, PM_DEVICE_ACTION_RESUME);

	if (err && err != -EALREADY) {
		LOG_ERR("Failed to resume UART. (%d)", err);
		return err;
	}

	return 0;
}

void sm_at_host_echo(bool enable)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	ctx->echo_enabled = enable;
	k_timer_stop(&ctx->idle_timer);
}

static void idle_timer_handler(struct k_timer *timer)
{
	struct sm_at_host_ctx *ctx = CONTAINER_OF(timer, struct sm_at_host_ctx, idle_timer);

	LOG_DBG("Time limit reached");
	sm_at_host_event_notify(ctx, SM_EVENT_URC);
}

static void idle_work(struct sm_at_host_ctx *ctx)
{
	/* Set as current context */
	sm_at_host_set_current_ctx(ctx);

	sys_snode_t *node;

	do {
		K_SPINLOCK(&sm_at_host_lock) {
			node = sys_slist_get(&ctx->idle_work_list);
		}
		if (!node) {
			break;
		}
		struct k_work *work = CONTAINER_OF(node, struct k_work, node);

		/* Don't submit, just run directly, because we are already in right context */
		work->handler(work);
	} while (true);

	/* Clear current context */
	sm_at_host_set_current_ctx(NULL);
}

/**
 * @brief Initialize an AT host context structure.
 *
 * Common initialization logic for both first and additional instances.
 *
 * @param ctx Context to initialize
 * @param pipe Modem pipe to attach (can be NULL)
 * @return 0 on success, negative error code on failure
 */
static int sm_at_host_ctx_init(struct sm_at_host_ctx *ctx, struct modem_pipe *pipe)
{
	/* Initialize context structure */
	memset(ctx, 0, sizeof(*ctx));

	ctx->at_buf = malloc(AT_BUF_MIN_SIZE);
	if (!ctx->at_buf) {
		LOG_ERR("Failed to allocate AT command buffer");
		return -ENOMEM;
	}
	ctx->at_buf_size = AT_BUF_MIN_SIZE;

	/* Initialize mutexes */
	k_mutex_init(&ctx->mutex_data);
	k_event_init(&ctx->pipe_event);

	/* Initialize mode */
	ctx->at_mode = SM_AT_COMMAND_MODE;
	atomic_ptr_set(&ctx->pipe, pipe);

	/* Initialize work items and timers */
	k_work_init(&ctx->raw_send_scheduled_work, raw_send_scheduled);
	k_work_init(&ctx->rx_work, at_pipe_rx_work_fn);
	k_work_init(&ctx->poll_ctx.poll_work, sm_at_socket_poll_work_handler);
	k_work_init(&ctx->poll_ctx.idle_work, sm_at_socket_poll_idle_handler);
	k_timer_init(&ctx->data_inactivity_timer, inactivity_timer_handler, NULL);
	k_timer_init(&ctx->idle_timer, idle_timer_handler, NULL);
	sys_slist_init(&ctx->idle_work_list);

	/* Initialize event context */
	sys_slist_init(&ctx->event_ctx.event_cbs);
	atomic_set(&ctx->event_ctx.events, 0);

	return 0;
}

static struct sm_at_host_ctx *sm_at_host_create(struct modem_pipe *pipe)
{
	struct sm_at_host_ctx *ctx;
	int err;

	if (!pipe) {
		LOG_ERR("Cannot create AT host without pipe");
		return NULL;
	}

	bool urcs_queued = !ring_buf_is_empty(&urc_buf);

	/* If the first instance already exists, and is
	 * not attached to any pipe, use it
	 */
	ctx = sm_at_host_get_urc_ctx();
	if (ctx && atomic_ptr_cas(&ctx->pipe, NULL, pipe)) {
		LOG_DBG("Reusing first AT host instance %p for pipe %p", (void *)ctx, (void *)pipe);
		modem_pipe_attach(pipe, at_pipe_event_handler, ctx);
		if (urcs_queued) {
			sm_at_host_event_notify(ctx, SM_EVENT_URC);
		}
		return ctx;
	}

	/* Allocate new instance from heap */
	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		LOG_ERR("Failed to allocate AT host context");
		return NULL;
	}

	/* Initialize the context */
	err = sm_at_host_ctx_init(ctx, pipe);
	if (err) {
		free(ctx);
		return NULL;
	}

	/* Add to instance list */
	K_SPINLOCK(&sm_at_host_lock) {
		sys_slist_append(&instance_list, &ctx->node);
	}

	modem_pipe_attach(pipe, at_pipe_event_handler, ctx);

	LOG_INF("Created AT host instance %p for pipe %p", (void *)ctx, (void *)pipe);
	return ctx;
}

static void send_urcs(void)
{
	while (!ring_buf_is_empty(&urc_buf)) {
		struct sm_at_host_ctx *ctx;
		uint8_t *p;
		size_t len = ring_buf_get_claim(&urc_buf, &p, UINT16_MAX);

		SYS_SLIST_FOR_EACH_CONTAINER(&instance_list, ctx, node) {
			if (!is_open(ctx) || !in_at_mode(ctx)) {
				continue;
			}
			int send = sm_at_host_pipe_tx_blocking(ctx, p, len);

			if (send < len) {
				LOG_ERR("Failed to send URC: %d (ctx %p)", send, ctx);
			}
		}
		ring_buf_get_finish(&urc_buf, len);
	}
}

static void sm_at_host_work_fn(struct k_work *work)
{
	struct sm_at_host_msg msg;

	while (k_msgq_get(&sm_at_host_msgq, &msg, K_NO_WAIT) == 0) {
		struct sm_at_host_ctx *ctx = sm_at_ctx_check(msg.ctx) ? msg.ctx : NULL;

		switch (msg.pipe_event) {
		case SM_PIPE_EVENT_OPENED:
			if (ctx) {
				struct modem_pipe *current = atomic_ptr_get(&ctx->pipe);

				if (current && current != msg.pipe) {
					LOG_ERR("Pipe mismatch on open event (ctx=%p, pipe=%p, "
						"event_pipe=%p)",
						(void *)ctx, (void *)current, (void *)msg.pipe);
					break;
				}
				if (!atomic_ptr_cas(&ctx->pipe, NULL, msg.pipe)) {
					LOG_ERR("CTX already attached to another pipe (ctx=%p, "
						"pipe=%p, event_pipe=%p)",
						(void *)ctx, (void *)current, (void *)msg.pipe);
					break;
				}
				modem_pipe_attach(msg.pipe, at_pipe_event_handler, ctx);
				LOG_DBG("AT ctx %p reopened pipe %p", (void *)ctx,
					(void *)msg.pipe);
				break;
			}
			sm_at_host_create(msg.pipe);
			break;
		case SM_PIPE_EVENT_CLOSED:
			/* NOTE: If close event is pushed into message queue but not processed
			 * before context is attached to a new pipe, we need to ignore the event
			 */
			if (ctx) {
				if (atomic_ptr_cas(&ctx->pipe, NULL, (void *)0xdeadbeef)) {
					if (msg.pipe->user_data == ctx) {
						/* Detach, in case new user have not attached yet */
						LOG_DBG("Detached CTX from pipe %p",
							(void *)msg.pipe);
						modem_pipe_attach(msg.pipe, null_pipe_handler,
								  NULL);
					}
					sm_at_host_destroy(ctx);
				} else {
					LOG_DBG("Ignoring close event for pipe %p on ctx %p with "
						"pipe %p",
						(void *)msg.pipe, (void *)ctx,
						(void *)atomic_ptr_get(&ctx->pipe));
				}
			} else {
				if (msg.pipe->callback == at_pipe_event_handler) {
					/* Detach, in case new user have not attached yet */
					modem_pipe_attach(msg.pipe, null_pipe_handler, NULL);
					LOG_DBG("Detached from pipe %p", (void *)msg.pipe);
				}
			}
			break;
		default:
			break;
		}

		switch (msg.sm_event) {
		case SM_EVENT_URC:
			/* Don't interrupt AT command execution */
			if (is_idle(ctx)) {
				send_urcs();
				idle_work(ctx);
			} else  {
				check_idle_timer(ctx, false);
			}
			break;
		case SM_EVENT_AT_MODE:
			if (is_idle(ctx)) {
				idle_work(ctx);
			} else  {
				check_idle_timer(ctx, false);
			}
			break;
		default:
			break;
		}
	}
}

void sm_at_host_queue_idle_work(struct modem_pipe *pipe, struct k_work *work)
{
	struct sm_at_host_ctx *ctx = sm_at_host_get_ctx_from(pipe);

	if (!ctx) {
		LOG_ERR("No AT host context found for pipe %p", (void *)pipe);
		return;
	}
	/*
	 * TODO: A serious race condition exists where a work item
	 * might have already been added to a idle_work_list of another socket
	 * context. This is because sm_at_socket.c uses a single async_poll_ctx but
	 * multiple sockets. So maybe the poll_ctx should be moved into sm_at_host_ctx?
	 */
	if (k_work_is_pending(work)) {
		LOG_ERR("Work %p is already pending, cannot queue", (void *)work);
		return;
	}

	K_SPINLOCK(&sm_at_host_lock) {
		if (!sys_slist_find(&ctx->idle_work_list, &work->node, &(sys_snode_t *){0})) {
			sys_slist_append(&ctx->idle_work_list, &work->node);
		}
	}
	if (is_idle(ctx)) {
		sm_at_host_event_notify(ctx, SM_EVENT_URC);
	}
}

static int sm_at_host_destroy(struct sm_at_host_ctx *ctx)
{
	struct k_work_sync sync;

	if (!ctx) {
		return -EINVAL;
	}

	if (!sm_at_ctx_check(ctx)) {
		return -EINVAL;
	}

	if (current_ctx == ctx) {
		LOG_ERR("Cannot destroy current AT host context");
		return -EPERM;
	}

	/* Cannot destroy first instance */
	if (sys_slist_len(&instance_list) == 1) {
		LOG_DBG("Cannot destroy first AT host instance");
		/* Destroy the pipe reference so we don't send to a closed pipe */
		atomic_ptr_set(&ctx->pipe, NULL);
		return -EPERM;
	}

	/* Stop timers */
	k_timer_stop(&ctx->idle_timer);
	k_timer_stop(&ctx->data_inactivity_timer);
	k_work_cancel_sync(&ctx->rx_work, &sync);
	k_work_cancel_sync(&ctx->raw_send_scheduled_work, &sync);

	/* Remove from instance list */
	K_SPINLOCK(&sm_at_host_lock) {
		sys_slist_find_and_remove(&instance_list, &ctx->node);
	}

	LOG_INF("Destroyed AT host instance %p", (void *)ctx);

	/* Free the context */
	free(ctx->data_rb_buf);
	free(ctx->at_buf);
	free(ctx);

	return 0;
}

static int sm_at_host_init(void)
{
	struct modem_pipe *pipe = sm_uart_pipe_get();
	struct sm_at_host_ctx *ctx;
	int err;

	if (!pipe) {
		LOG_ERR("No UART pipe available for AT host");
		return -ENODEV;
	}

	ctx = sm_at_host_create(pipe);
	if (!ctx) {
		LOG_ERR("Failed to create AT host context");
		return -ENOMEM;
	}

	/* Open the pipe */
	err = modem_pipe_open(pipe, K_FOREVER);
	if (err) {
		LOG_ERR("Failed to open AT pipe: %d", err);
		modem_pipe_release(pipe);
		sm_at_host_destroy(ctx);
		return err;
	}

	LOG_INF("at_host init done");
	return 0;
}
SYS_INIT(sm_at_host_init, APPLICATION, 0);

void sm_at_host_uninit(void)
{
	/* TODO: implement this */
	LOG_ERR("at_host uninit not yet implemented");
}

SM_AT_CMD_CUSTOM(xdatactrl, "AT#XDATACTRL", handle_at_datactrl);
STATIC int handle_at_datactrl(enum at_parser_cmd_type cmd_type, struct at_parser *parser, uint32_t)
{
	int ret = 0;
	uint16_t min_time_limit = get_min_data_mode_idle_timeout_ms();
	uint16_t time_limit;
	struct sm_at_host_ctx *ctx = sm_at_host_get_current();

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		ret = at_parser_num_get(parser, 1, &time_limit);
		if (ret) {
			return ret;
		}
		if (time_limit < min_time_limit) {
			LOG_ERR("Invalid time_limit: %d, min: %d", time_limit, min_time_limit);
			return -EINVAL;
		}
		ctx->data_mode.time_limit = time_limit;
		break;

	case AT_PARSER_CMD_TYPE_READ:
		rsp_send("\r\n#XDATACTRL: %d,%d\r\n", ctx->data_mode.time_limit, min_time_limit);
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XDATACTRL=<time_limit>\r\n");
		break;

	default:
		break;
	}

	return ret;
}
