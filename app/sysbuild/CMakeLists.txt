#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Resolve cmake variable references (e.g. ${APP_DIR}) in mcuboot overlay paths
# before sysbuild configures the child images. This is needed because
# mcuboot's cmake context does not have APP_DIR defined, so string(CONFIGURE)
# in zephyr_list(NORMALIZE_PATHS) would expand it to an empty string.
# At this point APP_DIR is already set from the -DAPP_DIR= cmake argument.
if(DEFINED mcuboot_EXTRA_DTC_OVERLAY_FILE)
  message(STATUS "sysbuild: mcuboot_EXTRA_DTC_OVERLAY_FILE (raw):      ${mcuboot_EXTRA_DTC_OVERLAY_FILE}")
  message(STATUS "sysbuild: APP_DIR:                                    ${APP_DIR}")
  string(CONFIGURE "${mcuboot_EXTRA_DTC_OVERLAY_FILE}" mcuboot_EXTRA_DTC_OVERLAY_FILE_RESOLVED)
  message(STATUS "sysbuild: mcuboot_EXTRA_DTC_OVERLAY_FILE (resolved):  ${mcuboot_EXTRA_DTC_OVERLAY_FILE_RESOLVED}")
  set(mcuboot_EXTRA_DTC_OVERLAY_FILE "${mcuboot_EXTRA_DTC_OVERLAY_FILE_RESOLVED}"
      CACHE STRING "" FORCE)
else()
  message(STATUS "sysbuild: mcuboot_EXTRA_DTC_OVERLAY_FILE not defined, skipping resolution")
endif()

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)

# Combine all per-board merged hexes and the provision data into a single
# merged.hex suitable for one-shot programming (no mid-sequence ERASE_ALL).
#
# b0 and mcuboot share the same board target (nrf9151dk_nrf9151), so
# merged_<bl_board_target>.hex already contains both b0 and mcuboot (s0+s1).
# When b0 is absent (secure boot disabled), fall back to mcuboot's board target
# so the merged hex still contains at least mcuboot + app.
# app_provision.hex is only included when b0 is present (secure boot enabled).
sysbuild_get(bl_board_target  IMAGE b0      VAR CONFIG_BOARD_TARGET KCONFIG)
sysbuild_get(app_board_target IMAGE app     VAR CONFIG_BOARD_TARGET KCONFIG)

if(NOT bl_board_target)
  sysbuild_get(bl_board_target IMAGE mcuboot VAR CONFIG_BOARD_TARGET KCONFIG)
endif()

if(bl_board_target AND app_board_target)
  foreach(var bl_board_target app_board_target)
    string(REPLACE "/" "_" ${var} "${${var}}")
    string(REPLACE "." "_" ${var} "${${var}}")
    string(REPLACE "@" "_" ${var} "${${var}}")
  endforeach()

  set(merged_bl_hex  ${CMAKE_BINARY_DIR}/merged_${bl_board_target}.hex)
  set(merged_app_hex ${CMAKE_BINARY_DIR}/merged_${app_board_target}.hex)
  set(merged_out     ${CMAKE_BINARY_DIR}/merged.hex)

  # Collect inputs; provision data only exists when b0/NSIB is enabled
  set(merged_inputs   ${merged_bl_hex} ${merged_app_hex})
  set(merged_depends  merged_${bl_board_target}_target
                      merged_${app_board_target}_target
                      ${merged_bl_hex}
                      ${merged_app_hex})

  if(TARGET app_provision_target)
    list(APPEND merged_inputs  ${CMAKE_BINARY_DIR}/app_provision.hex)
    list(APPEND merged_depends app_provision_target
                               ${CMAKE_BINARY_DIR}/app_provision.hex)
  endif()

  add_custom_command(
    OUTPUT ${merged_out}
    COMMAND ${PYTHON_EXECUTABLE}
      ${ZEPHYR_BASE}/scripts/build/mergehex.py
      --overlap replace
      -o ${merged_out}
      ${merged_inputs}
    DEPENDS ${merged_depends}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating merged.hex (full device image)"
  )

  add_custom_target(merged_hex_target ALL DEPENDS ${merged_out})
endif()
