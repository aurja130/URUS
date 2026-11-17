// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

#ifndef NTS_FLOATING_POINT_TRAPS
#error "NTS_FLOATING_POINT_TRAPS must be defined by the build system"
#endif

namespace nts {

inline constexpr bool floating_point_traps_enabled = (NTS_FLOATING_POINT_TRAPS != 0);

// Makes invalid, divide-by-zero and overflow operations raise SIGFPE; no-op when traps are off.
void configure_floating_point_environment();

} // namespace nts
