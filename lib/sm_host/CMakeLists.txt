#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library()
zephyr_library_sources(sm_host.c)
zephyr_library_sources(sm_host_monitor.c)
# Serial Modem monitor's data must be in RAM
zephyr_linker_sources(RWDATA sm_host_monitor.ld)
