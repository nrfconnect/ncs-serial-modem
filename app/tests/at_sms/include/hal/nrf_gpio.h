/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file hal/nrf_gpio.h
 * Simplified GPIO header for testing
 */
#ifndef NRF_GPIO_H__
#define NRF_GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO pin numbers - just stubs for tests */
#define NRF_GPIO_PIN_MAP(port, pin) ((port << 5) | pin)

typedef enum {
	NRF_GPIO_PIN_NOPULL = 0,
	NRF_GPIO_PIN_PULLDOWN = 1,
	NRF_GPIO_PIN_PULLUP = 3,
} nrf_gpio_pin_pull_t;

typedef enum {
	NRF_GPIO_PIN_S0S1 = 0,
	NRF_GPIO_PIN_H0S1 = 1,
	NRF_GPIO_PIN_S0H1 = 2,
	NRF_GPIO_PIN_H0H1 = 3,
} nrf_gpio_pin_drive_t;

typedef enum {
	NRF_GPIO_PIN_INPUT_CONNECT = 0,
	NRF_GPIO_PIN_INPUT_DISCONNECT = 1,
} nrf_gpio_pin_input_t;

typedef enum {
	NRF_GPIO_PIN_NOSENSE = 0,
	NRF_GPIO_PIN_SENSE_LOW = 2,
	NRF_GPIO_PIN_SENSE_HIGH = 3,
} nrf_gpio_pin_sense_t;

typedef enum {
	NRF_GPIO_PIN_DIR_INPUT = 0,
	NRF_GPIO_PIN_DIR_OUTPUT = 1,
} nrf_gpio_pin_dir_t;

/* Stub functions */
static inline void nrf_gpio_cfg_output(uint32_t pin_number)
{
	(void)pin_number;
}
static inline void nrf_gpio_cfg_input(uint32_t pin_number, nrf_gpio_pin_pull_t pull_config)
{
	(void)pin_number;
	(void)pull_config;
}
static inline void nrf_gpio_pin_set(uint32_t pin_number)
{
	(void)pin_number;
}
static inline void nrf_gpio_pin_clear(uint32_t pin_number)
{
	(void)pin_number;
}
static inline uint32_t nrf_gpio_pin_read(uint32_t pin_number)
{
	(void)pin_number;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* NRF_GPIO_H__ */
