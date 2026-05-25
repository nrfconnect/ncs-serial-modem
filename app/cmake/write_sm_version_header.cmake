#
# Copyright (c) 2025 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Extract git describe string
execute_process(
  COMMAND git describe --tags --always --dirty
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GIT_RESULT
  ERROR_QUIET
)

# Fallback if git command fails
if(NOT GIT_RESULT EQUAL 0)
  set(GIT_DESCRIBE "unknown")
endif()

if(NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "OUTPUT_FILE must be defined")
endif()

set(SM_VERSION "${GIT_DESCRIBE}")

if(DEFINED VERSION_FILE)
  file(READ "${VERSION_FILE}" VERSION_CONTENT)
  if(VERSION_CONTENT MATCHES "VERSION_MAJOR = ([0-9]+)")
    set(VERSION_MAJOR ${CMAKE_MATCH_1})
  else()
    message(FATAL_ERROR "Could not parse VERSION_MAJOR from ${VERSION_FILE}")
  endif()
  if(VERSION_CONTENT MATCHES "VERSION_MINOR = ([0-9]+)")
    set(VERSION_MINOR ${CMAKE_MATCH_1})
  else()
    message(FATAL_ERROR "Could not parse VERSION_MINOR from ${VERSION_FILE}")
  endif()
  if(VERSION_CONTENT MATCHES "PATCHLEVEL = ([0-9]+)")
    set(PATCHLEVEL ${CMAKE_MATCH_1})
  else()
    message(FATAL_ERROR "Could not parse PATCHLEVEL from ${VERSION_FILE}")
  endif()

  # Append git describe suffix to v{major}.{minor}.{patch} from VERSION.
  # Drop the nearest-tag prefix; keep only distance/hash (or nothing on exact tag).
  set(GIT_SUFFIX "")
  # e.g. v2.0.0-preview1-67-g0b82ed3-dirty -> -67-g0b82ed3-dirty
  if(GIT_DESCRIBE MATCHES "-[0-9]+-g[0-9a-fA-F]+(-dirty)?$")
    string(REGEX MATCH "-[0-9]+-g[0-9a-fA-F]+(-dirty)?$" GIT_SUFFIX "${GIT_DESCRIBE}")
  # No tags: git describe --always -> 0b82ed3[-dirty] -> -g0b82ed3[-dirty]
  elseif(GIT_DESCRIBE MATCHES "^[0-9a-fA-F]+(-dirty)?$")
    set(GIT_SUFFIX "-g${GIT_DESCRIBE}")
  # Exact tag (e.g. v2.0.0) or unknown: GIT_SUFFIX stays empty -> v2.0.0
  endif()

  set(SM_VERSION "v${VERSION_MAJOR}.${VERSION_MINOR}.${PATCHLEVEL}${GIT_SUFFIX}")
endif()

set(HEADER_TEXT
"/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_VERSION_H
#define SM_VERSION_H

#define SM_VERSION \"${SM_VERSION}\"

#endif /* SM_VERSION_H */

")

file(WRITE "${OUTPUT_FILE}" "${HEADER_TEXT}")
