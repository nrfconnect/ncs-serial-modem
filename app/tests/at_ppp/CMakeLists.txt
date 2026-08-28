#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(at_ppp_test)

zephyr_include_directories(${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_modem/include/)

# Compiler options to set configuration values
target_compile_options(app PRIVATE
  -D__ELASTERROR=2000
  # Suppress upstream Zephyr warnings in net_if.h (returns address of local var)
  -Wno-return-local-addr
)

# Generate test runner
test_runner_generate(src/test_at_ppp.c)

# Add sources.
#
# Only sm_ppp.c and sm_cmux.c are compiled from the application; everything
# they depend on is stubbed so that the PPP state machine can be driven
# deterministically.
target_sources(app PRIVATE
  src/test_at_ppp.c
  src/net_if_stubs.c
  src/socket_stubs.c
  src/cmux_stubs.c
  src/sm_stubs.c
  ../stubs/sm_workq.c
  ../stubs/at_cmd_custom_stubs.c
  ../../src/sm_ppp.c
  ../../src/sm_cmux.c
)

# Include directories - test-specific overrides first so that they shadow the
# real <zephyr/modem/ppp.h>, <zephyr/modem/cmux.h>,
# <zephyr/posix/sys/eventfd.h> and <modem/at_cmd_custom.h> headers.
set(includes
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/../at_commands/include"
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/../../src"
  "${ZEPHYR_BASE}/../nrfxlib/nrf_modem/include"
  "${ZEPHYR_BASE}/include"
  "${ZEPHYR_BASE}/lib/libc/common/include"
  "${PROJECT_SOURCE_DIR}/../stubs"
)

target_include_directories(app PRIVATE ${includes})
