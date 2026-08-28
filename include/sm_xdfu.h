/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_XDFU_H_
#define SM_XDFU_H_

#include <stddef.h>
#include <stdbool.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Image type accepted by the modem's AT#XDFU command.
 */
enum sm_xdfu_image_type {
	SM_XDFU_TYPE_APP = 0,
	SM_XDFU_TYPE_DELTA_MFW = 1,
	SM_XDFU_TYPE_FULL_MFW = 2,
	SM_XDFU_TYPE_MCUBOOT_BL = 3,
};

/**
 * @brief Run an AT#XDFU update on the given cellular modem
 *
 * Attach the modem's UART pipe, transfers @p file to the modem using
 * AT#XDFU commands, and resets the modem with
 * AT#XRESET afterwards.
 *
 * @param modem Cellular modem device (compatible with the Zephyr modem_cellular driver).
 * @param type  Image type to update.
 * @param file  Path to the update file (e.g. on a mounted filesystem).
 *
 * @retval 0 on success.
 * @retval -EINVAL Invalid @p type, or @p file is missing/empty.
 * @retval -ENODEV Modem or its UART device is not ready.
 * @retval -EBUSY  Modem or its UART is currently in use.
 * @retval -ETIMEDOUT Timed out waiting for the modem to respond.
 * @retval -EIO    Transfer or apply failed.
 */
int sm_xdfu_run(const struct device *modem, enum sm_xdfu_image_type type, const char *file);

#ifdef __cplusplus
}
#endif

#endif /* SM_XDFU_H_ */
