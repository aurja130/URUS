# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

find_package(GTest 1.17 REQUIRED CONFIG)

add_executable(nts_verification_tests
    ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
    ${PROJECT_SOURCE_DIR}/tests/unit/io/library_fixture.cpp
    ${PROJECT_SOURCE_DIR}/tests/unit/io/problem_fixture.cpp
    cross_section_interpolation_test.cpp
)
target_include_directories(nts_verification_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/io")
target_link_libraries(nts_verification_tests
    PRIVATE nts_core GTest::gtest nts_warnings hdf5::hdf5_cpp hdf5::hdf5)

include(GoogleTest)
gtest_discover_tests(nts_verification_tests DISCOVERY_MODE PRE_TEST)
