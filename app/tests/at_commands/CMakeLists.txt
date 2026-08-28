# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Copyright (c) 2025 Nordic Semiconductor ASA

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(at_commands)

# Generate sm_version.h header
set(GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
add_custom_target(
    generate_version_header ALL
    COMMAND ${CMAKE_COMMAND}
            -DOUTPUT_FILE=${GENERATED_DIR}/sm_version.h
            -DVERSION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/VERSION
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/write_sm_version_header.cmake
    BYPRODUCTS ${GENERATED_DIR}/sm_version.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    COMMENT "Regenerating sm_version.h"
)
zephyr_include_directories("${GENERATED_DIR}")

target_compile_options(app PRIVATE
  -D__ELASTERROR=2000
)

# Generate CMock
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_modem.h)
cmock_handle(${ZEPHYR_NRF_MODULE_DIR}/include/modem/nrf_modem_lib.h)
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_socket.h)
cmock_handle(${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include/nrf_modem_at.h
  FUNC_EXCLUDE "__nrf_modem_printf_like"
  FUNC_EXCLUDE "__nrf_modem_scanf_like"
  FUNC_EXCLUDE "nrf_modem_at_cmd")
cmock_handle(${ZEPHYR_BASE}/include/zephyr/net/socket.h zephyr/net)
cmock_handle(${ZEPHYR_NRF_MODULE_DIR}/include/modem/modem_jwt.h)
cmock_handle(${PROJECT_SOURCE_DIR}/../../src/sm_at_socket.h)

# Generate test runner
test_runner_generate(src/test_at_commands.c)

# Add sources
target_sources(app PRIVATE
  src/test_at_commands.c
  src/nrf_modem_at_wrapper.c
  ../stubs/uart_stubs.c
  ../stubs/sm_at_host_stubs.c
  ../stubs/control_pin_stubs.c
  ../stubs/pm_stubs.c
  ../stubs/tfm_stubs.c
  ../stubs/at_cmd_custom_stubs.c
  ../stubs/sm_workq.c
  ../stubs/sm_log_stubs.c
  ../../src/sm_util.c
  ../../src/sm_at_commands.c
  ../../src/sm_at_host.c
  ${ZEPHYR_BASE}/subsys/modem/modem_pipe.c
)

# Include directories - override headers first
set(includes
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/../../src"
  "${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include"
  "${ZEPHYR_BASE}/include"
  "${ZEPHYR_BASE}/lib/libc/common/include"
  "${PROJECT_SOURCE_DIR}/../stubs"
)

target_include_directories(app BEFORE PRIVATE ${includes})
