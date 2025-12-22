# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

add_subdirectory_ifdef(CONFIG_DTR_UART dtr_uart)
add_subdirectory_ifdef(CONFIG_NRF_MODEM_LIB_TRACE_BACKEND_CMUX trace_backend_cmux)
