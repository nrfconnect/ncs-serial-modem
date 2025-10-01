/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/pm/device.h>

/*
 * DTR (Data Terminal Ready) Logic:
 *
 * This driver implements DTR flow control where DTR input levels directly
 * correspond to DTR assertion/deassertion events:
 *
 * DTR Input Level 0 → DTR_DEASSERTED → UART inactive (powered down)
 * DTR Input Level 1 → DTR_ASSERTED   → UART active (powered up and ready)
 *
 * Internal state representation (matches input level):
 * - dtr_state = 0: DTR deasserted, UART inactive
 * - dtr_state = 1: DTR asserted, UART active
 */

LOG_MODULE_REGISTER(dtr_uart, CONFIG_DTR_UART_LOG_LEVEL);

#define DT_DRV_COMPAT nordic_dtr_uart

struct dtr_uart_data {
	/* --- Device and configuration --- */
	const struct device *dev;

	/* --- TX (Transmit) state --- */
	const uint8_t *tx_buf;
	size_t tx_len;
	bool tx_in_progress;

	/* --- RX (Receive) state --- */
	bool app_rx_enabled;		/* RX enabled by application */
	bool rx_active;			/* RX currently active */
	int32_t rx_timeout;
	struct k_sem rx_disable_sync;	/* Semaphore for RX disable completion */

	/* --- DTR (Data Terminal Ready) --- */
	bool dtr_state;	/* 0 = deasserted (UART inactive), 1 = asserted (UART active) */
	struct gpio_callback dtr_cb;
	struct k_mutex dtr_mutex;
	struct k_work_delayable dtr_work;

	/* --- RI (Ring Indicator) --- */
	struct k_work_delayable ri_work;

	/* --- Power Management --- */
	bool pm_suspended;	/* 0 = UART && DTR active, 1 = UART && DTR inactive */

	/* --- User callback --- */
	uart_callback_t user_callback;
	void *user_data;
};
/* Configuration structure. */
struct dtr_uart_config {
	/* Physical UART device */
	const struct device *uart;
	struct gpio_dt_spec dtr_gpio;
	struct gpio_dt_spec ri_gpio;
};

static void user_callback(const struct device *dev, struct uart_event *evt);

/* --- Power Management --- */
static void power_on_uart(struct dtr_uart_data *data)
{
	const struct dtr_uart_config *config = data->dev->config;
	enum pm_device_state state = PM_DEVICE_STATE_OFF;
	int err = pm_device_state_get(config->uart, &state);

	if (err) {
		LOG_ERR("Failed to get PM device state (%d).", err);
	}
	if (state != PM_DEVICE_STATE_ACTIVE) {
		/* Power on UART module */
		err = pm_device_action_run(config->uart, PM_DEVICE_ACTION_RESUME);
		if (err) {
			LOG_ERR("Failed to %s UART device (%d).", "resume", err);
		}
		LOG_DBG("UART powered on");
	}
}

static void power_off_uart(struct dtr_uart_data *data)
{
	const struct dtr_uart_config *config = data->dev->config;
	enum pm_device_state state = PM_DEVICE_STATE_OFF;
	int err = pm_device_state_get(config->uart, &state);

	if (err) {
		LOG_ERR("Failed to get PM device state (%d).", err);
		return;
	}
	if (state != PM_DEVICE_STATE_SUSPENDED) {
		/* Power off UART module */
		err = pm_device_action_run(config->uart, PM_DEVICE_ACTION_SUSPEND);
		if (err) {
			LOG_ERR("Failed to %s UART device (%d).", "suspend", err);
		}
		LOG_DBG("UART powered off");
	}
}

/* --- TX/RX helpers --- */
static void tx_complete(struct dtr_uart_data *data)
{
	data->tx_in_progress = false;
	data->tx_buf = NULL;
	data->tx_len = 0;
}

static void activate_tx(struct dtr_uart_data *data)
{
	const struct dtr_uart_config *config = data->dev->config;

	if (data->tx_buf) {
		int err;

		data->tx_in_progress = true;
		err = uart_tx(config->uart, data->tx_buf, data->tx_len, SYS_FOREVER_US);
		if (err) {
			LOG_ERR("TX: Not started (%d).", err);

			struct uart_event evt = {
				.type = UART_TX_ABORTED,
				.data.tx.buf = data->tx_buf,
				.data.tx.len = 0,
			};

			tx_complete(data);
			user_callback(data->dev, &evt);
		}
	}
}

