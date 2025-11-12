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

#define SM_DTR_GPIOS DT_NODE_HAS_PROP(DT_CHOSEN(ncs_sm_uart), dtr_gpios)
#define SM_HAS_PWR_KEY DT_HAS_CHOSEN(ncs_sm_power_key)

#if SM_DTR_GPIOS
static const struct gpio_dt_spec dtr_gpio =
	GPIO_DT_SPEC_GET_OR(DT_CHOSEN(ncs_sm_uart), dtr_gpios, {0});

static struct gpio_callback dtr_gpio_cb;
#endif
#if SM_HAS_PWR_KEY
static const struct gpio_dt_spec mdm_pwr_gpio =
	GPIO_DT_SPEC_GET_OR(DT_CHOSEN(ncs_sm_power_key), gpios, {0});

static struct gpio_callback mdm_pwr_gpio_cb;
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

#if SM_DTR_GPIOS
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
		k_work_submit_to_queue(&sm_work_q, &work);
	}
}
#endif

int sm_ctrl_pin_ready(void)
{
#if SM_DTR_GPIOS
	if (gpio_is_ready_dt(&dtr_gpio)) {
		return 0;
	}
#endif
	LOG_ERR("dtr-gpios is not ready");
	return -EFAULT;
}

void sm_ctrl_pin_enter_sleep_no_uninit(bool at_host_power_off)
{
#if SM_DTR_GPIOS || SM_HAS_PWR_KEY
	if (at_host_power_off) {
		sm_at_host_power_off();
	}

	LOG_INF("Entering sleep. No uninit.");
	LOG_PANIC();

	k_sleep(K_MSEC(100));

	nrf_regulators_system_off(NRF_REGULATORS_NS);
	assert(false);
#endif
}

void sm_ctrl_pin_enter_sleep(void)
{
#if SM_DTR_GPIOS || SM_HAS_PWR_KEY

	/* Stop threads, uninitialize host and disable DTR UART. */
	sm_at_host_uninit();

	/* Only power off the modem if it has not been put
	 * in flight mode to allow reducing NVM wear.
	 */
	if (!sm_is_modem_functional_mode(LTE_LC_FUNC_MODE_OFFLINE)) {
		sm_power_off_modem();
	}

	sm_ctrl_pin_enter_sleep_no_uninit(false);
#endif
}

void sm_ctrl_pin_enter_idle(void)
{
#if SM_DTR_GPIOS
	LOG_INF("Entering idle.");
	int err;

	err = sm_ctrl_pin_ready();
	if (err) {
		return;
	}

	gpio_init_callback(&dtr_gpio_cb, dtr_pin_callback, BIT(dtr_gpio.pin));
	err = gpio_add_callback_dt(&dtr_gpio, &dtr_gpio_cb);
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

void sm_ctrl_pin_init_gpios(void)
{
#if SM_DTR_GPIOS
	nrf_gpio_cfg_sense_set(dtr_gpio.pin, NRF_GPIO_PIN_SENSE_LOW);
#endif

#if SM_HAS_PWR_KEY
	/* Configure Modem Power GPIO */
	if (!gpio_is_ready_dt(&mdm_pwr_gpio)) {
		LOG_ERR("Modem Power GPIO not ready");
		return;
	}
	int err = gpio_pin_configure_dt(&mdm_pwr_gpio, GPIO_INPUT);

	if (err < 0) {
		LOG_ERR("Failed to configure Modem Power GPIO (%d).", err);
		return;
	}
	nrf_gpio_cfg_sense_set(mdm_pwr_gpio.pin, NRF_GPIO_PIN_SENSE_LOW);
#endif
}


#if SM_HAS_PWR_KEY
static void pwr_pin_fn(struct k_work *)
{
	nrf_gpio_cfg_sense_set(mdm_pwr_gpio.pin, NRF_GPIO_PIN_SENSE_LOW);
	sm_ctrl_pin_enter_sleep();
}

static void pwr_pin_callback(const struct device *dev, struct gpio_callback *gpio_callback,
			     uint32_t)
{
	static K_WORK_DELAYABLE_DEFINE(work, pwr_pin_fn);

	k_work_reschedule_for_queue(&sm_work_q, &work, K_MSEC(10));
}
#endif

int sm_ctrl_pin_init(void)
{
	int err;

	err = ext_xtal_control(true);
	if (err) {
		LOG_ERR("Failed to enable ext XTAL: %d", err);
		return err;
	}
#if SM_HAS_PWR_KEY
	if (!gpio_is_ready_dt(&mdm_pwr_gpio)) {
		LOG_ERR("Modem Power GPIO not ready");
		return -ENODEV;
	}
	err = gpio_pin_interrupt_configure_dt(&mdm_pwr_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		LOG_ERR("Failed to configure Modem Power GPIO interrupt (%d).", err);
		return err;
	}
	gpio_init_callback(&mdm_pwr_gpio_cb, pwr_pin_callback, BIT(mdm_pwr_gpio.pin));
	err = gpio_add_callback_dt(&mdm_pwr_gpio, &mdm_pwr_gpio_cb);
	if (err) {
		LOG_ERR("Failed to add Modem Power GPIO callback (%d).", err);
		return err;
	}
#endif
	return 0;
}
