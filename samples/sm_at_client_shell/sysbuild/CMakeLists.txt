#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)

if(NOT BOARD STREQUAL "native_sim")
  set(MERGED_HEX_APP_IMAGE sm_at_client_shell)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/merged_hex.cmake)
endif()
