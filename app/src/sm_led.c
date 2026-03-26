/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sm_led.h"
#include "sm_util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(sm_led, CONFIG_SM_LOG_LEVEL);

/*
 * LED GPIO specs from board DTS aliases:
 *   led0 → red_led   (GPIO0.29, active HIGH)
 *   led1 → green_led (GPIO0.31, active HIGH)
 *   led2 → blue_led  (GPIO0.30, active HIGH)
 */
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/* ---- LED output helper ---- */

static void led_set(bool r, bool g, bool b)
{
	gpio_pin_set_dt(&led_red,   r ? 1 : 0);
	gpio_pin_set_dt(&led_green, g ? 1 : 0);
	gpio_pin_set_dt(&led_blue,  b ? 1 : 0);
}

/* ---- Polling thread ---- */

#define LED_POLL_INTERVAL_MS 1000
#define LED_STACK_SIZE       768
#define LED_THREAD_PRIORITY  K_LOWEST_APPLICATION_THREAD_PRIO

static void led_poll_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Previous state — only log when something changes */
	int  prev_cfun = -1;
	int  prev_sim  = -1;
	int  prev_reg  = -1;

	while (1) {
		k_sleep(K_MSEC(LED_POLL_INTERVAL_MS));

		/* --- poll CFUN --- */
		int cfun = 0;
		int cfun_ret = sm_util_at_scanf("AT+CFUN?", "+CFUN: %d", &cfun);
		if (cfun_ret != 1) {
			LOG_DBG("CFUN poll failed (ret=%d), modem not ready", cfun_ret);
			led_set(false, false, false);
			continue;
		}

		bool modem_on = (cfun != 0);

		/* --- poll SIM state --- */
		int sim = 0;
		int sim_ret = 0;
		if (modem_on) {
			/* NOTE: "%%XSIM: %d" — %% is needed in the *response* fmt for
			 * vsscanf to match a literal '%'. The AT command itself is
			 * passed as a plain string, so no escaping is needed there. */
			sim_ret = sm_util_at_scanf("AT%XSIM?", "%%XSIM: %d", &sim);
			// LOG_INF("XSIM poll: ret=%d sim=%d", sim_ret, sim);
		}

		/* --- poll registration --- */
		int creg_n = 0, creg_stat = 0;
		bool reg = false;
		int cereg_ret = 0;
		if (modem_on) {
			cereg_ret = sm_util_at_scanf("AT+CEREG?",
						     "+CEREG: %d,%d",
						     &creg_n, &creg_stat);
			// LOG_INF("CEREG poll: ret=%d n=%d stat=%d",
			// 	cereg_ret, creg_n, creg_stat);
			if (cereg_ret == 2) {
				reg = (creg_stat == 1 || creg_stat == 5);
			}
		}

		/* --- log on change --- */
		if (cfun != prev_cfun || sim != prev_sim || (int)reg != prev_reg) {
			LOG_INF("LED state change: cfun=%d sim=%d(ret=%d) reg=%d(stat=%d,ret=%d)",
				cfun, sim, sim_ret, reg, creg_stat, cereg_ret);
			prev_cfun = cfun;
			prev_sim  = sim;
			prev_reg  = (int)reg;
		}

		/* --- drive LEDs --- */
		/* Red only when AT%XSIM? succeeds AND explicitly reports no SIM.
		 * If AT%XSIM? fails (e.g. LTE radio off — WiFi/GNSS-only mode),
		 * fall through to blue so the device doesn't look broken.           */
		if (!modem_on) {
			led_set(false, false, false); /* all off */
		} else if (reg) {
			led_set(false, true, false);  /* green — registered */
		} else if (sim_ret == 1 && sim == 0) {
			led_set(true, false, false);  /* red   — confirmed no SIM */
		} else {
			led_set(false, false, true);  /* blue  — on; LTE radio off or SIM present */
		}
	}
}

K_THREAD_DEFINE(sm_led_thread, LED_STACK_SIZE,
		led_poll_thread, NULL, NULL, NULL,
		LED_THREAD_PRIORITY, 0, 0);

/* ---- GPIO initialisation ---- */

static int sm_led_gpio_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	int err;

	if (!gpio_is_ready_dt(&led_red) ||
	    !gpio_is_ready_dt(&led_green) ||
	    !gpio_is_ready_dt(&led_blue)) {
		LOG_ERR("LED GPIO controller not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_INACTIVE);
	if (err) { return err; }
	err = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (err) { return err; }
	err = gpio_pin_configure_dt(&led_blue,  GPIO_OUTPUT_INACTIVE);
	if (err) { return err; }

	/* All LEDs off at boot */
	led_set(false, false, false);

	return 0;
}

SYS_INIT(sm_led_gpio_init, POST_KERNEL, 80);
