// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::KernelChoice;
using nts::NodeState;
using nts::OperatingState;
using nts::Problem;
using nts::SolverOptions;
using nts::test::all_text;
using nts::test::expect_one;
using nts::test::FixtureDataset;
using nts::test::FixtureType;
using nts::test::float64_array;
using nts::test::float64_scalar;
using nts::test::has_diagnostic;
using nts::test::has_entry_diagnostic;
using nts::test::integer_scalar;
using nts::test::ProblemTest;
using nts::test::string_scalar;

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity_value = std::numeric_limits<double>::infinity();

// The grid of the valid problem, [nz][ny][nx] = [2][2][3]: position p = i + 3 j + 6 k. The excluded
// positions (map value -1) are 0 = (0, 0, 0) and 6 = (0, 0, 1); the nodes are the other ten
// positions, in position order.
constexpr std::size_t position_count = 12;
constexpr std::array<std::size_t, 2> excluded_positions{0, 6};
constexpr std::array<std::size_t, 10> node_positions{1, 2, 3, 4, 5, 7, 8, 9, 10, 11};

// A node-state field of /operating_state (§6): its units, the rule its values hold (completing
// "is not <rule>" in a diagnostic), the NodeState member it fills, the base of an array (the value
// at position p is base + p) and a scalar that breaks the rule.
struct NodeField {
    std::string_view name;
    std::string_view units;
    std::string_view rule;
    double NodeState::* member;
    double base;
    double breaking;
};

constexpr std::array node_fields{
    NodeField{.name = "doppler_temperature",
              .units = "K",
              .rule = "finite and positive",
              .member = &NodeState::doppler_temperature_k,
              .base = 600.0,
              .breaking = 0.0},
    NodeField{.name = "moderator_density",
              .units = "kg m^-3",
              .rule = "finite and positive",
              .member = &NodeState::moderator_density_kg_m3,
              .base = 700.0,
              .breaking = 0.0},
    NodeField{.name = "moderator_temperature",
              .units = "K",
              .rule = "finite and positive",
              .member = &NodeState::moderator_temperature_k,
              .base = 560.0,
              .breaking = 0.0},
    NodeField{.name = "boron",
              .units = "ppm",
              .rule = "finite and non-negative",
              .member = &NodeState::boron_ppm,
              .base = 500.0,
              .breaking = -1.0},
};

// A float64 solver option (§7) and the rule its value holds.
struct FloatOption {
    std::string_view name;
    std::string_view rule;
};

constexpr std::array float_options{
    FloatOption{.name = "kernel_switch_threshold", .rule = "finite and non-negative"},
    FloatOption{.name = "wielandt_shift", .rule = "finite and positive"},
    FloatOption{.name = "eigenvalue_tolerance", .rule = "finite and positive"},
    FloatOption{.name = "fission_source_l2_tolerance", .rule = "finite and positive"},
    FloatOption{.name = "fission_source_linf_tolerance", .rule = "finite and positive"},
    FloatOption{.name = "inner_reduction", .rule = "in (0, 1)"},
    FloatOption{.name = "coupling_relaxation", .rule = "in (0, 1]"},
};

constexpr std::array<std::string_view, 3> tolerance_options{
    "eigenvalue_tolerance", "fission_source_l2_tolerance", "fission_source_linf_tolerance"};
constexpr std::array<std::string_view, 3> iteration_limit_options{
    "inner_iteration_limit", "outer_iteration_limit", "nodal_update_period"};
constexpr std::array<std::string_view, 4> integer_options{
    "inner_iteration_limit", "outer_iteration_limit", "nodal_update_period", "adjoint"};

struct KernelName {
    std::string_view name;
    KernelChoice choice;
};

constexpr std::array kernel_names{
    KernelName{.name = "finite_difference", .choice = KernelChoice::finite_difference},
    KernelName{.name = "nem", .choice = KernelChoice::nem},
    KernelName{.name = "anm", .choice = KernelChoice::anm},
    KernelName{.name = "hybrid", .choice = KernelChoice::hybrid},
};

// The location of a node-state field.
std::string state_location(const NodeField& field) {
    return std::format("/operating_state/{}", field.name);
}

// The location of a solver option.
std::string option_location(std::string_view name) {
    return std::format("/solver_options/{}", name);
}

