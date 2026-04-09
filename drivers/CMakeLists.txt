# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

add_subdirectory_ifdef(CONFIG_DTR_UART dtr_uart)
add_subdirectory_ifdef(CONFIG_MODEM_NRF91M1 nrf91m1)
