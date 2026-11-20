// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// The position-dependent rules of the problem input (docs/contracts/problem_input.md §3, §4, §6
// and §9) on a 3 x 3 x 3 box, which has interior, face, edge and corner positions. The problem
// tests use a 2 x 2 x 3 box, which has no interior position.

#include "nts/common/diagnostic.hpp"
#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "library_fixture.hpp"
#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::Diagnostics;
using nts::Problem;
using nts::test::all_text;
using nts::test::expect_one;
using nts::test::FixtureDataset;
using nts::test::FixtureSet;
using nts::test::FixtureType;
using nts::test::float64_array;
using nts::test::has_entry_diagnostic;
using nts::test::ProblemTest;

// The box: [nz][ny][nx] = [3][3][3]; position (i, j, k) is the element i + 3 j + 9 k.
constexpr std::size_t side = 3;
constexpr std::size_t position_count = side * side * side;
constexpr std::size_t grid_node_count = 27;
constexpr double excluded_value = -1.0;     // a composition_map value: no node there
constexpr double reflector_map_value = 1.0; // the map value of input composition 1
constexpr std::size_t reflector = 1;        // library composition 1 of "test_set"
constexpr std::size_t state_0 = 0;          // the state of the first fuel temperature and boron

// A position of the box, named for the test parameter.
struct Position {
    std::string_view name;
    std::size_t i;
    std::size_t j;
    std::size_t k;
};

constexpr std::array grid_positions{
    Position{.name = "Interior", .i = 1, .j = 1, .k = 1},
    Position{.name = "FaceCentre", .i = 1, .j = 1, .k = 0},
    Position{.name = "Edge", .i = 1, .j = 0, .k = 0},
    Position{.name = "Corner", .i = 0, .j = 0, .k = 0},
    Position{.name = "OppositeCorner", .i = 2, .j = 2, .k = 2},
};

// A node-state field of /initial_state (§6): its units, the rule its values hold (completing
// "is not <rule>" in a diagnostic), a valid value at every position and a value that breaks the
// rule.
struct NodeField {
    std::string_view name;
    std::string_view units;
    std::string_view rule;
    double valid;
    double breaking;
};

constexpr std::array node_fields{
    NodeField{.name = "doppler_temperature",
              .units = "K",
              .rule = "finite and positive",
              .valid = 600.0,
              .breaking = 0.0},
    NodeField{.name = "moderator_density",
              .units = "kg m^-3",
              .rule = "finite and positive",
              .valid = 700.0,
              .breaking = 0.0},
    NodeField{.name = "moderator_temperature",
              .units = "K",
              .rule = "finite and positive",
              .valid = 560.0,
              .breaking = 0.0},
    NodeField{.name = "boron",
              .units = "ppm",
              .rule = "finite and non-negative",
              .valid = 500.0,
              .breaking = -1.0},
};

// The index of a position in the row-major arrays of the box.
std::size_t index_of(const Position& position) {
    return position.i + (side * position.j) + (side * side * position.k);
}

// "position (i, j, k)", the entry of a position's diagnostic.
std::string entry_of(const Position& position) {
    return std::format("position ({}, {}, {})", position.i, position.j, position.k);
}

std::string state_location(const NodeField& field) {
    return std::format("/initial_state/{}", field.name);
}

// A [3][3][3] array of the field's valid value, with `value` at `index`.
FixtureDataset grid_state(const NodeField& field, std::size_t index, double value) {
    std::vector<double> values(position_count, field.valid);
    values[index] = value;
    return float64_array(std::move(values), {side, side, side}, std::string(field.units));
}

class GridProblemTest : public ProblemTest {
protected:
    // The valid problem on the 3 x 3 x 3 box: every position is fuel (map 0) and the exterior
    // face is kept.
    void SetUp() override {
        nts::test::FixtureGroup& geometry = problem().groups["reactor"].groups["geometry"];
        geometry.datasets["width_x"] = float64_array({10.0, 21.5, 10.0}, {side}, "cm");
        geometry.datasets["width_y"] = float64_array({15.0, 15.0, 12.0}, {side}, "cm");
        geometry.datasets["width_z"] = float64_array({20.0, 30.0, 20.0}, {side}, "cm");
        geometry.datasets["composition_map"] = FixtureDataset{
            .type = FixtureType::int32,
            .shape = {side, side, side},
            .numbers = std::vector<double>(position_count, 0.0),
            .strings = {},
            .attributes = {},
        };
    }