// The values of a [2][2][3] array: position p holds base + p; NaN at the excluded positions when
// `nan_at_excluded`.
std::vector<double> position_values(double base, bool nan_at_excluded = false) {
    std::vector<double> values;
    values.reserve(position_count);
    for (std::size_t p = 0; p < position_count; ++p) {
        values.push_back(base + static_cast<double>(p));
    }
    if (nan_at_excluded) {
        for (const std::size_t p : excluded_positions) {
            values[p] = not_a_number;
        }
    }
    return values;
}

// A [nz][ny][nx] = [2][2][3] array of `values` in `units`.
FixtureDataset position_array(std::vector<double> values, std::string_view units) {
    return float64_array(std::move(values), {2, 2, 3}, std::string(units));
}

// A solver option holding a float64 value. Solver options carry no units (§1).
FixtureDataset number(double value) {
    return FixtureDataset{.type = FixtureType::float64,
                          .shape = {},
                          .numbers = {value},
                          .strings = {},
                          .attributes = {}};
}

// Node n of an array state holds the base of the field plus the position index of node n.
void expect_array_states(const OperatingState& state, const NodeField& field) {
    ASSERT_EQ(state.node_states.size(), node_positions.size());
    for (std::size_t n = 0; n < node_positions.size(); ++n) {
        EXPECT_DOUBLE_EQ(state.node_states[n].*field.member,
                         field.base + static_cast<double>(node_positions[n]));
    }
}

// The defaults of contract §7, restated.
void expect_defaults(const SolverOptions& options) {
    EXPECT_EQ(options.kernel, KernelChoice::hybrid);
    EXPECT_DOUBLE_EQ(options.kernel_switch_threshold, 0.01);
    EXPECT_DOUBLE_EQ(options.wielandt_shift, 0.04);
    EXPECT_DOUBLE_EQ(options.eigenvalue_tolerance, 5e-6);
    EXPECT_DOUBLE_EQ(options.fission_source_l2_tolerance, 5e-5);
    EXPECT_DOUBLE_EQ(options.fission_source_linf_tolerance, 5e-4);
    EXPECT_DOUBLE_EQ(options.inner_reduction, 0.01);
    EXPECT_EQ(options.inner_iteration_limit, 1);
    EXPECT_EQ(options.outer_iteration_limit, 1000);
    EXPECT_EQ(options.nodal_update_period, 5);
    EXPECT_DOUBLE_EQ(options.coupling_relaxation, 1.0);
    EXPECT_FALSE(options.adjoint);
}

// ----- Check 8: /operating_state (§6) ------------------------------------------------------------

TEST_F(ProblemTest, ReadsScalarStatesAtEveryNode) {
    const Problem loaded = accepted();
    const OperatingState& state = loaded.input.operating_state;
    EXPECT_DOUBLE_EQ(state.thermal_power_w, 1.0e6);
    ASSERT_EQ(state.node_states.size(), node_positions.size());
    for (const NodeState& node : state.node_states) {
        EXPECT_DOUBLE_EQ(node.doppler_temperature_k, 700.0);
        EXPECT_DOUBLE_EQ(node.moderator_density_kg_m3, 700.0);
        EXPECT_DOUBLE_EQ(node.moderator_temperature_k, 560.0);
        EXPECT_DOUBLE_EQ(node.boron_ppm, 500.0);
    }
}

TEST_F(ProblemTest, RejectsAMissingThermalPower) {
    problem().groups["operating_state"].datasets.erase("thermal_power");
    expect_one(rejected(), "/operating_state/thermal_power", "dataset is missing");
}

TEST_F(ProblemTest, RejectsAThermalPowerThatIsNotAScalar) {
    auto& datasets = problem().groups["operating_state"].datasets;
    datasets["thermal_power"] = float64_array({1.0e6}, {1}, "W");
    expect_one(rejected(), "/operating_state/thermal_power", "is not a scalar");
}

TEST_F(ProblemTest, RejectsAThermalPowerInTheWrongUnits) {
    auto& datasets = problem().groups["operating_state"].datasets;
    datasets["thermal_power"] = float64_scalar(1.0e6, "MW");
    expect_one(rejected(), "/operating_state/thermal_power@units", "are not the accepted 'W'");
}

TEST_F(ProblemTest, RejectsAThermalPowerThatIsNotFloat64) {
    auto& datasets = problem().groups["operating_state"].datasets;
    FixtureDataset power = float64_scalar(1.0e6, "W");
    power.type = FixtureType::float32;
    datasets["thermal_power"] = power;
    expect_one(rejected(), "/operating_state/thermal_power", "not a float64 dataset");
}

