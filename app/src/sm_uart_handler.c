/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/drivers/uart.h>
#include <hal/nrf_uarte.h>
#include <hal/nrf_gpio.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/pm/device.h>
#include <zephyr/modem/pipe.h>
#include "sm_uart_handler.h"
#include "sm_at_host.h"
#include "sm_util.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sm_uart_handler, CONFIG_SM_LOG_LEVEL);

#define SM_PIPE (CONFIG_SM_CMUX || CONFIG_SM_PPP)

#define UART_RX_TIMEOUT_US		2000
#define UART_ERROR_DELAY_MS		500

const struct device *const sm_uart_dev = DEVICE_DT_GET(DT_CHOSEN(ncs_sm_uart));
uint32_t sm_uart_baudrate;

static void rx_process(struct k_work *work);
static void tx_write_nonblock_fn(struct k_work *);
static K_WORK_DELAYABLE_DEFINE(rx_process_work, rx_process);
static K_WORK_DEFINE(tx_write_nonblock_work, tx_write_nonblock_fn);
struct rx_buf_t {
	atomic_t ref_counter;
	size_t len;
	uint8_t buf[CONFIG_SM_UART_RX_BUF_SIZE];
};

#define UART_SLAB_BLOCK_SIZE sizeof(struct rx_buf_t)
#define UART_SLAB_BLOCK_COUNT CONFIG_SM_UART_RX_BUF_COUNT
#define UART_SLAB_ALIGNMENT 4
BUILD_ASSERT((sizeof(struct rx_buf_t) % UART_SLAB_ALIGNMENT) == 0);
K_MEM_SLAB_DEFINE(rx_slab, UART_SLAB_BLOCK_SIZE, UART_SLAB_BLOCK_COUNT, UART_SLAB_ALIGNMENT);

/* 4 messages for 512 bytes, 32 messages for 4096 bytes. */
#define UART_RX_EVENT_COUNT ((CONFIG_SM_UART_RX_BUF_COUNT * CONFIG_SM_UART_RX_BUF_SIZE) / 128)
#define UART_RX_EVENT_COUNT_FOR_BUF (UART_RX_EVENT_COUNT / CONFIG_SM_UART_RX_BUF_COUNT)
struct rx_event_t {
	uint8_t *buf;
	size_t len;
};
K_MSGQ_DEFINE(rx_event_queue, sizeof(struct rx_event_t), UART_RX_EVENT_COUNT, 4);

RING_BUF_DECLARE(tx_buf, CONFIG_SM_UART_TX_BUF_SIZE);

struct sm_urc_ctx *urc_ctx; /* URC context for handling unsolicited responses. */

enum sm_uart_state {
	SM_UART_STATE_TX_ENABLED_BIT,
	SM_UART_STATE_RX_ENABLED_BIT,
	SM_UART_STATE_RX_RECOVERY_BIT,
	SM_UART_STATE_RX_RECOVERY_DISABLED_BIT
};
static atomic_t uart_state;

#if SM_PIPE

enum sm_pipe_state {
	SM_PIPE_STATE_INIT_BIT,
	SM_PIPE_STATE_OPEN_BIT,
};
static struct {
	struct modem_pipe pipe;
	sm_pipe_tx_t tx_cb;
	atomic_t state;

	struct k_work notify_transmit_idle;
	struct k_work notify_closed;
} sm_pipe;

#endif

K_SEM_DEFINE(tx_done_sem, 0, 1);

static inline struct rx_buf_t *block_start_get(uint8_t *buf)
{
	size_t block_num;

	/* blocks are fixed size units from a continuous memory slab: */
	/* round down to the closest unit size to find beginning of block. */

	block_num =
		(((size_t)buf - (size_t)rx_slab.buffer) / UART_SLAB_BLOCK_SIZE);

	return (struct rx_buf_t *) &rx_slab.buffer[block_num * UART_SLAB_BLOCK_SIZE];
}

static struct rx_buf_t *rx_buf_alloc(void)
{
	struct rx_buf_t *buf;
	int err;

