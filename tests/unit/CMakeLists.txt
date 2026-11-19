# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

find_package(GTest 1.17 REQUIRED CONFIG)

add_executable(nts_unit_tests
    main.cpp
    common/diagnostic_test.cpp
    common/floating_point_test.cpp
    common/invariant_test.cpp
    io/library_fixture.cpp
    io/nodal_library_test.cpp
)
target_link_libraries(nts_unit_tests
    PRIVATE nts_core GTest::gtest nts_warnings hdf5::hdf5_cpp hdf5::hdf5)

include(GoogleTest)
gtest_discover_tests(nts_unit_tests DISCOVERY_MODE PRE_TEST)
