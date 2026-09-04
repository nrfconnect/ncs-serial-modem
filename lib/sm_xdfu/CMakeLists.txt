#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library()
zephyr_library_sources(sm_xdfu.c)

# Use modem firmware CBOR decoder from nRF Connect SDK
# The full library cannot be used as it depends on the libmodem and is targeting to run on nRF91.
zephyr_library_sources(${ZEPHYR_BASE}/../nrf/subsys/dfu/fmfu_fdev/src/modem_update_decode.c)
zephyr_include_directories(${ZEPHYR_BASE}/../nrf/subsys/dfu/fmfu_fdev/include)
