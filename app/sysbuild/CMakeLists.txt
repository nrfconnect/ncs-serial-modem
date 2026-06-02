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

set(MERGED_HEX_APP_IMAGE app)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/merged_hex.cmake)
