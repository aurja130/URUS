# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

add_executable(urus urus.cpp)
target_link_libraries(urus PRIVATE nts_core nts_warnings)
