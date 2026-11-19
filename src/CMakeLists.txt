# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

add_library(nts_core STATIC
    nts/common/diagnostic.cpp
    nts/common/floating_point.cpp
    nts/common/invariant.cpp
    nts/io/hdf5_reading.cpp
    nts/io/nodal_library.cpp
    nts/io/problem.cpp
    nts/xs/cross_section_model.cpp
)
target_include_directories(nts_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(nts_core PRIVATE nts_warnings hdf5::hdf5_cpp hdf5::hdf5)
