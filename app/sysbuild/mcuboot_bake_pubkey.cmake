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
if(NOT DEFINED CACHE{MCUBOOT_BAKE_PUBKEY})
  message(WARNING
    "mcuboot_bake_pubkey.cmake requires -DMCUBOOT_BAKE_PUBKEY=<file.pem> on the "
    "sysbuild cmake command line; leaving CONFIG_BOOT_SIGNATURE_KEY_FILE unchanged."
  )
  return()
endif()
set(MCUBOOT_BAKE_PUBKEY "$CACHE{MCUBOOT_BAKE_PUBKEY}")

set_config_string(${ZCMAKE_APPLICATION} CONFIG_BOOT_SIGNATURE_KEY_FILE "${MCUBOOT_BAKE_PUBKEY}")

if(DEFINED CACHE{MCUBOOT_BAKE_PUBKEY_2} AND NOT "$CACHE{MCUBOOT_BAKE_PUBKEY_2}" STREQUAL "")
  set(MCUBOOT_BAKE_PUBKEY_2 "$CACHE{MCUBOOT_BAKE_PUBKEY_2}")
  # Append second key as comma-separated entry; semicolons do not survive sysbuild.
  set_config_string(${ZCMAKE_APPLICATION} CONFIG_BOOT_SIGNATURE_KEY_FILE
    "${MCUBOOT_BAKE_PUBKEY},${MCUBOOT_BAKE_PUBKEY_2}")
  message(STATUS "sysbuild: MCUboot keys = ${MCUBOOT_BAKE_PUBKEY},${MCUBOOT_BAKE_PUBKEY_2}")
endif()
