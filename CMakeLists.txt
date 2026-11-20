# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.30...4.4)
project(fnirsi_dla_firmware C ASM)
include(cmake/BitoscopeBuildJobs.cmake)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(target_flags -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medlow)

add_subdirectory(mcu)

if(EXISTS ${CMAKE_SOURCE_DIR}/private/CMakeLists.txt)
  add_subdirectory(private)
endif()
