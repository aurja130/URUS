# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

add_library(nts_core STATIC
    nts/cmfd/cmfd_system.cpp
    nts/common/build_info.cpp
    nts/common/diagnostic.cpp
    nts/common/floating_point.cpp
    nts/common/invariant.cpp
    nts/common/sha256.cpp
    nts/io/hdf5_reading.cpp
    nts/io/nodal_library.cpp
    nts/io/output.cpp
    nts/io/problem.cpp
    nts/solve/eigenvalue.cpp
    nts/solve/steady_state.cpp
    nts/xs/cross_section_model.cpp
)
target_include_directories(nts_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(nts_core PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(nts_core PUBLIC Eigen3::Eigen)
target_link_libraries(nts_core PRIVATE nts_warnings hdf5::hdf5_cpp hdf5::hdf5)
add_dependencies(nts_core nts_build_info)
