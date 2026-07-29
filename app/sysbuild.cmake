#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Application-level sysbuild hook.
#
# Split build/sign support: when -DMCUBOOT_BAKE_PUBKEY=<file.pem> is passed,
# MCUboot S0/S1 images bake that public key as the verification key MCUboot
# uses at runtime, WITHOUT changing SB_CONFIG_BOOT_SIGNATURE_KEY_FILE.
#
# Why this way: SB_CONFIG_BOOT_SIGNATURE_KEY_FILE feeds every in-build signing
# step (app image, MCUboot update packages). Pointing it at a public key would
# break all of those steps. By overriding only the bake key, the build keeps
# signing its throwaway artifacts with the default debug private key (those
# outputs are discarded), while the harvested MCUboot binaries embed the real
# production verification key. The unsigned payloads are then signed in the
# secure environment via Vault; the matching private key never leaves Vault.
#
# The key-override is appended to each MCUboot image's IMAGE_CONF_SCRIPT so it
# runs after the default config (which sets the key from SB_CONFIG), and
# therefore wins (Kconfig: last assignment wins). Normal builds are unaffected.

if(MCUBOOT_BAKE_PUBKEY)
  if(NOT EXISTS "${MCUBOOT_BAKE_PUBKEY}")
    message(FATAL_ERROR "MCUBOOT_BAKE_PUBKEY does not exist: ${MCUBOOT_BAKE_PUBKEY}")
  endif()
  set(_baked 0)
  foreach(img mcuboot mcuboot_s1_variant)
    if(TARGET ${img})
      set_property(TARGET ${img} APPEND PROPERTY IMAGE_CONF_SCRIPT
                   "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_bake_pubkey.cmake")
      set(_baked 1)
    endif()
  endforeach()
  if(_baked)
    message(STATUS "sysbuild: MCUBOOT_BAKE_PUBKEY -> MCUboot will bake ${MCUBOOT_BAKE_PUBKEY}")
  else()
    message(WARNING "sysbuild: MCUBOOT_BAKE_PUBKEY set but no mcuboot image targets found")
  endif()
endif()

if(MCUBOOT_BAKE_PUBKEY_2)
  if(NOT MCUBOOT_BAKE_PUBKEY)
    message(FATAL_ERROR "MCUBOOT_BAKE_PUBKEY_2 requires MCUBOOT_BAKE_PUBKEY to also be set")
  endif()
  if(NOT EXISTS "${MCUBOOT_BAKE_PUBKEY_2}")
    message(FATAL_ERROR "MCUBOOT_BAKE_PUBKEY_2 does not exist: ${MCUBOOT_BAKE_PUBKEY_2}")
  endif()
  message(STATUS "sysbuild: MCUBOOT_BAKE_PUBKEY_2 = ${MCUBOOT_BAKE_PUBKEY_2} (key rotation)")
endif()
