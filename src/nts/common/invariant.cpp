// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/invariant.hpp"

#include <cstdio> // IWYU pragma: keep (stderr)
#include <cstdlib>
#include <print>
#include <source_location>
#include <string_view>

namespace nts::detail {

void fail_invariant(std::string_view expression, std::string_view message,
                    const std::source_location& loc) noexcept {
    // A throwing print must not turn a failed invariant into an exception that a caller's
    // catch could swallow: the program aborts either way.
    try {
        std::print(stderr, "invariant check failed: {} - {} [{}:{} in {}]\n", expression, message,
                   loc.file_name(), loc.line(), loc.function_name());
    } catch (...) {
        std::abort();
    }
    std::abort();
}

} // namespace nts::detail