static int deactivate_tx(struct dtr_uart_data *data)
{
	const struct dtr_uart_config *config = data->dev->config;
	int err;

	if (data->tx_buf && !data->tx_in_progress) {
		LOG_DBG("TX: Abort - Before started.");

		struct uart_event evt = {
			.type = UART_TX_ABORTED,
			.data.tx.buf = data->tx_buf,
			.data.tx.len = 0,
		};
		tx_complete(data);
		user_callback(data->dev, &evt);
		return 0;
	}

	err = uart_tx_abort(config->uart);
	if (err == 0) {
		LOG_DBG("TX: Abort.");
	} else if (err != -EFAULT) {
		/* We assume that UART_TX_ABORTED is sent. */
		LOG_ERR("TX: Abort (%d).", err);
	}

	return err;
}

static int deactivate_rx(struct dtr_uart_data *data)
{
	const struct dtr_uart_config *config = data->dev->config;
	int err;

	data->rx_active = false;

	err = uart_rx_disable(config->uart);
	if (err == -EFAULT) {
		LOG_DBG("RX: Already disabled.");
		err = 0;
	} else if (err && err != -EFAULT) {
		LOG_ERR("RX: Failed to disable (%d).", err);
	}

	return err;
}

static void activate_rx(struct dtr_uart_data *data)
{
	if (data->rx_active) {
		LOG_DBG("RX: Already active");
		return;
	}

	if (!data->app_rx_enabled) {
		LOG_DBG("RX: Not enabled by application");
		return;
	}

	struct uart_event evt = {
		.type = UART_RX_BUF_REQUEST,
	};
	user_callback(data->dev, &evt);
}

/* --- RI handling --- */
static void ri_work_fn(struct k_work *work)
{
	const struct k_work_delayable *delayed_work =
		CONTAINER_OF(work, struct k_work_delayable, work);
	const struct dtr_uart_data *data =
		CONTAINER_OF(delayed_work, struct dtr_uart_data, ri_work);
	const struct dtr_uart_config *config = data->dev->config;

	gpio_pin_set_dt(&config->ri_gpio, 0);
}

static void ri_start(struct dtr_uart_data *data)
{
	const struct dtr_uart_config *config = data->dev->config;

	gpio_pin_set_dt(&config->ri_gpio, 1);
	k_work_schedule(&data->ri_work, K_MSEC(100));
}

/* --- DTR handling --- */
static void uart_dtr_input_gpio_callback(const struct device *port, struct gpio_callback *cb,
					 uint32_t pins)
{
	struct dtr_uart_data *data = CONTAINER_OF(cb, struct dtr_uart_data, dtr_cb);

	k_work_reschedule(&data->dtr_work, K_MSEC(10));
}

static void dtr_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct dtr_uart_data *data = CONTAINER_OF(dwork, struct dtr_uart_data, dtr_work);
	const struct dtr_uart_config *config = data->dev->config;

	k_mutex_lock(&data->dtr_mutex, K_FOREVER);

	bool asserted = gpio_pin_get_dt(&config->dtr_gpio) && !data->pm_suspended;

	if (data->dtr_state == asserted) {
		LOG_INF("DTR is already %s, ignoring event", asserted ? "asserted" : "deasserted");
		goto exit;
	}

	LOG_DBG("DTR %s", asserted ? "asserted" : "deasserted");
	data->dtr_state = asserted;

	if (asserted) {
		/* Stop RI signal. */
		k_work_cancel_delayable(&data->ri_work);
		gpio_pin_set_dt(&config->ri_gpio, 0);

		/* Enable UART and RX/TX. */
		power_on_uart(data);
		activate_rx(data);
		activate_tx(data);
	} else {
		/* Disable TX. */
		deactivate_tx(data);

		/* Wait for RX to be fully disabled, before powering UART down. */
		k_sem_reset(&data->rx_disable_sync);
		deactivate_rx(data);
		k_sem_take(&data->rx_disable_sync, K_MSEC(100));
		power_off_uart(data);
	}
exit:
	k_mutex_unlock(&data->dtr_mutex);
}

/* --- UART and user callbacks --- */
static void user_callback(const struct device *dev, struct uart_event *evt)
{
	const struct dtr_uart_data *data = dev->data;

	if (data->user_callback) {
		data->user_callback(dev, evt, data->user_data);
	}
}

