# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Copyright (c) 2026 Nordic Semiconductor ASA

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(at_nrfcloud)

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
zephyr_include_directories(${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_modem/include/)

# Compiler options to set configuration values
target_compile_options(app PRIVATE
  -DCONFIG_NRF_MODEM_LIB_MEM_DIAG=y
  -D__ELASTERROR=2000
  -DCONFIG_SM_AT_BUF_SIZE=4096
  -DCONFIG_SM_URC_BUFFER_SIZE=4096
  -DCONFIG_SM_DATAMODE_BUF_SIZE=4096
  -DCONFIG_SM_DATAMODE_TERMINATOR=\"+++\"
  -DCONFIG_SM_LOG_LEVEL=0
  -DCONFIG_SM_CR_LF_TERMINATION=1
  -DCONFIG_SM_CUSTOMER_VERSION=\"\"
  -DCONFIG_SM_URC_DELAY_WITH_INCOMPLETE_ECHO_MS=1000
  -DCONFIG_SM_AT_ECHO_MAX_LEN=256
  -DCONFIG_SM_UART_RX_BUF_SIZE=256
  -DCONFIG_SM_UART_TX_BUF_SIZE=256
  -DCONFIG_SM_NRF_CLOUD_LOCATION=1
  -DCONFIG_NRF_CLOUD_SEC_TAG=16842753
)

# Generate CMock mocks
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_modem.h)
cmock_handle(${ZEPHYR_NRF_MODULE_DIR}/include/modem/nrf_modem_lib.h)
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_socket.h)
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_modem_at.h
  FUNC_EXCLUDE "__nrf_modem_printf_like"
  FUNC_EXCLUDE "__nrf_modem_scanf_like"
  FUNC_EXCLUDE "nrf_modem_at_cmd"
  # at_monitor_sys_init() calls this at SYS_INIT time, before CMock is
  # initialised by setUp(). Exclude it from mocking and provide a manual
  # stub in nrf_modem_at_wrapper.c instead.
  )
cmock_handle(${ZEPHYR_BASE}/include/zephyr/net/socket.h zephyr/net)
cmock_handle(${PROJECT_SOURCE_DIR}/../../src/sm_at_socket.h)
# Mock nRF Cloud CoAP API – the stub header carries all required type
# definitions directly; no REST-layer include chain is needed.
cmock_handle(${PROJECT_SOURCE_DIR}/include/net/nrf_cloud_coap.h)

# Generate test runner
test_runner_generate(src/test_at_nrfcloud.c)

# Add sources
target_sources(app PRIVATE
  src/test_at_nrfcloud.c
  src/nrf_modem_at_wrapper.c
  src/nrf_cloud_stubs.c          # date_time_update_async + nrf_cloud_client_id_get only
  ../stubs/uart_stubs.c
  ../stubs/sm_at_host_stubs.c
  ../stubs/control_pin_stubs.c
  ../stubs/pm_stubs.c
  ../stubs/tfm_stubs.c
  ../stubs/at_cmd_custom_stubs.c
  ../stubs/sm_workq.c
  ../stubs/sm_log_stubs.c
  ../../src/sm_util.c
  ../../src/sm_at_nrfcloud.c
  ../../src/sm_at_host.c
  ../../src/sm_at_commands.c
  ${ZEPHYR_BASE}/subsys/modem/modem_pipe.c
)

# Include directories:
#   1. Test-specific overrides (stub headers) come first so they shadow the
#      real nrf_cloud / date_time / wifi headers.
#   2. Shared test include overrides (pm_config.h, fw_info.h, etc.) from the
#      at_commands test.
#   3. Source directories and external include paths follow.
set(includes
  "${PROJECT_SOURCE_DIR}/include/"
  "${PROJECT_SOURCE_DIR}/../at_commands/include/"
  "${PROJECT_SOURCE_DIR}/../../src"
  "${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include"
  "${ZEPHYR_BASE}/include/"
  "${PROJECT_SOURCE_DIR}/../stubs"
)

target_include_directories(app PRIVATE ${includes})