	/* Async UART driver returns pointers to received data as */
	/* offsets from beginning of RX buffer block. */
	/* This code uses a reference counter to keep track of the number of */
	/* references within a single RX buffer block */

	err = k_mem_slab_alloc(&rx_slab, (void **) &buf, K_NO_WAIT);
	if (err) {
		return NULL;
	}

	atomic_set(&buf->ref_counter, 1);

	return buf;
}

static void rx_buf_ref(void *buf)
{
	atomic_inc(&(block_start_get(buf)->ref_counter));
}

static void rx_buf_unref(void *buf)
{
	struct rx_buf_t *uart_buf = block_start_get(buf);
	atomic_t ref_counter = atomic_dec(&uart_buf->ref_counter);

	/* ref_counter is the uart_buf->ref_counter value prior to decrement */
	if (ref_counter == 1) {
		k_mem_slab_free(&rx_slab, (void *)uart_buf);
	}
}

static int rx_enable(void)
{
	struct rx_buf_t *buf;
	int ret;

	if (atomic_test_bit(&uart_state, SM_UART_STATE_RX_ENABLED_BIT) ||
	    atomic_test_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_DISABLED_BIT)) {
		return 0;
	}

	buf = rx_buf_alloc();
	if (!buf) {
		LOG_ERR("UART RX failed to allocate buffer");
		return -ENOMEM;
	}

	ret = uart_rx_enable(sm_uart_dev, buf->buf, sizeof(buf->buf), UART_RX_TIMEOUT_US);
	if (ret) {
		LOG_ERR("UART RX enable failed: %d", ret);
		rx_buf_unref(buf);
		return ret;
	}
	atomic_set_bit(&uart_state, SM_UART_STATE_RX_ENABLED_BIT);

	return 0;
}

static int rx_disable(void)
{
	int err;

	atomic_set_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_DISABLED_BIT);

	while (atomic_test_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_BIT)) {
		/* Wait until possible recovery is complete. */
		k_sleep(K_MSEC(10));
	}

	err = uart_rx_disable(sm_uart_dev);
	if (err && err != -EFAULT) {
		LOG_ERR("UART RX disable failed: %d", err);
		return err;
	}

	while (atomic_test_bit(&uart_state, SM_UART_STATE_RX_ENABLED_BIT)) {
		/* Wait until RX stopped */
		k_sleep(K_MSEC(10));
	}

	return 0;
}

static void rx_recovery(void)
{
	int err;

	if (atomic_test_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_DISABLED_BIT)) {
		return;
	}

	atomic_set_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_BIT);

	err = rx_enable();
	if (err) {
		k_work_schedule_for_queue(&sm_work_q, &rx_process_work, K_MSEC(UART_RX_MARGIN_MS));
	}

	atomic_clear_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_BIT);
}

static void rx_process(struct k_work *work)
{
#if SM_PIPE
	/* With pipe, CMUX layer is notified and it requests the data. */
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT)) {
		modem_pipe_notify_receive_ready(&sm_pipe.pipe);
		return;
	}
#endif
	/* Without pipe, we push the data immediately. */
	struct rx_event_t rx_event;
	size_t processed;
	bool stop_at_receive = false;
	int err;

	while (k_msgq_get(&rx_event_queue, &rx_event, K_NO_WAIT) == 0) {
		processed = sm_at_receive(rx_event.buf, rx_event.len, &stop_at_receive);

		if (processed == rx_event.len) {
			/* All data processed, release the buffer. */
			rx_buf_unref(rx_event.buf);
		} else {
			rx_event.len -= processed;
			rx_event.buf += processed;
			err = k_msgq_put_front(&rx_event_queue, &rx_event, K_NO_WAIT);
			if (err) {
				LOG_ERR("RX event queue full, dropped %zu bytes", rx_event.len);
				rx_buf_unref(rx_event.buf);
			}
		}

		if (stop_at_receive) {
			break;
		}
	}

	rx_recovery();
}

static void tx_enable(void)
{
	if (!atomic_test_and_set_bit(&uart_state, SM_UART_STATE_TX_ENABLED_BIT)) {
		k_sem_give(&tx_done_sem);
	}
}