static void uart_callback(const struct device *uart, struct uart_event *evt, void *user_data)
{
	struct device *dev = user_data;
	struct dtr_uart_data *data = dev->data;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX: Done");
		tx_complete(data);
		user_callback(dev, evt);
		break;
	case UART_TX_ABORTED:
		LOG_DBG("TX: Aborted");
		tx_complete(data);
		user_callback(dev, evt);
		break;
	case UART_RX_RDY:
		LOG_DBG("RX: Ready buf:%p, offset: %d,len: %d", (void *)evt->data.rx.buf,
			evt->data.rx.offset, evt->data.rx.len);
		user_callback(dev, evt);
		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("RX: Buf request");
		user_callback(dev, evt);
		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("RX: Buf released %p", (void *)evt->data.rx_buf.buf);
		user_callback(dev, evt);
		break;

	case UART_RX_DISABLED: {
		LOG_DBG("RX: Disabled. DTR: %d.", data->dtr_state);
		/* When RX disabled because of DTR down, we handle it ourselves. */
		if (data->dtr_state && data->app_rx_enabled) {
			data->app_rx_enabled = false;
			user_callback(dev, evt);
		}

		if (!data->dtr_state) {
			/* RX disabled because of DTR down. */
			k_sem_give(&data->rx_disable_sync);
		}
		break;
	}
	case UART_RX_STOPPED:
		LOG_DBG("RX: Stopped");
		if (data->dtr_state && data->app_rx_enabled) {
			user_callback(dev, evt);
		}
		break;
	}
}

/* --- API Implementation --- */
static int api_callback_set(const struct device *dev, uart_callback_t callback, void *user_data)
{
	struct dtr_uart_data *data = dev->data;

	data->user_callback = callback;
	data->user_data = user_data;

	return 0;
}

static int api_tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
	const struct dtr_uart_config *config = dev->config;
	struct dtr_uart_data *data = dev->data;

	LOG_DBG("api_tx: %zu bytes", len);

	if (buf == NULL || len == 0) {
		struct uart_event evt = {
			.type = UART_TX_DONE,
			.data.tx.buf = buf,
			.data.tx.len = 0,
		};
		user_callback(data->dev, &evt);
		return 0;
	}

	if (data->tx_buf) {
		LOG_WRN("TX: already scheduled");
		return -EBUSY;
	}

	if (data->dtr_state) {
		return uart_tx(config->uart, buf, len, timeout);
	}
	data->tx_buf = buf;
	data->tx_len = len;

	/* Start RI pulse. */
	ri_start(data);

	/* Buffer the data until DTR is down. */
	return 0;
}

static int api_tx_abort(const struct device *dev)
{
	struct dtr_uart_data *data = dev->data;

	LOG_DBG("api_tx_abort");

	return deactivate_tx(data);
}

static int api_rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout)
{
	const struct dtr_uart_config *config = dev->config;
	struct dtr_uart_data *data = dev->data;

	LOG_DBG("api_rx_enable: %p, %zu", (void *)buf, len);

	if (data->app_rx_enabled) {
		LOG_ERR("RX already enabled");
		return -EBUSY;
	}
	data->app_rx_enabled = true;
	data->rx_timeout = timeout;

	if (!data->dtr_state) {
		LOG_DBG("RX: DTR not asserted, releasing buffer.");
		struct uart_event evt = {
			.type = UART_RX_BUF_RELEASED,
			.data.rx_buf.buf = buf,
		};
		user_callback(dev, &evt);
		return 0;
	}
	data->rx_active = true;
	return uart_rx_enable(config->uart, buf, len, timeout);
}

static int api_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	const struct dtr_uart_config *config = dev->config;
	struct dtr_uart_data *data = dev->data;
	int err = 0;

	LOG_DBG("api_rx_buf_rsp: %p, len: %zu", (void *)buf, len);

	if (!data->dtr_state) {
		goto release;
	}

	if (!data->app_rx_enabled) {
		goto release;
	}

	if (!data->rx_active) {
		data->rx_active = true;
		err = uart_rx_enable(config->uart, buf, len, data->rx_timeout);
		if (err == -EBUSY) {
			LOG_ERR("RX: Busy");
			err = 0;
			goto release;
		}
		if (err) {
			LOG_ERR("RX: Enable failed (%d).", err);
			data->rx_active = false;
			goto release;
		}

		LOG_DBG("RX: Enabled");
		return 0;
	}
	return uart_rx_buf_rsp(config->uart, buf, len);

release:
	struct uart_event evt = {
		.type = UART_RX_BUF_RELEASED,
		.data.rx_buf.buf = buf,
	};
	user_callback(dev, &evt);
	return err;
}

static int api_rx_disable(const struct device *dev)
{
	struct dtr_uart_data *data = dev->data;

	LOG_DBG("api_rx_disable");

	data->app_rx_enabled = false;
	return deactivate_rx(data);
}

static int api_err_check(const struct device *dev)
{
	const struct dtr_uart_config *config = dev->config;

	return uart_err_check(config->uart);
}

#if defined(CONFIG_UART_USE_RUNTIME_CONFIGURE)
static int api_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct dtr_uart_config *config = dev->config;

	return uart_configure(config->uart, cfg);
}

static int api_config_get(const struct device *dev, struct uart_config *cfg)
{
	const struct dtr_uart_config *config = dev->config;

	return uart_config_get(config->uart, cfg);
}
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