TEST_F(ProblemTest, RejectsAThermalPowerThatIsNotFiniteAndPositive) {
    auto& datasets = problem().groups["operating_state"].datasets;
    for (const double value : {0.0, -1.0, not_a_number, infinity_value}) {
        SCOPED_TRACE(value);
        datasets["thermal_power"] = float64_scalar(value, "W");
        expect_one(rejected(), "/operating_state/thermal_power", "is not finite and positive");
    }
}

// Thermal power is in W and finite and positive: a wrong unit and a non-positive value are both
// reported, each once (§6).
TEST_F(ProblemTest, ReportsTheThermalPowerUnitsAndValueTogether) {
    problem().groups["operating_state"].datasets["thermal_power"] = float64_scalar(-1.0, "MW");
    const auto diagnostics = rejected();
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/operating_state/thermal_power@units",
                               "are not the accepted 'W'"))
        << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/operating_state/thermal_power",
                               "-1 W is not finite and positive"))
        << all_text(diagnostics);
}

// The node-state fields: each is checked on its own, so each runs as its own parameter.
class NodeStateField : public ProblemTest, public testing::WithParamInterface<NodeField> {};

TEST_P(NodeStateField, RejectsAMissingState) {
    const NodeField& field = GetParam();
    problem().groups["operating_state"].datasets.erase(std::string(field.name));
    expect_one(rejected(), state_location(field), "dataset is missing");
}

TEST_P(NodeStateField, RejectsAStateInTheWrongUnits) {
    const NodeField& field = GetParam();
    problem().groups["operating_state"].datasets[std::string(field.name)] =
        float64_scalar(field.base, "1");
    expect_one(rejected(), state_location(field) + "@units",
               std::format("are not the accepted '{}'", field.units));
}

TEST_P(NodeStateField, RejectsAStateThatIsNotFloat64) {
    const NodeField& field = GetParam();
    FixtureDataset value = float64_scalar(field.base, std::string(field.units));
    value.type = FixtureType::float32;
    problem().groups["operating_state"].datasets[std::string(field.name)] = value;
    expect_one(rejected(), state_location(field), "not a float64 dataset");
}

TEST_P(NodeStateField, RejectsAScalarThatBreaksItsRule) {
    const NodeField& field = GetParam();
    auto& datasets = problem().groups["operating_state"].datasets;
    for (const double value : {not_a_number, field.breaking}) {
        SCOPED_TRACE(value);
        datasets[std::string(field.name)] = float64_scalar(value, std::string(field.units));
        expect_one(rejected(), state_location(field), std::format("is not {}", field.rule));
    }
}

TEST_P(NodeStateField, RejectsAnArrayValueThatBreaksItsRuleAtANode) {
    const NodeField& field = GetParam();
    std::vector<double> values = position_values(field.base);
    values[4] = field.breaking; // position (1, 1, 0) = 1 + 3 * 1 + 6 * 0, a node
    problem().groups["operating_state"].datasets[std::string(field.name)] =
        position_array(std::move(values), field.units);
    const auto diagnostics = rejected();
    EXPECT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, state_location(field), "position (1, 1, 0)",
                                     std::format("is not {}", field.rule)))
        << all_text(diagnostics);
}

TEST_P(NodeStateField, RejectsAnArrayOfTheWrongShape) {
    const NodeField& field = GetParam();
    for (const std::vector<std::size_t>& shape :
         {std::vector<std::size_t>{2, 3, 2}, std::vector<std::size_t>{12}}) {
        SCOPED_TRACE(shape.size());
        problem().groups["operating_state"].datasets[std::string(field.name)] =
            float64_array(position_values(field.base), shape, std::string(field.units));
        expect_one(rejected(), state_location(field),
                   "is not a scalar or [nz][ny][nx] = [2, 2, 3]");
    }
}

// The units are checked even when the geometry is unusable; the shape is not, and the values are
// not read (§8).
TEST_P(NodeStateField, ChecksArrayUnitsWhileTheGeometryIsUnusable) {
    const NodeField& field = GetParam();
    problem().groups["geometry"].datasets["width_x"] = float64_array({10.0, -1.0, 10.0}, {3}, "cm");
    problem().groups["operating_state"].datasets[std::string(field.name)] =
        position_array(position_values(field.base), "1");
    const auto diagnostics = rejected();
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/width_x", "index 1",
                                     "width -1 cm is not finite and positive"))
        << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, state_location(field) + "@units",
                               std::format("are not the accepted '{}'", field.units)))
        << all_text(diagnostics);
}

