// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// What the engine was built from (output contract §6, `provenance/`). The values come from a
// header generated at build time, rewritten only when one of them changes.

#include <string_view>

namespace nts {

struct BuildInfo {
    std::string_view engine_version; // CMake project version
    std::string_view engine_commit;  // git commit, "-dirty" appended for tracked changes; "unknown"
    std::string_view build_preset;   // CMake preset; "none" without one
    std::string_view build_type;     // CMake build type
};

[[nodiscard]] const BuildInfo& build_info();

} // namespace nts