    void set_map(std::size_t index, double value) {
        problem().groups["reactor"].groups["geometry"].datasets["composition_map"].numbers[index] =
            value;
    }
};

class GridPositionTest : public GridProblemTest, public testing::WithParamInterface<Position> {};

TEST_P(GridPositionTest, RejectsAMapValueBelowMinusOne) {
    const Position& position = GetParam();
    set_map(index_of(position), -2.0);
    const Diagnostics diagnostics = rejected();
    expect_one(diagnostics, "/reactor/geometry/composition_map", "value -2 is below -1");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/geometry/composition_map",
                                     entry_of(position), "value -2 is below -1"))
        << all_text(diagnostics);
}

// K = 2 input compositions: the index 2 names none.
TEST_P(GridPositionTest, RejectsAMapValueAboveTheLastComposition) {
    const Position& position = GetParam();
    constexpr double first_out_of_range = 2.0;
    set_map(index_of(position), first_out_of_range);
    const Diagnostics diagnostics = rejected();
    expect_one(diagnostics, "/reactor/geometry/composition_map",
               "value 2 is not -1 or an input composition index below 2");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/geometry/composition_map",
                                     entry_of(position),
                                     "value 2 is not -1 or an input composition index below 2"))
        << all_text(diagnostics);
}

TEST_P(GridPositionTest, RejectsANodeStateThatBreaksItsRuleAtThePosition) {
    const Position& position = GetParam();
    std::map<std::string, FixtureDataset>& datasets = problem().groups["initial_state"].datasets;
    for (const NodeField& field : node_fields) {
        SCOPED_TRACE(field.name);
        const std::string name(field.name);
        const FixtureDataset valid = datasets.at(name);
        datasets[name] = grid_state(field, index_of(position), field.breaking);
        const Diagnostics diagnostics = rejected();
        expect_one(diagnostics, state_location(field), std::format("is not {}", field.rule));
        EXPECT_TRUE(has_entry_diagnostic(diagnostics, state_location(field), entry_of(position),
                                         std::format("is not {}", field.rule)))
            << all_text(diagnostics);
        datasets[name] = valid; // one defect per case
    }
}

// Values at excluded positions are never read, so the breaking value there is accepted; the
// box then has one node fewer.
TEST_P(GridPositionTest, AcceptsANodeStateBreakingItsRuleAtAnExcludedPosition) {
    const Position& position = GetParam();
    set_map(index_of(position), excluded_value);
    for (const NodeField& field : node_fields) {
        SCOPED_TRACE(field.name);
        problem().groups["initial_state"].datasets[std::string(field.name)] =
            grid_state(field, index_of(position), field.breaking);
        const Problem loaded = accepted();
        EXPECT_EQ(loaded.input.reactor.node_count(), grid_node_count - 1);
    }
}

// An excluded position makes the exterior face required (§4, §9).
TEST_P(GridPositionTest, RejectsAnExcludedPositionWithoutTheExteriorFace) {
    const Position& position = GetParam();
    set_map(index_of(position), excluded_value);
    problem().groups["reactor"].groups["boundary_conditions"].groups.erase("exterior");
    const Diagnostics diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().location, "/reactor/boundary_conditions/exterior");
    EXPECT_TRUE(diagnostics.front().entry.empty()) << all_text(diagnostics);
    EXPECT_TRUE(diagnostics.front().message.contains("group is missing")) << all_text(diagnostics);
}

// A used composition with an invalid entry is reported at the position it is used at (§5).
TEST_P(GridPositionTest, RejectsAUsedReflectorWithAnInvalidEntry) {
    const Position& position = GetParam();
    set_map(index_of(position), reflector_map_value);
    FixtureSet& set = library().sets.front();
    set.valid[(reflector * set.state_count()) + state_0] = 0;
    const Diagnostics diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().location, "/composition_sets/test_set/valid");
    EXPECT_TRUE(diagnostics.front().message.contains("entry is invalid (valid = 0)"))
        << all_text(diagnostics);
    EXPECT_TRUE(diagnostics.front().entry.contains("reflector")) << all_text(diagnostics);
}

INSTANTIATE_TEST_SUITE_P(Positions, GridPositionTest, testing::ValuesIn(grid_positions),
                         [](const testing::TestParamInfo<Position>& param_info) {
                             return std::string(param_info.param.name);
                         });

// Every position of the box is a node: 27 nodes, none excluded.
TEST_F(GridProblemTest, AcceptsTheValidGridWithEveryPositionANode) {
    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.reactor.node_count(), grid_node_count);
}

} // namespace
