/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_CTRL_PIN_
#define SM_CTRL_PIN_

/** @file sm_ctrl_pin.h
 *
 * @brief Control pin handling functions for Serial Modem
 * @{
 */

/**
 * @brief Check if control pin is ready.
 *
 * @retval 0 if ready, nonzero otherwise.
 */
int sm_ctrl_pin_ready(void);

/**
 * @brief Enter idle.
 */
void sm_ctrl_pin_enter_idle(void);

/**
 * @brief Enter sleep.
 */
void sm_ctrl_pin_enter_sleep(void);

/**
 * @brief Enter sleep without uninitializing AT host.
 *
 * @param at_host_power_off If true, power off AT host before entering sleep.
 */
void sm_ctrl_pin_enter_sleep_no_uninit(bool at_host_power_off);

/**
 * @brief nRF91 Series SiP enters System OFF mode.
 */
void sm_ctrl_pin_enter_shutdown(void);

/**
 * @brief Initialize Serial Modem control pins.
 */
void sm_ctrl_pin_init_gpios(void);

/**
 * @brief Initialize Serial Modem control pin module.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_ctrl_pin_init(void);

/** @} */

#endif /* SM_CTRL_PIN_ */
