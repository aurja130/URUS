// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

#include <source_location>
#include <string_view>

namespace nts::detail {

// Writes one line with expression, message, file, line and function to stderr, then aborts.
[[noreturn]] void
fail_invariant(std::string_view expression, std::string_view message,
               const std::source_location& loc = std::source_location::current()) noexcept;

} // namespace nts::detail

// The invariant check: guards conditions the code itself guarantees. Always enabled,
// independent of NDEBUG. `cond` is evaluated once; `msg` only on failure. Input and data
// errors are never asserted; they are reported as diagnostics through std::expected.
#define NTS_ASSERT(cond, msg) ((cond) ? void(0) : ::nts::detail::fail_invariant(#cond, (msg)))
