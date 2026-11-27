// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/diagnostic.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

#include "core_fixture.hpp"

namespace nts::test {
namespace {

// Slab of four nodes along x (fuel, fuel, reflector, reflector), vacuum on the x faces and
// reflective on y and z; every node 15 x 10 x 10 cm^3.
struct Slab {
    ReactorDescription reactor;
    std::vector<NodeCrossSections> xs;
};

[[nodiscard]] Slab make_slab() {
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::vacuum),     boundary(BoundaryKind::vacuum),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective)};
    Slab slab{.reactor = make_reactor(uniform_widths(60.0, 4), uniform_widths(10.0, 1),
                                      uniform_widths(10.0, 1), faces),
              .xs = {}};
    slab.xs = {example_fuel(), example_fuel(), example_reflector(), example_reflector()};
    return slab;
}

} // namespace

TEST(EigenvalueSolve, PowerSumsToTheThermalPowerAndFollowsVkappaPhi) {
    const Slab slab = make_slab();
    const double thermal_power_w = 2.5e6;
    const SteadyState state =
        solve_steady_state(slab.reactor, slab.xs, verification_options(), thermal_power_w);

    ASSERT_EQ(state.power_w.size(), slab.reactor.node_count());
    ASSERT_EQ(state.volume_cm3.size(), slab.reactor.node_count());
    double total_w = 0.0;
    for (double const power : state.power_w) {
        total_w += power;
    }
    EXPECT_NEAR(total_w, thermal_power_w, 1.0e-12 * thermal_power_w);

    // TM Eq. (3.10): P^m = V^m (κΣ_f1 φ_1 + κΣ_f2 φ_2), with the scaled flux.
    for (std::size_t node = 0; node < slab.reactor.node_count(); ++node) {
        const double want =
            state.volume_cm3[node] *
            ((slab.xs[node].kappa_fission_j_per_cm[0] * state.flux[2 * node]) +
             (slab.xs[node].kappa_fission_j_per_cm[1] * state.flux[(2 * node) + 1]));
        EXPECT_NEAR(state.power_w[node], want, 1.0e-12 * thermal_power_w);
        EXPECT_NEAR(state.volume_cm3[node], 1500.0, 1.0e-12 * 1500.0); // 15 x 10 x 10 cm
    }
    EXPECT_TRUE(state.convergence.converged);
}

TEST(EigenvalueSolve, PowerWeightsNodesByTheirVolumes) {
    // Two identical nodes of volumes 70 and 105 cm^3 in a reflective box: the flux is flat,
    // so the power per node is proportional to the volume (ratio 1.5).
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective)};
    const ReactorDescription reactor = make_reactor({7.0, 10.5}, {10.0}, {1.0}, faces);
    const std::vector<NodeCrossSections> xs(reactor.node_count(), example_fuel());

    const SteadyState state = solve_steady_state(reactor, xs, verification_options(), 1.0e5);
    EXPECT_NEAR(state.volume_cm3[0], 70.0, 1.0e-12 * 70.0);
    EXPECT_NEAR(state.volume_cm3[1], 105.0, 1.0e-12 * 105.0);
    EXPECT_NEAR(state.power_w[1] / state.power_w[0], 1.5, 1.0e-9);
    EXPECT_NEAR(state.power_w[0] + state.power_w[1], 1.0e5, 1.0e-12 * 1.0e5);
}

TEST(EigenvalueSolve, NonConvergedResultIsStillScaledToThePower) {
    const Slab slab = make_slab();
    SolverOptions options = verification_options();
    options.outer_iteration_limit = 2;
    const double thermal_power_w = 1.0e6;
    const SteadyState state = solve_steady_state(slab.reactor, slab.xs, options, thermal_power_w);

    EXPECT_FALSE(state.convergence.converged);
    double total_w = 0.0;
    for (double const power : state.power_w) {
        total_w += power;
    }
    EXPECT_NEAR(total_w, thermal_power_w, 1.0e-12 * thermal_power_w);
}

namespace {

ProblemInput in_place_input() {
    ProblemInput input;
    input.file = "problem.toml";
    return input;
}

// The settings document is a linked file; the other documents are in place.
ProblemInput linked_settings_input() {
    ProblemInput input = in_place_input();
    input.document_files[static_cast<std::size_t>(InputDocument::settings)] = "settings.toml";
    return input;
}

} // namespace

TEST(EigenvalueSolve, CheckSolvableAcceptsTheFiniteDifferenceEigenvalueProblem) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    const Diagnostics diagnostics = check_solvable(in_place_input(), verification_options(), xs);
    EXPECT_TRUE(diagnostics.empty());
}

TEST(EigenvalueSolve, CheckSolvableRejectsTheHybridKernel) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    SolverOptions options = verification_options();
    options.kernel = KernelChoice::hybrid;
    const Diagnostics diagnostics = check_solvable(in_place_input(), options, xs);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].file, "problem.toml");
    EXPECT_EQ(diagnostics[0].location, "/settings/kernel");
}

TEST(EigenvalueSolve, CheckSolvableRejectsTheAdjointProblem) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    SolverOptions options = verification_options();
    options.adjoint = true;
    const Diagnostics diagnostics = check_solvable(in_place_input(), options, xs);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].file, "problem.toml");
    EXPECT_EQ(diagnostics[0].location, "/settings/adjoint");
}

TEST(EigenvalueSolve, CheckSolvableNamesTheLinkedSettingsFile) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    SolverOptions options = verification_options();
    options.adjoint = true;
    const Diagnostics diagnostics = check_solvable(linked_settings_input(), options, xs);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].file, "settings.toml");
    EXPECT_EQ(diagnostics[0].location, "/adjoint");
}

TEST(EigenvalueSolve, CheckSolvableRejectsAWielandtShiftThatCannotMoveK) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    SolverOptions options = verification_options();
    options.wielandt_shift = 0.5 * minimum_wielandt_shift;
    const Diagnostics diagnostics = check_solvable(in_place_input(), options, xs);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].file, "problem.toml");
    EXPECT_EQ(diagnostics[0].location, "/settings/wielandt_shift");
}

TEST(EigenvalueSolve, CheckSolvableAcceptsTheWielandtShiftAtTheFloor) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    SolverOptions options = verification_options();
    options.wielandt_shift = minimum_wielandt_shift;
    EXPECT_TRUE(check_solvable(in_place_input(), options, xs).empty());
}

TEST(EigenvalueSolve, CheckSolvableReportsEachFailedCheck) {
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    SolverOptions options = verification_options();
    options.kernel = KernelChoice::hybrid;
    options.adjoint = true;
    const Diagnostics diagnostics = check_solvable(in_place_input(), options, xs);
    EXPECT_EQ(diagnostics.size(), 2U);
}

TEST(EigenvalueSolve, CheckSolvableRejectsDataWithoutFissionProduction) {
    std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    for (NodeCrossSections& node : xs) {
        node.nu_fission_per_cm = {0.0, 0.0};
    }
    const Diagnostics diagnostics = check_solvable(in_place_input(), verification_options(), xs);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].file, "problem.toml");
    EXPECT_EQ(diagnostics[0].location, "cross sections");
}

TEST(EigenvalueSolve, CheckSolvableRejectsDataWithoutFissionEnergy) {
    std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    for (NodeCrossSections& node : xs) {
        node.kappa_fission_j_per_cm = {0.0, 0.0};
    }
    const Diagnostics diagnostics = check_solvable(in_place_input(), verification_options(), xs);
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].file, "problem.toml");
    EXPECT_EQ(diagnostics[0].location, "cross sections");
}

} // namespace nts::test
