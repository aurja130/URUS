# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

cmake_minimum_required(VERSION 3.28)

project(urus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NTS_FLOATING_POINT_TRAPS "Trap invalid, divide-by-zero and overflow operations" OFF)
set(NTS_SANITIZERS "" CACHE STRING "Comma-separated sanitizers, for example address,undefined")

add_library(nts_warnings INTERFACE)
target_compile_options(nts_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

if(NTS_SANITIZERS)
    add_compile_options("-fsanitize=${NTS_SANITIZERS}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${NTS_SANITIZERS}")
endif()

include(CTest)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
