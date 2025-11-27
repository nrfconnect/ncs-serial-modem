#
# Copyright (c) 2025 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Make sure the directory and file exists
set(GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/cmake/write_sm_version_header.cmake")
  message(FATAL_ERROR "Version header generation script not found")
endif()

# Always regenerate sm_version.h at every build
add_custom_target(
    generate_version_header ALL
    COMMAND ${CMAKE_COMMAND}
            -D OUTPUT_FILE=${GENERATED_DIR}/sm_version.h
            -P ${PROJECT_SOURCE_DIR}/cmake/write_sm_version_header.cmake
    BYPRODUCTS ${GENERATED_DIR}/sm_version.h
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Regenerating sm_version.h"
)

# Add include directory for the generated header
zephyr_include_directories("${GENERATED_DIR}")
