// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// The present library, rewritten to the library contract from the full-core OpenMC runs, read
// by the engine: plan 1.2 acceptance (7650 compositions, the invalid-entry counts of TM §19.3,
// all in the 32 corner columns of every plane).

#include "nts/common/diagnostic.hpp"
#include "nts/io/nodal_library.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace {

constexpr std::size_t mesh_x = 15;
constexpr std::size_t mesh_y = 15;
constexpr std::size_t mesh_z = 17;
constexpr std::size_t nodes = mesh_x * mesh_y * mesh_z; // 3825 per rod state
constexpr std::size_t rod_states = 2;

std::string all_text(const nts::Diagnostics& diagnostics) {
    std::string text;
    for (const auto& diagnostic : diagnostics) {
        text += nts::to_string(diagnostic) + "\n";
    }
    return text;
}

// Read once for all tests.
const std::expected<nts::NodalLibrary, nts::Diagnostics>& present_library() {
    static const auto library = nts::read_nodal_library(NTS_PRESENT_LIBRARY);
    return library;
}

class PresentLibrary : public testing::Test {
protected:
    void SetUp() override {
        const auto& read = present_library();
        ASSERT_TRUE(read.has_value()) << all_text(read.error());
        ASSERT_EQ(read->composition_sets.size(), 1U);
        library_ = &*read;
    }

    [[nodiscard]] const nts::NodalLibrary& library() const { return *library_; }
    [[nodiscard]] const nts::CompositionSet& set() const {
        return library_->composition_sets.front();
    }

private:
    const nts::NodalLibrary* library_ = nullptr;
};

// The 32 positions of each 15 x 15 plane outside the core's circular boundary (TM §19.3).
bool is_corner_column(std::size_t x, std::size_t y) {
    const std::size_t edge_y = y < mesh_y / 2 ? y : mesh_y - 1 - y; // 0 on the outer rows
    const std::size_t edge_x = x < mesh_x / 2 ? x : mesh_x - 1 - x;
    switch (edge_y) {
    case 0:
        return edge_x <= 3;
    case 1:
        return edge_x <= 1;
    case 2:
    case 3:
        return edge_x == 0;
    default:
        return false;
    }
}

// Composition c = r * 3825 + x + 15 y + 225 z (library contract §7.3).
bool is_corner_composition(std::size_t composition) {
    const std::size_t node = composition % nodes;
    return is_corner_column(node % mesh_x, (node / mesh_x) % mesh_y);
}

TEST(CornerColumns, AreThirtyTwoPerPlaneAndSymmetric) {
    std::size_t corners = 0;
    for (std::size_t y = 0; y < mesh_y; ++y) {
        for (std::size_t x = 0; x < mesh_x; ++x) {
            corners += is_corner_column(x, y) ? 1U : 0U;
            EXPECT_EQ(is_corner_column(x, y), is_corner_column(y, x));
        }
    }
    EXPECT_EQ(corners, 32U);
}

TEST_F(PresentLibrary, HoldsOneSetOfEveryNodeInBothRodStates) {
    EXPECT_EQ(set().name, "beavrs_full_core");
    ASSERT_EQ(set().composition_count(), rod_states * nodes);
    const auto& names = set().composition_names;
    EXPECT_EQ(names.front(), "unrodded_x00_y00_z00");
    EXPECT_EQ(names.at(1 + (2 * mesh_x) + (3 * mesh_x * mesh_y)), "unrodded_x01_y02_z03");
    EXPECT_EQ(names.at(nodes), "rodded_x00_y00_z00");
    EXPECT_EQ(names.back(), "rodded_x14_y14_z16");
    EXPECT_EQ(library().format_version, 1);
    EXPECT_EQ(library().group_boundary_ev, 0.625);

    const auto& axes = set().axes;
    ASSERT_EQ(axes.size(), 3U);
    EXPECT_EQ(axes[0].axis, nts::StateAxis::fuel_temperature);
    EXPECT_EQ(axes[0].points, (std::vector<double>{293.0, 600.0, 900.0, 1200.0, 1500.0, 2000.0}));
    EXPECT_EQ(axes[1].axis, nts::StateAxis::moderator_density);
    EXPECT_EQ(axes[1].points.size(), 6U);
    EXPECT_EQ(axes[2].axis, nts::StateAxis::boron);
    EXPECT_EQ(axes[2].points, std::vector<double>{975.0});
    EXPECT_EQ(set().state_count, 36U);
}

// Entries (composition, branch state) whose value breaks the entry rule, per quantity, over
// the 7650 compositions and 36 branch states. D and Sigma_a: TM §19.3 (46, 79, 41, 77). The
// effective down-scatter breaks in the 220 entries where TM §19.3 found the raw Sigma_12 zero
// (41 of them NaN, as the fast flux is 0) and in 2 more corner entries where the Eq. (3.5)
// correction makes it negative, each well within its standard deviation. 1/v is zero exactly
// where Sigma_a is. Every other quantity obeys its rule everywhere; Sigma_f and the
// discontinuity factors are absent.
constexpr std::array<std::size_t, nts::quantity_count> expected_violations{
    46,  // D1
    79,  // D2
    41,  // Sigma_a1
    77,  // Sigma_a2
    222, // Sigma_s12 (effective)
    0,   // Sigma_s21
    0,   // nu_Sigma_f1
    0,   // nu_Sigma_f2
    0,   // kappa_Sigma_f1
    0,   // kappa_Sigma_f2
    41,  // inv_v1
    77,  // inv_v2
    0,   // beta
    0,   // lambda
    0,   // Sigma_f1
    0,   // Sigma_f2
    0,   // discontinuity factors
};

TEST_F(PresentLibrary, BrokenEntriesMatchTheTheoryManualCounts) {
    const auto counts = nts::count_rule_violations(set());
    for (const auto& spec : nts::quantity_specs()) {
        const auto index = static_cast<std::size_t>(spec.quantity);
        EXPECT_EQ(counts.at(index), expected_violations.at(index)) << spec.name;
    }
}

// Every invalid entry is in a corner column and breaks a rule: the writer marks no entry
// without cause. 227 entries are the union of the counts above.
TEST_F(PresentLibrary, EveryInvalidEntryIsInACornerColumnAndBreaksARule) {
    const nts::CompositionSet& core = set();
    std::size_t invalid = 0;
    for (std::size_t c = 0; c < core.composition_count(); ++c) {
        for (std::size_t s = 0; s < core.state_count; ++s) {
            if (core.valid.at((c * core.state_count) + s) != 0) {
                continue;
            }
            ++invalid;
            EXPECT_TRUE(is_corner_composition(c)) << nts::describe_entry(core, c, s);
            const bool breaks =
                std::ranges::any_of(nts::quantity_specs(), [&](const nts::QuantitySpec& spec) {
                    return nts::breaks_entry_rule(core, spec.quantity, c, s);
                });
            EXPECT_TRUE(breaks) << nts::describe_entry(core, c, s);
        }
    }
    EXPECT_EQ(invalid, 227U);
}

TEST_F(PresentLibrary, EveryFuelNodeIsUsable) {
    const nts::CompositionSet& core = set();
    std::vector<std::size_t> fuel;
    for (std::size_t c = 0; c < core.composition_count(); ++c) {
        if (!is_corner_composition(c)) {
            fuel.push_back(c);
        }
    }
    EXPECT_EQ(fuel.size(), rod_states * mesh_z * ((mesh_x * mesh_y) - 32));
    const auto diagnostics = nts::check_compositions_usable(library(), core, fuel);
    EXPECT_TRUE(diagnostics.empty()) << all_text(diagnostics);
}

} // namespace
