#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Generates ${CMAKE_BINARY_DIR}/merged.hex by combining all images into a
# single flashable file.
#
# The caller must set MERGED_HEX_APP_IMAGE to the sysbuild image name of the
# application before including this file.
#
# Requires SB_CONFIG_MERGED_HEX_FILES=y so that sysbuild produces the
# per-board merged hex files used as inputs.  Callers should guard the
# include() with a board check and pair it with a Kconfig.sysbuild that adds
# "depends on !BOARD_NATIVE_SIM" (or similar) to MERGED_HEX_FILES so that
# simulator targets are excluded at the Kconfig level as well.
#
# With a bootloader (b0 / mcuboot):
#   merged.hex = mergehex(merged_<bl_board>.hex, merged_<app_board>.hex
#                         [, app_provision.hex])
#
# Without a bootloader (single-image build):
#   merged.hex = copy of merged_<app_board>.hex

if(NOT DEFINED MERGED_HEX_APP_IMAGE)
  message(FATAL_ERROR
    "Set MERGED_HEX_APP_IMAGE to the sysbuild image name before "
    "including merged_hex.cmake")
endif()

# Bootloader board target: prefer b0, fall back to mcuboot
if(TARGET b0)
  sysbuild_get(bl_board_target IMAGE b0 VAR CONFIG_BOARD_TARGET KCONFIG)
endif()
if(NOT bl_board_target AND TARGET mcuboot)
  sysbuild_get(bl_board_target IMAGE mcuboot VAR CONFIG_BOARD_TARGET KCONFIG)
endif()

sysbuild_get(app_board_target IMAGE ${MERGED_HEX_APP_IMAGE} VAR CONFIG_BOARD_TARGET KCONFIG)

if(NOT app_board_target)
  message(WARNING
    "merged_hex.cmake: no board target for image '${MERGED_HEX_APP_IMAGE}', "
    "skipping merged.hex generation")
  return()
endif()

set(merged_out ${CMAKE_BINARY_DIR}/merged.hex)

# Sanitise board target strings (replace path separators with underscores)
foreach(var bl_board_target app_board_target)
  if(${var})
    string(REPLACE "/" "_" ${var} "${${var}}")
    string(REPLACE "." "_" ${var} "${${var}}")
    string(REPLACE "@" "_" ${var} "${${var}}")
  endif()
endforeach()

if(bl_board_target)
  # Multi-image build: combine bootloader merged hex + app merged hex
  # b0 and mcuboot share the same board target on nrf9151dk, so
  # merged_<bl_board_target>.hex already contains both b0 and mcuboot (s0 + s1).
  set(merged_bl_hex  ${CMAKE_BINARY_DIR}/merged_${bl_board_target}.hex)
  set(merged_app_hex ${CMAKE_BINARY_DIR}/merged_${app_board_target}.hex)

  set(merged_inputs  ${merged_bl_hex} ${merged_app_hex})
  set(merged_depends merged_${bl_board_target}_target
                     merged_${app_board_target}_target
                     ${merged_inputs})

  # app_provision.hex is only present when b0/NSIB is enabled
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
    COMMENT "Generating merged.hex (bootloader + app)"
  )
else()
  # Single-image build: promote the per-board merged hex to merged.hex.
  # Requires SB_CONFIG_MERGED_HEX_FILES=y (Kconfig.sysbuild disables this for
  # native_sim and other targets that do not produce Intel hex files).
  set(merged_app_hex ${CMAKE_BINARY_DIR}/merged_${app_board_target}.hex)

  add_custom_command(
    OUTPUT ${merged_out}
    COMMAND ${CMAKE_COMMAND} -E copy ${merged_app_hex} ${merged_out}
    DEPENDS merged_${app_board_target}_target ${merged_app_hex}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating merged.hex (single image)"
  )
endif()

add_custom_target(merged_hex_target ALL DEPENDS ${merged_out})
