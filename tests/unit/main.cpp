// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/floating_point.hpp"

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    nts::configure_floating_point_environment();
    testing::InitGoogleTest(&argc, argv);
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    return RUN_ALL_TESTS();
}