static int tx_disable(k_timeout_t timeout)
{
	int err;

	if (!atomic_test_and_clear_bit(&uart_state, SM_UART_STATE_TX_ENABLED_BIT)) {
		return 0;
	}

	if (k_sem_take(&tx_done_sem, timeout) == 0) {
		return 0;
	}

	err = uart_tx_abort(sm_uart_dev);
	if (!err) {
		LOG_INF("TX aborted");
	} else if (err != -EFAULT) {
		LOG_ERR("uart_tx_abort failed (%d).", err);
		return err;
	}

	return 0;
}

static int tx_start(void)
{
	uint8_t *buf;
	size_t len;
	int err;

	if (!atomic_test_bit(&uart_state, SM_UART_STATE_TX_ENABLED_BIT)) {
		return -EAGAIN;
	}

	len = ring_buf_get_claim(&tx_buf, &buf, ring_buf_capacity_get(&tx_buf));
	err = uart_tx(sm_uart_dev, buf, len, SYS_FOREVER_US);
	if (err) {
		LOG_ERR("UART TX error: %d", err);
		ring_buf_get_finish(&tx_buf, 0);
		return err;
	}

	return 0;
}

static inline void uart_callback_notify_pipe_transmit_idle(void)
{
#if SM_PIPE
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT)) {
		/* This needs to be done in system work queue to avoid deadlock while
		 * collecting modem crash dump.
		 */
		k_work_submit(&sm_pipe.notify_transmit_idle);
	}
#endif
}

static inline void uart_callback_notify_pipe_closure(void)
{
#if SM_PIPE
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT) &&
	    !atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT) &&
	    !atomic_test_bit(&uart_state, SM_UART_STATE_RX_ENABLED_BIT) &&
	    !atomic_test_bit(&uart_state, SM_UART_STATE_TX_ENABLED_BIT)) {
		/* Pipe is closed, RX and TX are idle, notify the closure.
		 * This should be done in system work queue.
		 */
		k_work_submit(&sm_pipe.notify_closed);
	}
#endif
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct rx_buf_t *buf;
	struct rx_event_t rx_event;
	int err;

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	switch (evt->type) {
	case UART_TX_DONE:
	case UART_TX_ABORTED:
		err = ring_buf_get_finish(&tx_buf, evt->data.tx.len);
		if (err) {
			LOG_ERR("UART_TX_%s failure: %d",
				(evt->type == UART_TX_DONE) ? "DONE" : "ABORTED", err);
		}
		if (ring_buf_is_empty(&tx_buf) ||
		    (evt->type == UART_TX_ABORTED &&
		     !atomic_test_bit(&uart_state, SM_UART_STATE_TX_ENABLED_BIT))) {
			/* TX buffer is empty or we aborted due to TX being disabled. */
			k_sem_give(&tx_done_sem);
			uart_callback_notify_pipe_transmit_idle();
		} else {
			tx_start();
		}
		break;
	case UART_RX_RDY:
		rx_buf_ref(evt->data.rx.buf);
		rx_event.buf = &evt->data.rx.buf[evt->data.rx.offset];
		rx_event.len = evt->data.rx.len;
		err = k_msgq_put(&rx_event_queue, &rx_event, K_NO_WAIT);
		if (err) {
			LOG_ERR("RX event queue full, dropped %zu bytes", evt->data.rx.len);
			rx_buf_unref(evt->data.rx.buf);
			break;
		}
		k_work_schedule_for_queue(&sm_work_q, &rx_process_work, K_NO_WAIT);
		break;
	case UART_RX_BUF_REQUEST:
		if (k_msgq_num_free_get(&rx_event_queue) < UART_RX_EVENT_COUNT_FOR_BUF) {
			LOG_WRN("Disabling UART RX: No event space.");
			break;
		}
		buf = rx_buf_alloc();
		if (!buf) {
			LOG_WRN("Disabling UART RX: No free buffers.");
			break;
		}
		err = uart_rx_buf_rsp(sm_uart_dev, buf->buf, sizeof(buf->buf));
		if (err) {
			LOG_WRN("Disabling UART RX: %d", err);
			rx_buf_unref(buf);
		}
		break;
	case UART_RX_BUF_RELEASED:
		if (evt->data.rx_buf.buf) {
			rx_buf_unref(evt->data.rx_buf.buf);
		}
		break;
	case UART_RX_DISABLED:
		atomic_clear_bit(&uart_state, SM_UART_STATE_RX_ENABLED_BIT);
		k_work_reschedule_for_queue(&sm_work_q, &rx_process_work, K_NO_WAIT);
		break;
	default:
		break;
	}

	uart_callback_notify_pipe_closure();
}

