// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/build_info.hpp"

// Generated at build time by cmake/write_build_info.cmake into the build directory.
#include "nts/common/build_info_values.hpp"

namespace nts {
namespace {

constexpr BuildInfo build_info_value{
    .engine_version = build_values::engine_version,
    .engine_commit = build_values::engine_commit,
    .build_preset = build_values::build_preset,
    .build_type = build_values::build_type,
};

} // namespace

const BuildInfo& build_info() {
    return build_info_value;
}

} // namespace nts
