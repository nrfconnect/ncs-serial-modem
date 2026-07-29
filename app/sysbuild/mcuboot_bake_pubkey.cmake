#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Injected into each MCUboot image's IMAGE_CONF_SCRIPT by ../sysbuild.cmake
# when MCUBOOT_BAKE_PUBKEY is set. This sets the public key that MCUboot will
# use at runtime to verify images it manages (application slot and MCUboot
# update packages).
#
# Execution order: appended AFTER the default script that sets the key from
# SB_CONFIG_BOOT_SIGNATURE_KEY_FILE, so this assignment wins. The in-build
# signing (which uses SB_CONFIG_BOOT_SIGNATURE_KEY_FILE) still succeeds with
# the debug key — those signatures are discarded. Only the key baked here
# is used in production; the matching private key never leaves Vault.
#
set_config_string(${ZCMAKE_APPLICATION} CONFIG_BOOT_SIGNATURE_KEY_FILE "${MCUBOOT_BAKE_PUBKEY}")

if(DEFINED MCUBOOT_BAKE_PUBKEY_2 AND NOT MCUBOOT_BAKE_PUBKEY_2 STREQUAL "")
    # Append second key as comma-separated entry; semicolons do not survive sysbuild.
    set_config_string(${ZCMAKE_APPLICATION} CONFIG_BOOT_SIGNATURE_KEY_FILE
                      "${MCUBOOT_BAKE_PUBKEY},${MCUBOOT_BAKE_PUBKEY_2}")
    message(STATUS "sysbuild: MCUboot keys = ${MCUBOOT_BAKE_PUBKEY},${MCUBOOT_BAKE_PUBKEY_2}")
endif()
