/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_AT_NRFCLOUD_OBS_LTE_METRICS_H_
#define SM_AT_NRFCLOUD_OBS_LTE_METRICS_H_

/** @file sm_at_nrfcloud_obs_lte_metrics.h
 *
 * @brief Hooks for feeding modem AT notifications into the LTE Memfault metrics.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Update the LTE metrics on a +CEREG registration status change.
 *
 * @param reg_status The <stat> value from the +CEREG notification.
 */
void sm_memfault_lte_metrics_on_cereg(unsigned int reg_status);

/**
 * @brief Update the LTE metrics after an AT+CFUN mode change has been applied.
 *
 * Call this once the modem has accepted the AT+CFUN command. Activation starts the
 * on-time and time-to-connect timers, while deactivation stops them and clears the
 * connected state without counting it as a connection loss.
 *
 * @param mode The functional mode set by the AT+CFUN command.
 */
void sm_memfault_lte_metrics_on_cfun(unsigned int mode);

/**
 * @brief Arm the LTE metrics before an AT+CFUN mode change is forwarded to the modem.
 *
 * Call this before forwarding the command so an intentional deactivation is not counted
 * as a connection loss when the modem emits +CEREG: 0 while processing it.
 *
 * @param mode The functional mode requested by the AT+CFUN command.
 */
void sm_memfault_lte_metrics_on_cfun_request(unsigned int mode);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* SM_AT_NRFCLOUD_OBS_LTE_METRICS_H_ */