// The map's values are checked only when the widths are usable; a bad map value makes the map
// unusable, yet the array's shape is still checked against the widths (§8).
TEST_P(NodeStateField, ChecksAnArrayShapeWhileOnlyTheMapIsUnusable) {
    const NodeField& field = GetParam();
    problem().groups["geometry"].datasets["composition_map"].numbers[0] = -2.0;
    problem().groups["operating_state"].datasets[std::string(field.name)] =
        float64_array(position_values(field.base), {3, 2, 2}, std::string(field.units));
    const auto diagnostics = rejected();
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/composition_map", "position (0, 0, 0)",
                                     "value -2 is below -1"))
        << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, state_location(field),
                               "shape [3, 2, 2] is not a scalar or [nz][ny][nx] = [2, 2, 3]"))
        << all_text(diagnostics);
}

TEST_P(NodeStateField, ReadsAnArrayInNodeOrder) {
    const NodeField& field = GetParam();
    problem().groups["operating_state"].datasets[std::string(field.name)] =
        position_array(position_values(field.base), field.units);
    const Problem loaded = accepted();
    expect_array_states(loaded.input.operating_state, field);
}

// Values at excluded positions are never read, so NaN there is accepted (§6).
TEST_P(NodeStateField, AcceptsAnArrayWhoseExcludedValuesAreNotRead) {
    const NodeField& field = GetParam();
    problem().groups["operating_state"].datasets[std::string(field.name)] =
        position_array(position_values(field.base, true), field.units);
    const Problem loaded = accepted();
    expect_array_states(loaded.input.operating_state, field);
}

INSTANTIATE_TEST_SUITE_P(OperatingState, NodeStateField, testing::ValuesIn(node_fields),
                         [](const testing::TestParamInfo<NodeField>& param_info) {
                             return std::string(param_info.param.name);
                         });

// A node state outside the library's table is not an input error: the cross-section model
// clamps and counts it (§6).
TEST_F(ProblemTest, AcceptsNodeStatesOutsideTheLibraryTable) {
    auto& datasets = problem().groups["operating_state"].datasets;
    datasets["doppler_temperature"] = float64_scalar(2000.0, "K");
    datasets["boron"] = float64_scalar(3000.0, "ppm");
    const Problem loaded = accepted();
    ASSERT_EQ(loaded.input.operating_state.node_states.size(), node_positions.size());
    for (const NodeState& node : loaded.input.operating_state.node_states) {
        EXPECT_DOUBLE_EQ(node.doppler_temperature_k, 2000.0);
        EXPECT_DOUBLE_EQ(node.boron_ppm, 3000.0);
    }
}

// Boron is finite and non-negative: zero is the lower bound and is accepted.
TEST_F(ProblemTest, AcceptsBoronAtZero) {
    problem().groups["operating_state"].datasets["boron"] = float64_scalar(0.0, "ppm");
    const Problem loaded = accepted();
    for (const NodeState& node : loaded.input.operating_state.node_states) {
        EXPECT_DOUBLE_EQ(node.boron_ppm, 0.0);
    }
}

// Without usable widths the map's dimensions are unknown: the array's shape and values are not
// checked (its units still are, §8), so only the width is reported here.
TEST_F(ProblemTest, RejectsNoArrayStateWhileTheGeometryIsUnusable) {
    problem().groups["geometry"].datasets["width_x"] = float64_array({10.0, -1.0, 10.0}, {3}, "cm");
    problem().groups["operating_state"].datasets["doppler_temperature"] =
        position_array(std::vector<double>(position_count, -1.0), "K");
    expect_one(rejected(), "/geometry/width_x", "is not finite and positive");
}

TEST_F(ProblemTest, RejectsAnUnknownOperatingStateMember) {
    problem().groups["operating_state"].datasets["extra"] = float64_scalar(1.0, "K");
    expect_one(rejected(), "/operating_state/extra", "not a member of the problem input contract");
}

// ----- Check 9: /solver_options (§7) -------------------------------------------------------------