/* Write the data to tx_buffer and trigger sending. Repeat until everything is sent.
 * Returns 0 on success or a negative error code.
 */
static int tx_write_block(const uint8_t *data, size_t *len, bool flush)
{
	size_t ret;
	size_t sent = 0;
	int err;

	while (sent < *len) {
		ret = ring_buf_put(&tx_buf, data + sent, *len - sent);
		if (ret) {
			sent += ret;
			continue;
		}

		/* Buffer full, block and start TX. */
		err = k_sem_take(&tx_done_sem, K_FOREVER);
		if (err) {
			LOG_ERR("TX %s failed (%d). TX buf overflow, %zu dropped.",
				"semaphore take", err, *len - sent);
			*len = sent;
			return err;
		}
		err = tx_start();
		if (err) {
			LOG_ERR("TX %s failed (%d). TX buf overflow, %zu dropped.", "start", err,
				*len - sent);
			k_sem_give(&tx_done_sem);
			*len = sent;
			return err;
		}
	}

	*len = sent;
	if (flush && k_sem_take(&tx_done_sem, K_NO_WAIT) == 0) {
		err = tx_start();
		if (err == -EAGAIN) {
			k_sem_give(&tx_done_sem);
			return 0;
		} else if (err) {
			LOG_ERR("TX %s failed (%d).", "start", err);
			k_sem_give(&tx_done_sem);
			return err;
		}
	}

	return 0;
}

static void tx_write_nonblock_fn(struct k_work *)
{
	static struct sm_event_callback event_cb = {
		.cb = tx_write_nonblock_fn
	};

	struct sm_urc_ctx *uc = urc_ctx; /* Take a local copy. */
	uint8_t *data;
	size_t len;
	int err = 0;

	if (uc == NULL) {
		LOG_DBG("No URC context");
		return;
	}

	if (sm_at_host_echo_urc_delay()) {
		LOG_DBG("Defer URC processing until %s", "echo delay has elapsed");
		sm_at_host_register_event_cb(&event_cb, SM_EVENT_URC);
		return;
	}

	if (!in_at_mode()) {
		LOG_DBG("Defer URC processing until %s", "in AT mode");
		sm_at_host_register_event_cb(&event_cb, SM_EVENT_AT_MODE);
		return;
	}

	/* Do not lock the URC mutex.
	 * This is the only reader and URC context ownership cannot be transferred as we
	 * are in the same work queue that processes AT-commands.
	 * Locking the mutex would cause a deadlock in tx_write_nonblock if the DTR is deasserted
	 * while we are emptying the buffer.
	 */
	do {
		len = ring_buf_get_claim(&uc->rb, &data, ring_buf_capacity_get(&uc->rb));
		if (!len) {
			break;
		}
		err = tx_write_block(data, &len, true);
		ring_buf_get_finish(&uc->rb, len);

	} while (!ring_buf_is_empty(&uc->rb) && !err);

	if (err) {
		LOG_WRN("URC transmit failed (%d). %d bytes unsent.", err,
			ring_buf_size_get(&uc->rb));
	}
}

