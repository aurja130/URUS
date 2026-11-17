// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/floating_point.hpp"

#include "nts/common/invariant.hpp"

#include <fenv.h> // NOLINT(modernize-deprecated-headers): feenableexcept is a glibc extension.

namespace nts {

void configure_floating_point_environment() {
    if constexpr (floating_point_traps_enabled) {
        const int previous = feenableexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);
        NTS_ASSERT(previous != -1, "feenableexcept failed");
    }
}

} // namespace nts
