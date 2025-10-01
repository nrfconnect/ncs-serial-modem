/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/kernel.h>
#include <assert.h>
#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <hal/nrf_regulators.h>
#include <zephyr/sys/reboot.h>
#include "sm_at_host.h"
#include "sm_defines.h"
#include "sm_util.h"
#include "sm_ctrl_pin.h"

LOG_MODULE_REGISTER(sm_ctrl_pin, CONFIG_SM_LOG_LEVEL);

#if DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
static const struct gpio_dt_spec dtr_gpio =
	GPIO_DT_SPEC_GET_OR(DT_CHOSEN(ncs_sm_uart), dtr_gpios, {0});

static struct gpio_callback gpio_cb;
#endif

static int ext_xtal_control(bool xtal_on)
{
	int err = 0;
#if defined(CONFIG_SM_EXTERNAL_XTAL)
	static struct onoff_manager *clk_mgr;

	if (xtal_on) {
		struct onoff_client cli = {};

		/* request external XTAL for UART */
		clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
		sys_notify_init_spinwait(&cli.notify);
		err = onoff_request(clk_mgr, &cli);
		if (err < 0) {
			LOG_ERR("Clock request failed: %d", err);
			return err;
		}
		while (sys_notify_fetch_result(&cli.notify, &err) < 0) {
			/*empty*/
		}
	} else {
		/* release external XTAL for UART */
		err = onoff_release(clk_mgr);
		if (err < 0) {
			LOG_ERR("Clock release failed: %d", err);
			return err;
		}
	}
#endif

	return err;
}

#if DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
static void dtr_enable_fn(struct k_work *)
{
	LOG_INF("DTR pin callback work function.");
	sm_at_host_power_on();
}

static void dtr_pin_callback(const struct device *dev, struct gpio_callback *gpio_callback,
			     uint32_t)
{
	static K_WORK_DEFINE(work, dtr_enable_fn);
	bool asserted = gpio_pin_get_dt(&dtr_gpio);

	LOG_DBG("DTR pin %s.", asserted ? "asserted" : "de-asserted");

	if (asserted) {
		gpio_remove_callback(dev, gpio_callback);
		k_work_submit(&work);
	}
}
#endif

int sm_ctrl_pin_ready(void)
{
#if DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
	if (gpio_is_ready_dt(&dtr_gpio)) {
		return 0;
	}
#endif
	LOG_ERR("dtr-gpios is not ready");
	return -EFAULT;
}

void sm_ctrl_pin_enter_sleep(void)
{
#if DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
	int err;

	err = sm_ctrl_pin_ready();
	if (err) {
		return;
	}

	/* Stop threads, uninitialize host and disable DTR UART. */
	sm_at_host_uninit();

	/* Only power off the modem if it has not been put
	 * in flight mode to allow reducing NVM wear.
	 */
	if (!sm_is_modem_functional_mode(LTE_LC_FUNC_MODE_OFFLINE)) {
		sm_power_off_modem();
	}

	gpio_pin_interrupt_configure_dt(&dtr_gpio, GPIO_INT_DISABLE);

	LOG_INF("Entering sleep.");
	LOG_PANIC();
	nrf_gpio_cfg_sense_set(dtr_gpio.pin, NRF_GPIO_PIN_SENSE_LOW);

	k_sleep(K_MSEC(100));

	nrf_regulators_system_off(NRF_REGULATORS_NS);
	assert(false);
#endif
}

/* TODO: Cellular modem driver uses pulse triggered power management, which is not suitable with
 *       the current implementation.
 */
void sm_ctrl_pin_enter_sleep_no_uninit(void)
{
#if DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
	LOG_INF("Entering sleep.");
	LOG_PANIC();
	nrf_gpio_cfg_sense_set(dtr_gpio.pin, NRF_GPIO_PIN_SENSE_LOW);

	k_sleep(K_MSEC(100));

	nrf_regulators_system_off(NRF_REGULATORS_NS);
	assert(false);
#endif
}

void sm_ctrl_pin_enter_idle(void)
{
#if DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
	LOG_INF("Entering idle.");
	int err;

	err = sm_ctrl_pin_ready();
	if (err) {
		return;
	}

	gpio_init_callback(&gpio_cb, dtr_pin_callback, BIT(dtr_gpio.pin));
	err = gpio_add_callback_dt(&dtr_gpio, &gpio_cb);
	if (err) {
		LOG_ERR("gpio_add_callback failed: %d", err);
		return;
	}

	err = ext_xtal_control(false);
	if (err < 0) {
		LOG_WRN("Failed to disable ext XTAL: %d", err);
	}
#endif
}

void sm_ctrl_pin_enter_shutdown(void)
{
	LOG_INF("Entering shutdown.");
	k_sleep(K_MSEC(100));

	nrf_regulators_system_off(NRF_REGULATORS_NS);
	assert(false);
}

int sm_ctrl_pin_init(void)
{
	int err;

	err = ext_xtal_control(true);
	if (err < 0) {
		LOG_ERR("Failed to enable ext XTAL: %d", err);
		return err;
	}

	return err;
}
