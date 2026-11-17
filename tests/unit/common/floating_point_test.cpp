// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/floating_point.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <csignal>
#include <limits>

namespace {

double divide_zero_by_zero() {
    const volatile double numerator = 0.0;
    const volatile double denominator = 0.0;
    const volatile double result = numerator / denominator;
    return result;
}

double divide_one_by_zero() {
    const volatile double one = 1.0;
    const volatile double zero = 0.0;
    const volatile double result = one / zero;
    return result;
}

double overflow_max_times_two() {
    const volatile double largest = std::numeric_limits<double>::max();
    const volatile double result = largest * 2.0;
    return result;
}

void expect_trap_or_ieee_result(double (*operation)(), bool yields_nan) {
    if constexpr (nts::floating_point_traps_enabled) {
        EXPECT_EXIT(operation(), testing::KilledBySignal(SIGFPE), "");
    } else {
        const double result = operation();
        if (yields_nan) {
            EXPECT_TRUE(std::isnan(result));
        } else {
            EXPECT_TRUE(std::isinf(result) && result > 0.0);
        }
    }
}

} // namespace

TEST(FloatingPointTraps, DivideZeroByZeroTrapsOrYieldsNan) {
    expect_trap_or_ieee_result(divide_zero_by_zero, true);
}

TEST(FloatingPointTraps, DivideOneByZeroTrapsOrYieldsInfinity) {
    expect_trap_or_ieee_result(divide_one_by_zero, false);
}

TEST(FloatingPointTraps, OverflowTrapsOrYieldsInfinity) {
    expect_trap_or_ieee_result(overflow_max_times_two, false);
}
