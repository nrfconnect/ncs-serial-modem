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
 * @brief Enter idle.
 */
void sm_ctrl_pin_enter_idle(void);

/**
 * @brief Enter sleep.
 */
void sm_ctrl_pin_enter_sleep(void);

/**
 * @brief Enter sleep without uninitializing AT host.
 */
void sm_ctrl_pin_enter_sleep_no_uninit(void);

/**
 * @brief nRF91 Series SiP enters System OFF mode.
 */
void sm_ctrl_pin_enter_shutdown(void);

/**
 * @brief Temporarily sets the indicate pin high.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_ctrl_pin_indicate(void);

/**
 * @brief Initialize Serial Modem control pin module.
 *
 * @retval 0 on success, nonzero otherwise.
 */
int sm_ctrl_pin_init(void);

/**
 * @brief Initialize Serial Modem control pins, that is, power and indicate pins.
 */
void sm_ctrl_pin_init_gpios(void);

/** @} */

#endif /* SM_CTRL_PIN_ */