/* --- PM Device Management --- */
#if defined(CONFIG_PM_DEVICE)
static int dtr_uart_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct dtr_uart_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		LOG_DBG("PM SUSPEND - Disobey DTR and disable UART");
		data->pm_suspended = true;
		dtr_work_handler(&data->dtr_work.work);
		break;
	case PM_DEVICE_ACTION_RESUME:
		LOG_DBG("PM RESUME - Obey DTR");
		data->pm_suspended = false;
		dtr_work_handler(&data->dtr_work.work);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif

/* --- Initialization --- */
static int dtr_uart_init(const struct device *dev)
{
	struct dtr_uart_data *data = dev->data;
	const struct dtr_uart_config *config = dev->config;
	int err;

	data->dev = dev;

	/* Check UART device readiness. */
	if (!device_is_ready(config->uart)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	/* Check and configure DTR GPIO as input. */
	if (!gpio_is_ready_dt(&config->dtr_gpio)) {
		LOG_ERR("DTR GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&config->dtr_gpio, GPIO_INPUT);
	if (err < 0) {
		LOG_ERR("Failed to configure DTR GPIO (%d).", err);
		return err;
	}

	/* Check and configure RI GPIO as output. */
	if (!gpio_is_ready_dt(&config->ri_gpio)) {
		LOG_ERR("RI GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&config->ri_gpio, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		LOG_ERR("Failed to configure RI GPIO (%d).", err);
		return err;
	}

	/* Initialize data structure. */
	data->rx_timeout = SYS_FOREVER_US;
	k_mutex_init(&data->dtr_mutex);
	k_sem_init(&data->rx_disable_sync, 0, 1);
	k_work_init_delayable(&data->dtr_work, dtr_work_handler);
	k_work_init_delayable(&data->ri_work, ri_work_fn);

	/* Set UART callback. */
	err = uart_callback_set(config->uart, uart_callback, (void *)dev);
	if (err < 0) {
		LOG_ERR("Failed to set UART callback (%d).", err);
		return -EINVAL;
	}

	/* Read initial DTR state. */
	int initial_dtr_state = gpio_pin_get_dt(&config->dtr_gpio);

	if (initial_dtr_state < 0) {
		LOG_ERR("Failed to read initial DTR state (%d).", initial_dtr_state);
		return initial_dtr_state;
	}
	/* Map GPIO input level directly to DTR state:
	 * Input level 0 → DTR deasserted (dtr_state = 0, UART inactive)
	 * Input level 1 → DTR asserted (dtr_state = 1, UART active)
	 */
	data->dtr_state = initial_dtr_state;

	/* Set up GPIO interrupt for DTR changes. */
	gpio_init_callback(&data->dtr_cb, uart_dtr_input_gpio_callback, BIT(config->dtr_gpio.pin));
	err = gpio_add_callback(config->dtr_gpio.port, &data->dtr_cb);
	if (err < 0) {
		LOG_ERR("Failed to add DTR GPIO callback (%d).", err);
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&config->dtr_gpio, GPIO_INT_EDGE_BOTH);
	if (err < 0) {
		LOG_ERR("Failed to configure DTR GPIO interrupt (%d).", err);
		return err;
	}

	LOG_DBG("DTR UART initialized, initial DTR state: %d", data->dtr_state);
	return 0;
}

/* --- UART driver API ---- */
static const struct uart_driver_api dtr_uart_api = {
	.callback_set = api_callback_set,
	.tx = api_tx,
	.tx_abort = api_tx_abort,
	.rx_enable = api_rx_enable,
	.rx_buf_rsp = api_rx_buf_rsp,
	.rx_disable = api_rx_disable,
	.err_check = api_err_check,
#if defined(CONFIG_UART_USE_RUNTIME_CONFIGURE)
	.configure = api_configure,
	.config_get = api_config_get,
#endif
};

/* --- Device Instantiation --- */
#define DTR_UART_INIT(n)                                                                           \
	static const struct dtr_uart_config dtr_uart_config_##n = {                                \
		.dtr_gpio = GPIO_DT_SPEC_INST_GET(n, dtr_gpios),                                   \
		.ri_gpio = GPIO_DT_SPEC_INST_GET(n, ri_gpios),                                     \
		.uart = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(n))),                                  \
	};                                                                                         \
	static struct dtr_uart_data dtr_uart_data_##n;                                             \
	PM_DEVICE_DT_INST_DEFINE(n, dtr_uart_pm_action);                                           \
	DEVICE_DT_INST_DEFINE(n, dtr_uart_init, PM_DEVICE_DT_INST_GET(n), &dtr_uart_data_##n,      \
			      &dtr_uart_config_##n, POST_KERNEL, 51, &dtr_uart_api);

DT_INST_FOREACH_STATUS_OKAY(DTR_UART_INIT)
