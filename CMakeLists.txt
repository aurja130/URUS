# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

cmake_minimum_required(VERSION 3.28)

project(urus VERSION 0.1.0 LANGUAGES CXX)

# The project uses no C++ modules. Scanning puts @modmap files into compile_commands.json,
# which exist only after a build.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(NTS_SANITIZERS "" CACHE STRING "Comma-separated sanitizers, for example address,undefined")

add_library(nts_warnings INTERFACE)
target_compile_options(nts_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

if(NTS_SANITIZERS)
    add_compile_options(
        "-fsanitize=${NTS_SANITIZERS}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${NTS_SANITIZERS}")
endif()

include(CTest)
find_package(HDF5 1.14 REQUIRED COMPONENTS C CXX)
find_package(Eigen3 5.0 REQUIRED CONFIG)

set(NTS_BUILD_PRESET "none" CACHE STRING "Name of the CMake preset that configured this build")
set(NTS_BUILD_INFO_HEADER "${CMAKE_BINARY_DIR}/generated/nts/common/build_info_values.hpp")
add_custom_target(nts_build_info
    COMMAND "${CMAKE_COMMAND}"
        -DNTS_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DNTS_OUTPUT_FILE=${NTS_BUILD_INFO_HEADER}
        -DNTS_ENGINE_VERSION=${PROJECT_VERSION}
        -DNTS_BUILD_PRESET=${NTS_BUILD_PRESET}
        -DNTS_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -P "${PROJECT_SOURCE_DIR}/cmake/write_build_info.cmake"
    BYPRODUCTS "${NTS_BUILD_INFO_HEADER}"
    VERBATIM)
add_subdirectory(src)
add_subdirectory(app)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
