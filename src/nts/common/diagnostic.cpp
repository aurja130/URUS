// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/diagnostic.hpp"

#include <string>

namespace nts {

std::string to_string(const Diagnostic& diagnostic) {
    const std::string prefix = diagnostic.file + ": " + diagnostic.location + ": ";
    if (diagnostic.entry.empty()) {
        return prefix + diagnostic.message;
    }
    return prefix + diagnostic.entry + ": " + diagnostic.message;
}

} // namespace nts