static int tx_write_nonblock(const uint8_t *data, size_t len)
{
	int ret = 0;
	struct sm_urc_ctx *uc = urc_ctx; /* Take a local copy. */

	if (uc == NULL) {
		LOG_ERR("No URC context");
		return -EFAULT;
	}

	/* Lock to prevent concurrent writes. */
	k_mutex_lock(&uc->mutex, K_FOREVER);

	if (ring_buf_space_get(&uc->rb) >= len) {
		ring_buf_put(&uc->rb, data, len);
	} else {
		LOG_WRN("URC buf overflow, dropping %u bytes.", len);
		ret = -ENOBUFS;
	}

	k_mutex_unlock(&uc->mutex);

	bool running = (sm_work_q.flags & K_WORK_QUEUE_STARTED) == K_WORK_QUEUE_STARTED;

	if (running) {
		k_work_submit_to_queue(&sm_work_q, &tx_write_nonblock_work);
	} else {
		/* Work queue not running yet, use system work queue. */
		k_work_submit(&tx_write_nonblock_work);
	}

	return ret;
}

static int sm_uart_tx_write(const uint8_t *data, size_t len, bool flush, bool urc)
{
	int ret;

	/* Send only from Serial Modem work queue to guarantee URC ordering.
	 * But only if the work queue is running.
	 * During startup, we need to use the system workqueue.
	 */
	bool running = (sm_work_q.flags & K_WORK_QUEUE_STARTED) == K_WORK_QUEUE_STARTED;

	if (running && k_current_get() == k_work_queue_thread_get(&sm_work_q) && !urc) {
		ret = tx_write_block(data, &len, flush);
	} else {
		/* In other contexts, we buffer until Serial Modem work queue becomes available. */
		ret = tx_write_nonblock(data, len);
	}

	return ret;
}

int sm_tx_write(const uint8_t *data, size_t len, bool flush, bool urc)
{
#if SM_PIPE
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT) && sm_pipe.tx_cb != NULL) {
		return sm_pipe.tx_cb(data, len, urc);
	}
#endif
	return sm_uart_tx_write(data, len, flush, urc);
}

int sm_uart_handler_enable(void)
{
	int err;
	uint32_t start_time;
	struct uart_config cfg;

	if (!device_is_ready(sm_uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	err = uart_config_get(sm_uart_dev, &cfg);
	if (err) {
		LOG_ERR("uart_config_get: %d", err);
		return err;
	}

	atomic_clear(&uart_state);

	sm_uart_baudrate = cfg.baudrate;
	LOG_INF("UART baud: %d d/p/s-bits: %d/%d/%d HWFC: %d",
		cfg.baudrate, cfg.data_bits, cfg.parity,
		cfg.stop_bits, cfg.flow_ctrl);

	/* Wait for the UART line to become valid */
	start_time = k_uptime_get_32();
	do {
		err = uart_err_check(sm_uart_dev);
		if (err) {
			uint32_t now = k_uptime_get_32();

			if (now - start_time > UART_ERROR_DELAY_MS) {
				LOG_ERR("UART check failed: %d", err);
				return -EIO;
			}
			k_sleep(K_MSEC(10));
		}
	} while (err);
	err = uart_callback_set(sm_uart_dev, uart_callback, NULL);
	if (err) {
		LOG_ERR("Cannot set callback: %d", err);
		return -EFAULT;
	}

	urc_ctx = sm_at_host_urc_ctx_acquire(SM_URC_OWNER_AT);
	if (!urc_ctx) {
		LOG_ERR("Failed to acquire URC context");
		return -EFAULT;
	}

	tx_enable();
	err = rx_enable();
	if (err) {
		return -EFAULT;
	}

	/* Flush possibly pending data in case Serial Modem was idle. */
	tx_start();

	return 0;
}

int sm_uart_handler_disable(void)
{
	int err;

	err = tx_disable(K_MSEC(50));
	if (err) {
		LOG_ERR("TX disable failed (%d).", err);
		return err;
	}

	err = rx_disable();
	if (err) {
		LOG_ERR("RX disable failed (%d).", err);
		return err;
	}

	(void)k_work_cancel_delayable(&rx_process_work);

	return 0;
}

#if SM_PIPE

static int pipe_open(void *data)
{
	int ret;

	ARG_UNUSED(data);

	if (!atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT)) {
		return -EINVAL;
	}

	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT)) {
		return -EALREADY;
	}

	atomic_clear_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_DISABLED_BIT);
	ret = rx_enable();
	if (ret) {
		return ret;
	}

	tx_enable();

	atomic_set_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT);
	modem_pipe_notify_opened(&sm_pipe.pipe);

	return 0;
}

