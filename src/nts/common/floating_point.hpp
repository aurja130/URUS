// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

namespace nts {

// Traps are on in every build without NDEBUG (Debug and the sanitizer presets) and off in release.
#ifdef NDEBUG
inline constexpr bool floating_point_traps_enabled = false;
#else
inline constexpr bool floating_point_traps_enabled = true;
#endif

// Makes invalid, divide-by-zero and overflow operations raise SIGFPE; no-op when traps are off.
void configure_floating_point_environment();

} // namespace nts
