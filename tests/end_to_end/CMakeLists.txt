# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

find_package(GTest 1.17 REQUIRED CONFIG)

add_executable(nts_end_to_end_tests
    ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
    present_library_test.cpp
)
target_compile_definitions(nts_end_to_end_tests PRIVATE
    NTS_PRESENT_LIBRARY="${PROJECT_SOURCE_DIR}/external/BEAVRS_minimal/beavrs_kinetics_library.hdf5")
target_link_libraries(nts_end_to_end_tests PRIVATE nts_core GTest::gtest nts_warnings)

include(GoogleTest)
gtest_discover_tests(nts_end_to_end_tests DISCOVERY_MODE PRE_TEST)