class SolverOptionsTest : public ProblemTest {
protected:
    void set_option(std::string_view name, FixtureDataset value) {
        problem().groups["solver_options"].datasets[std::string(name)] = std::move(value);
    }

    void clear_option(std::string_view name) {
        problem().groups["solver_options"].datasets.erase(std::string(name));
    }
};

TEST_F(SolverOptionsTest, AcceptsAnAbsentSolverOptionsGroupWithTheDefaults) {
    const Problem loaded = accepted();
    expect_defaults(loaded.input.solver_options);
}

TEST_F(SolverOptionsTest, AcceptsAnEmptySolverOptionsGroupWithTheDefaults) {
    problem().groups.try_emplace("solver_options");
    const Problem loaded = accepted();
    expect_defaults(loaded.input.solver_options);
}

TEST_F(SolverOptionsTest, ReadsEveryOptionSetToAValidValue) {
    set_option("kernel", string_scalar("nem"));
    set_option("kernel_switch_threshold", number(0.0));
    set_option("wielandt_shift", number(0.1));
    set_option("eigenvalue_tolerance", number(1e-7));
    set_option("fission_source_l2_tolerance", number(1e-6));
    set_option("fission_source_linf_tolerance", number(1e-5));
    set_option("inner_reduction", number(0.5));
    set_option("inner_iteration_limit", integer_scalar(3));
    set_option("outer_iteration_limit", integer_scalar(50));
    set_option("nodal_update_period", integer_scalar(1));
    set_option("coupling_relaxation", number(1.0));
    set_option("adjoint", integer_scalar(1));

    const Problem loaded = accepted();
    const SolverOptions& options = loaded.input.solver_options;
    EXPECT_EQ(options.kernel, KernelChoice::nem);
    EXPECT_DOUBLE_EQ(options.kernel_switch_threshold, 0.0);
    EXPECT_DOUBLE_EQ(options.wielandt_shift, 0.1);
    EXPECT_DOUBLE_EQ(options.eigenvalue_tolerance, 1e-7);
    EXPECT_DOUBLE_EQ(options.fission_source_l2_tolerance, 1e-6);
    EXPECT_DOUBLE_EQ(options.fission_source_linf_tolerance, 1e-5);
    EXPECT_DOUBLE_EQ(options.inner_reduction, 0.5);
    EXPECT_EQ(options.inner_iteration_limit, 3);
    EXPECT_EQ(options.outer_iteration_limit, 50);
    EXPECT_EQ(options.nodal_update_period, 1);
    EXPECT_DOUBLE_EQ(options.coupling_relaxation, 1.0);
    EXPECT_TRUE(options.adjoint);
}

TEST_F(SolverOptionsTest, ReadsEachKernelName) {
    for (const KernelName& kernel : kernel_names) {
        SCOPED_TRACE(kernel.name);
        set_option("kernel", string_scalar(std::string(kernel.name)));
        const Problem loaded = accepted();
        EXPECT_EQ(loaded.input.solver_options.kernel, kernel.choice);
    }
}

// Solver options carry no units (§1), so a float option without a `units` attribute is accepted.
TEST_F(SolverOptionsTest, AcceptsAFloatOptionWithoutAUnitsAttribute) {
    set_option("wielandt_shift", number(0.1));
    const Problem loaded = accepted();
    EXPECT_DOUBLE_EQ(loaded.input.solver_options.wielandt_shift, 0.1);
}

TEST_F(SolverOptionsTest, RejectsAnUnknownKernel) {
    set_option("kernel", string_scalar("magic"));
    expect_one(rejected(), "/solver_options/kernel", "unknown kernel");
}

TEST_F(SolverOptionsTest, RejectsANegativeKernelSwitchThreshold) {
    set_option("kernel_switch_threshold", number(-1e-9));
    expect_one(rejected(), "/solver_options/kernel_switch_threshold",
               "is not finite and non-negative");
}

TEST_F(SolverOptionsTest, RejectsAZeroWielandtShift) {
    set_option("wielandt_shift", number(0.0));
    expect_one(rejected(), "/solver_options/wielandt_shift", "is not finite and positive");
}

TEST_F(SolverOptionsTest, RejectsANonPositiveTolerance) {
    for (const std::string_view name : tolerance_options) {
        SCOPED_TRACE(name);
        set_option(name, number(0.0));
        expect_one(rejected(), option_location(name), "is not finite and positive");
        clear_option(name);
    }
}

