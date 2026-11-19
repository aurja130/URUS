// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/diagnostic.hpp"

#include <gtest/gtest.h>

namespace {

TEST(Diagnostic, FormatsFileLocationEntryAndMessage) {
    const nts::Diagnostic diagnostic{.file = "lib.h5",
                                     .location = "/composition_sets/core/D1/value",
                                     .entry = "composition 0 'fuel'",
                                     .message = "value 0 breaks the rule > 0"};
    EXPECT_EQ(nts::to_string(diagnostic), "lib.h5: /composition_sets/core/D1/value: "
                                          "composition 0 'fuel': value 0 breaks the rule > 0");
}

TEST(Diagnostic, LeavesOutAnEmptyEntry) {
    const nts::Diagnostic diagnostic{
        .file = "lib.h5", .location = "/@format_version", .entry = "", .message = "version 2"};
    EXPECT_EQ(nts::to_string(diagnostic), "lib.h5: /@format_version: version 2");
}

} // namespace
