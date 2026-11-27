# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

find_package(GTest 1.17 REQUIRED CONFIG)

add_executable(nts_unit_tests
    main.cpp
    cmfd/cmfd_system_test.cpp
    common/diagnostic_test.cpp
    common/floating_point_test.cpp
    common/invariant_test.cpp
    common/sha256_test.cpp
    io/library_fixture.cpp
    io/nodal_library_test.cpp
    io/output_test.cpp
    io/problem_fixture.cpp
    io/problem_binding_test.cpp
    io/problem_document_test.cpp
    io/problem_geometry_test.cpp
    io/problem_position_test.cpp
    io/problem_state_test.cpp
    io/problem_test.cpp
    solve/core_fixture.cpp
    solve/eigenvalue_test.cpp
    solve/steady_state_test.cpp
    xs/cross_section_model_test.cpp
)
target_link_libraries(nts_unit_tests
    PRIVATE nts_core GTest::gtest nts_warnings hdf5::hdf5_cpp hdf5::hdf5)
target_include_directories(nts_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/unit/solve")

include(GoogleTest)
gtest_discover_tests(nts_unit_tests DISCOVERY_MODE PRE_TEST)