// inner_reduction is an open interval: both ends are rejected.
TEST_F(SolverOptionsTest, RejectsAnInnerReductionOutsideTheOpenInterval) {
    for (const double value : {0.0, 1.0}) {
        SCOPED_TRACE(value);
        set_option("inner_reduction", number(value));
        expect_one(rejected(), "/solver_options/inner_reduction", "is not in (0, 1)");
        clear_option("inner_reduction");
    }
}

// coupling_relaxation is half-open: 1 is accepted, 0 and anything above 1 are rejected.
TEST_F(SolverOptionsTest, RejectsACouplingRelaxationOutsideTheHalfOpenInterval) {
    for (const double value : {0.0, 1.0000001}) {
        SCOPED_TRACE(value);
        set_option("coupling_relaxation", number(value));
        expect_one(rejected(), "/solver_options/coupling_relaxation", "is not in (0, 1]");
        clear_option("coupling_relaxation");
    }
}

TEST_F(SolverOptionsTest, RejectsANonFiniteFloatOption) {
    for (const FloatOption& option : float_options) {
        SCOPED_TRACE(option.name);
        set_option(option.name, number(not_a_number));
        expect_one(rejected(), option_location(option.name), std::format("is not {}", option.rule));
        clear_option(option.name);
    }
}

TEST_F(SolverOptionsTest, RejectsAnIterationLimitOrPeriodBelowOne) {
    for (const std::string_view name : iteration_limit_options) {
        SCOPED_TRACE(name);
        set_option(name, integer_scalar(0));
        expect_one(rejected(), option_location(name), "is not at least 1");
        clear_option(name);
    }
}

TEST_F(SolverOptionsTest, RejectsAnAdjointOtherThanZeroOrOne) {
    set_option("adjoint", integer_scalar(2));
    expect_one(rejected(), "/solver_options/adjoint", "is not 0 or 1");
}

TEST_F(SolverOptionsTest, RejectsAFloatOptionThatIsNotFloat64) {
    for (const FloatOption& option : float_options) {
        SCOPED_TRACE(option.name);
        set_option(option.name, integer_scalar(1));
        expect_one(rejected(), option_location(option.name), "not a float64 dataset");
        clear_option(option.name);
    }
}

TEST_F(SolverOptionsTest, RejectsAnIntegerOptionThatIsNotAnInteger) {
    for (const std::string_view name : integer_options) {
        SCOPED_TRACE(name);
        set_option(name, number(1.0));
        expect_one(rejected(), option_location(name), "not an integer dataset");
        clear_option(name);
    }
}

// The integer range is signed 64-bit: 2^63 is rejected, not wrapped (§1, §7).
TEST_F(SolverOptionsTest, RejectsAnIntegerOptionBeyondTheSignedRange) {
    set_option("outer_iteration_limit", FixtureDataset{.type = FixtureType::uint64,
                                                       .shape = {},
                                                       .numbers = {9223372036854775808.0},
                                                       .strings = {},
                                                       .attributes = {}});
    expect_one(rejected(), "/solver_options/outer_iteration_limit",
               "a value exceeds the signed 64-bit range");
}

TEST_F(SolverOptionsTest, RejectsAnOptionThatIsAOneElementArray) {
    for (const FloatOption& option : float_options) {
        SCOPED_TRACE(option.name);
        set_option(option.name, FixtureDataset{.type = FixtureType::float64,
                                               .shape = {1},
                                               .numbers = {1.0},
                                               .strings = {},
                                               .attributes = {}});
        expect_one(rejected(), option_location(option.name), "is not a scalar");
        clear_option(option.name);
    }
    for (const std::string_view name : integer_options) {
        SCOPED_TRACE(name);
        set_option(name, FixtureDataset{.type = FixtureType::int64,
                                        .shape = {1},
                                        .numbers = {1.0},
                                        .strings = {},
                                        .attributes = {}});
        expect_one(rejected(), option_location(name), "is not a scalar");
        clear_option(name);
    }
}

TEST_F(SolverOptionsTest, RejectsAKernelThatIsANumber) {
    set_option("kernel", number(1.0));
    expect_one(rejected(), "/solver_options/kernel", "not a string dataset");
}

TEST_F(SolverOptionsTest, RejectsAnUnknownSolverOption) {
    set_option("tolerance", number(1e-6));
    expect_one(rejected(), "/solver_options/tolerance",
               "not a member of the problem input contract");
}

} // namespace
