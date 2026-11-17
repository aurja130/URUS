# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

add_library(nts_core STATIC
    nts/common/floating_point.cpp
    nts/common/invariant.cpp
)
target_include_directories(nts_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(nts_core PUBLIC "NTS_FLOATING_POINT_TRAPS=$<BOOL:${NTS_FLOATING_POINT_TRAPS}>")
target_link_libraries(nts_core PRIVATE nts_warnings)
