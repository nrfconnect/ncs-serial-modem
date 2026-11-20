/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_FW_UPDATE_H
#define SM_FW_UPDATE_H

/** @file sm_fw_update.h
 *
 * @brief Firmware update functionality shared by the DFU and FOTA features.
 * @{
 */

#include <stdbool.h>

/**
 * @brief Check the result of a firmware update after boot.
 *
 * Confirms an application image that MCUboot has swapped in, and reports the result of an
 * application FOTA. The result of a modem update is handled separately by the modem
 * library DFU result hook.
 */
void sm_fw_update_boot_check(void);

/**
 * @brief Claim the modem DFU area for direct delta modem DFU.
 *
 * AT#XFOTA and AT#XNRFCLOUDFOTA use the same modem DFU area, and the modem is not usable for a
 * download while an erase of the area is pending. So the claim is refused while one of them is
 * in progress or waiting for activation. Once claimed, the area stays claimed until the modem
 * library is shut down, which closes the modem write session and activates a scheduled update.
 *
 * @retval 0 on success.
 * @retval -EBUSY if a FOTA is in progress.
 */
int sm_fw_update_modem_dfu_claim(void);

/**
 * @brief Check whether direct delta modem DFU has claimed the modem DFU area.
 *
 * While the area is claimed, AT#XFOTA and AT#XNRFCLOUDFOTA must not start a modem update or
 * erase the area.
 *
 * @return true if the modem DFU area is claimed, false otherwise.
 */
bool sm_fw_update_modem_dfu_busy(void);

/** @} */

#endif /* SM_FW_UPDATE_H */
