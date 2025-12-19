#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(at_socket_test)

# Generate sm_version.h header
set(GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
add_custom_target(
    generate_version_header ALL
    COMMAND ${CMAKE_COMMAND}
            -D OUTPUT_FILE=${GENERATED_DIR}/sm_version.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/write_sm_version_header.cmake
    BYPRODUCTS ${GENERATED_DIR}/sm_version.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    COMMENT "Regenerating sm_version.h"
)
zephyr_include_directories("${GENERATED_DIR}")

# Compiler options to set configuration values
target_compile_options(app PRIVATE
  -DCONFIG_NRF_MODEM_LIB_MEM_DIAG=y
  -D__ELASTERROR=2000
  -DCONFIG_SM_AT_BUF_SIZE=4096
  -DCONFIG_SM_URC_BUFFER_SIZE=4096
  -DCONFIG_SM_DATAMODE_BUF_SIZE=4096
  -DCONFIG_SM_DATAMODE_TERMINATOR=\"+++\"
  -DCONFIG_SM_LOG_LEVEL=3
  -DCONFIG_SM_CR_LF_TERMINATION=1
  -DCONFIG_SM_TCP_POLL_TIME=10
  -DCONFIG_SM_UDP_POLL_TIME=10
  -DCONFIG_SM_CUSTOMER_VERSION=\"\"
  -DCONFIG_SM_URC_DELAY_WITH_INCOMPLETE_ECHO_MS=1000
  -DCONFIG_SM_AT_ECHO_MAX_LEN=256
  -DCONFIG_SM_UART_RX_BUF_SIZE=256
  -DCONFIG_SM_UART_TX_BUF_SIZE=256
  # Suppress upstream Zephyr warnings in net_if.h (returns address of local var)
  -Wno-return-local-addr
)

# Generate CMock mocks for external dependencies
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_modem.h)
cmock_handle(${ZEPHYR_NRF_MODULE_DIR}/include/modem/nrf_modem_lib.h)
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_socket.h)
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_modem_at.h
  FUNC_EXCLUDE "__nrf_modem_printf_like"
  FUNC_EXCLUDE "__nrf_modem_scanf_like"
  FUNC_EXCLUDE "nrf_modem_at_cmd")
cmock_handle(${ZEPHYR_BASE}/include/zephyr/net/socket.h zephyr/net)

# Generate test runner
test_runner_generate(src/test_at_socket.c)

# Add sources
target_sources(app PRIVATE
  src/test_at_socket.c
  src/nrf_modem_at_wrapper.c
  ../stubs/uart_stubs.c
  ../stubs/sm_at_host_stubs.c
  ../stubs/control_pin_stubs.c
  ../stubs/pm_stubs.c
  ../stubs/at_cmd_custom_stubs.c
  ../../src/sm_util.c
  ../../src/sm_at_socket.c
  ../../src/sm_at_host.c
  ../../src/sm_at_commands.c
)

# Include directories - override headers first
set(includes
  "${PROJECT_SOURCE_DIR}/include/"
  "${PROJECT_SOURCE_DIR}/../../src"
  "${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include"
  "${ZEPHYR_BASE}/include/"
)

target_include_directories(app PRIVATE ${includes})

# Link with wrap for sm_at_send
target_link_libraries(app PRIVATE
  "-Wl,--wrap=sm_at_send"
)
