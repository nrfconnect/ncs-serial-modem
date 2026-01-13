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

#define SM_PIPE CONFIG_SM_CMUX

#define UART_RX_TIMEOUT_US		2000
#define UART_ERROR_DELAY_MS		500

const struct device *const sm_uart_dev = DEVICE_DT_GET(DT_CHOSEN(ncs_sm_uart));
uint32_t sm_uart_baudrate;

static int sm_uart_pipe_init_internal(void);
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

enum sm_uart_state {
	SM_UART_STATE_TX_ENABLED_BIT,
	SM_UART_STATE_RX_ENABLED_BIT,
	SM_UART_STATE_RX_RECOVERY_BIT,
	SM_UART_STATE_RX_RECOVERY_DISABLED_BIT
};
static atomic_t uart_state;

enum sm_pipe_state {
	SM_PIPE_STATE_INIT_BIT,
	SM_PIPE_STATE_OPEN_BIT,
};
static struct {
	struct modem_pipe pipe;
	atomic_t state;

	struct k_work notify_transmit_idle;
	struct k_work notify_closed;
} sm_pipe = {
	.state = ATOMIC_INIT(0),
};

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
		// TODO: Retry with delay?
		LOG_ERR("UART RX recovery failed: %d", err);
	}

	atomic_clear_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_BIT);
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
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT)) {
		/* This needs to be done in system work queue to avoid deadlock while
		 * collecting modem crash dump.
		 */
		k_work_submit(&sm_pipe.notify_transmit_idle);
	}
}

static inline void uart_callback_notify_pipe_closure(void)
{
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT) &&
	    !atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT) &&
	    !atomic_test_bit(&uart_state, SM_UART_STATE_RX_ENABLED_BIT) &&
	    !atomic_test_bit(&uart_state, SM_UART_STATE_TX_ENABLED_BIT)) {
		/* Pipe is closed, RX and TX are idle, notify the closure.
		 * This should be done in system work queue.
		 */
		k_work_submit(&sm_pipe.notify_closed);
	}
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
		modem_pipe_notify_receive_ready(&sm_pipe.pipe);
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
		/* Notify pipe that receive may be ready after re-enable */
		modem_pipe_notify_receive_ready(&sm_pipe.pipe);
		break;
	default:
		break;
	}

	uart_callback_notify_pipe_closure();
}

static void notify_transmit_idle_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	modem_pipe_notify_transmit_idle(&sm_pipe.pipe);
}
static void notify_closed_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_DBG("UART pipe closed!!!");
	modem_pipe_notify_closed(&sm_pipe.pipe);
}

static int sm_uart_handler_enable(void)
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

	/* Initialize UART pipe for unified interface */
	err = sm_uart_pipe_init_internal();
	if (err && err != -EALREADY) {
		LOG_ERR("Failed to initialize UART pipe: %d", err);
		return err;
	}

	return 0;
}
SYS_INIT(sm_uart_handler_enable, POST_KERNEL, 100);

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

	return 0;
}

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
	atomic_set_bit(&sm_pipe.state, SM_PIPE_STATE_OPEN_BIT);

	atomic_clear_bit(&uart_state, SM_UART_STATE_RX_RECOVERY_DISABLED_BIT);
	ret = rx_enable();
	if (ret) {
		return ret;
	}

	tx_enable();

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

	LOG_DBG("Closing UART pipe");

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

struct modem_pipe *sm_uart_pipe_get(void)
{
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT)) {
		return &sm_pipe.pipe;
	}
	return NULL;
}

static int sm_uart_pipe_init_internal(void)
{
	if (atomic_test_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT)) {
		return -EALREADY;
	}

	k_work_init(&sm_pipe.notify_transmit_idle, notify_transmit_idle_fn);
	k_work_init(&sm_pipe.notify_closed, notify_closed_fn);

	atomic_set_bit(&sm_pipe.state, SM_PIPE_STATE_INIT_BIT);

	modem_pipe_init(&sm_pipe.pipe, &sm_pipe, &modem_pipe_api);

	return 0;
}
