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

// The sanitizer runtimes call these hooks during startup. The floating-point trap must stop the
// program with SIGFPE, not be reported by a sanitizer, so the runtimes' SIGFPE handler is turned
// off here. This object is always linked: every main calls configure_floating_point_environment.
// The hooks only return a constant; no_sanitize keeps every sanitizer out of them. Their names
// and external linkage are fixed by the runtimes, hence the suppressed checks.
#pragma GCC visibility push(default)
// NOLINTBEGIN(bugprone-reserved-identifier,misc-use-internal-linkage,readability-identifier-naming)
extern "C" {

[[gnu::no_sanitize("address", "thread", "undefined")]] const char* __asan_default_options() {
    return "handle_sigfpe=0";
}

[[gnu::no_sanitize("address", "thread", "undefined")]] const char* __tsan_default_options() {
    return "handle_sigfpe=0";
}

[[gnu::no_sanitize("address", "thread", "undefined")]] const char* __ubsan_default_options() {
    return "handle_sigfpe=0";
}

} // extern "C"
// NOLINTEND(bugprone-reserved-identifier,misc-use-internal-linkage,readability-identifier-naming)
#pragma GCC visibility pop
