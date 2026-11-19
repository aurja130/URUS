// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

#include <string>
#include <vector>

namespace nts {

// One input or data error, naming the file and the dataset or attribute it concerns
// (contract §5). `entry` names one entry of a dataset; it is empty where no entry applies.
struct Diagnostic {
    std::string file;
    std::string location;
    std::string entry;
    std::string message;
};

using Diagnostics = std::vector<Diagnostic>;

// "<file>: <location>: <entry>: <message>"; the entry part is omitted when empty.
[[nodiscard]] std::string to_string(const Diagnostic& diagnostic);

} // namespace nts
