// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/invariant.hpp"

#include <gtest/gtest.h>

#include <csignal>
#include <format>
#include <string>

namespace {

// The NTS_ASSERT in require_positive sits two lines below this constant; keep them adjacent.
constexpr int require_positive_assert_line = __LINE__ + 2;
void require_positive(int value) {
    NTS_ASSERT(value > 0, "value must be positive");
}

} // namespace

TEST(InvariantTest, FailureAbortsWithDiagnostic) {
    const std::string failure_pattern = std::format(
        R"(invariant check failed: value > 0 - value must be positive \[.*invariant_test\.cpp:{})"
        R"( in .*require_positive)",
        require_positive_assert_line);
    EXPECT_EXIT(require_positive(-1), testing::KilledBySignal(SIGABRT), failure_pattern.c_str());
}

TEST(InvariantTest, ConditionIsEvaluatedOnce) {
    int evaluations = 0;
    const auto evaluate = [&evaluations]() {
        ++evaluations;
        return evaluations;
    };
    NTS_ASSERT(evaluate() > 0, "condition holds");
    EXPECT_EQ(evaluations, 1);
}

TEST(InvariantTest, MessageIsNotEvaluatedWhenConditionHolds) {
    int message_evaluations = 0;
    const auto count_message = [&message_evaluations]() {
        ++message_evaluations;
        return "unused";
    };
    NTS_ASSERT(message_evaluations == 0, count_message());
    EXPECT_EQ(message_evaluations, 0);
}
