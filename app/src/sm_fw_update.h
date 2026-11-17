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

/** Firmware update targets that direct DFU can claim. */
enum sm_fw_update_target {
	/** The modem DFU area, for delta modem firmware. */
	SM_FW_UPDATE_TARGET_MODEM,
	/** The MCUboot update slot, which application and MCUboot bootloader images share. */
	SM_FW_UPDATE_TARGET_IMAGE,
};

/**
 * @brief Claim a firmware update target for direct DFU.
 *
 * AT#XFOTA and AT#XNRFCLOUDFOTA use the same targets as direct DFU.
 * The claim is refused while a FOTA is in progress or waiting for activation.
 *
 * Once claimed, the modem DFU area stays claimed until the modem library is shut down, which closes
 * the modem write session and activates a scheduled modem update.
 * The MCUboot update slot stays claimed until the reset of the application, which activates the
 * scheduled image.
 *
 * @param target The target to claim.
 *
 * @retval 0 on success.
 * @retval -EBUSY if a FOTA is in progress.
 */
int sm_fw_update_dfu_claim(enum sm_fw_update_target target);

/**
 * @brief Check whether direct DFU has claimed any of the firmware update targets.
 *
 * While a target is claimed, AT#XFOTA and AT#XNRFCLOUDFOTA must not start an update or erase the
 * modem DFU area.
 *
 * @return true if a target is claimed, false otherwise.
 */
bool sm_fw_update_dfu_busy(void);

/** @} */

#endif /* SM_FW_UPDATE_H */
