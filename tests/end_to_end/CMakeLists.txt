# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

find_package(GTest 1.17 REQUIRED CONFIG)
find_program(NTS_PYTHON_EXECUTABLE python3 REQUIRED)
find_program(NTS_SHA256SUM sha256sum REQUIRED)

add_executable(nts_end_to_end_tests
    ${PROJECT_SOURCE_DIR}/tests/unit/main.cpp
    ${PROJECT_SOURCE_DIR}/tests/unit/io/library_fixture.cpp
    present_library_test.cpp
    urus_test.cpp
)
target_include_directories(nts_end_to_end_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/io")
target_compile_definitions(nts_end_to_end_tests PRIVATE
    NTS_PRESENT_LIBRARY="${PROJECT_SOURCE_DIR}/external/BEAVRS_minimal/beavrs_kinetics_library.hdf5"
    NTS_URUS_EXECUTABLE="$<TARGET_FILE:urus>"
    NTS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    NTS_PYTHON_EXECUTABLE="${NTS_PYTHON_EXECUTABLE}"
    NTS_SHA256SUM="${NTS_SHA256SUM}")
target_link_libraries(nts_end_to_end_tests
    PRIVATE nts_core GTest::gtest nts_warnings hdf5::hdf5_cpp hdf5::hdf5)
add_dependencies(nts_end_to_end_tests urus)

include(GoogleTest)
gtest_discover_tests(nts_end_to_end_tests DISCOVERY_MODE PRE_TEST)
