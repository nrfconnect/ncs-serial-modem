#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(.)
target_sources_ifdef(CONFIG_SM_HTTPC app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sm_at_httpc.c)