/* Returns the number of bytes written or a negative error code. */
static int pipe_transmit(void *data, const uint8_t *buf, size_t size)
{
	size_t ret;
	size_t sent = 0;

	ARG_UNUSED(data);

	if (!atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT)) {
		return -EPERM;
	}

	if (!buf || size == 0) {
		return -EINVAL;
	}

	while (sent < size) {
		ret = ring_buf_put(&tx_buf, buf + sent, size - sent);
		if (ret) {
			sent += ret;
		} else {
			/* Buffer full. */
			break;
		}
	}

	if (k_sem_take(&tx_done_sem, K_NO_WAIT) == 0) {
		int err = tx_start();

		if (err == -EAGAIN) {
			k_sem_give(&tx_done_sem);
			return (int)sent;
		} else if (err) {
			LOG_ERR("TX %s failed (%d).", "start", err);
			k_sem_give(&tx_done_sem);
			return err;
		}
	}

	return (int)sent;
}

static int pipe_receive(void *data, uint8_t *buf, size_t size)
{
	struct rx_event_t rx_event;
	size_t received = 0;
	size_t copy_size;
	int err;

	ARG_UNUSED(data);

	if (!buf || size == 0) {
		return 0;
	}

	while (size > received) {
		if (k_msgq_get(&rx_event_queue, &rx_event, K_NO_WAIT)) {
			break;
		}
		copy_size = MIN(size - received, rx_event.len);
		memcpy(buf, rx_event.buf, copy_size);
		received += copy_size;
		buf += copy_size;

		if (rx_event.len == copy_size) {
			rx_buf_unref(rx_event.buf);
		} else {
			rx_event.len -= copy_size;
			rx_event.buf += copy_size;
			err = k_msgq_put_front(&rx_event_queue, &rx_event, K_NO_WAIT);
			if (err) {
				LOG_ERR("RX event queue full, dropped %zu bytes", rx_event.len);
				rx_buf_unref(rx_event.buf);
			}
		}
	}
	if (k_msgq_num_used_get(&rx_event_queue) == 0) {
		/* Try to recover RX, in case it was disabled. */
		rx_recovery();
	}

	return (int)received;
}

static int pipe_close(void *data)
{
	ARG_UNUSED(data);

	if (!atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT)) {
		return -EALREADY;
	}

	atomic_clear_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT);

	return sm_uart_handler_disable();
}

static const struct modem_pipe_api modem_pipe_api = {
	.open = pipe_open,
	.transmit = pipe_transmit,
	.receive = pipe_receive,
	.close = pipe_close,
};

static void notify_transmit_idle_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	modem_pipe_notify_transmit_idle(&sm_pipe.pipe);
}
static void notify_closed_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	modem_pipe_notify_closed(&sm_pipe.pipe);
}

static void at_to_cmux_switch(void)
{
	/* TX handling when moving from AT to CMUX. */

	/* - Complete (OK message) TX transmission through regular UART. */
	tx_disable(K_MSEC(10));

	/* - Release URC context for handling unsolicited responses.
	 *   We are serving AT#XCMUX, so it is not possible that the URC sending would be active.
	 */
	sm_at_host_urc_ctx_release(urc_ctx, SM_URC_OWNER_AT);
	urc_ctx = NULL;

	/* RX handling when moving from AT to CMUX:
	 * - RX and RX buffers are retained.
	 * - Data in RX buffers is routed to CMUX AT channel.
	 */
}

struct modem_pipe *sm_uart_pipe_init(sm_pipe_tx_t pipe_tx_cb)
{
	k_work_init(&sm_pipe.notify_transmit_idle, notify_transmit_idle_fn);
	k_work_init(&sm_pipe.notify_closed, notify_closed_fn);

	sm_pipe.tx_cb = pipe_tx_cb;
	atomic_set_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT);

	modem_pipe_init(&sm_pipe.pipe, &sm_pipe, &modem_pipe_api);

	at_to_cmux_switch();

	return &sm_pipe.pipe;
}

#endif /* SM_PIPE */
