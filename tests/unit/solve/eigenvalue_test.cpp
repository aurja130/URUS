// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <Eigen/Core>
#include <Eigen/SparseCore>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_fixture.hpp"

namespace nts::test {
namespace {

// Deterministic uniform values (splitmix64).
class ValueStream {
public:
    explicit ValueStream(std::uint64_t seed) : state_(seed) {}

    // Uniform in [lo, hi).
    [[nodiscard]] double uniform(double lo, double hi) {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        const double unit = static_cast<double>(z >> 11U) * 0x1.0p-53;
        return lo + ((hi - lo) * unit);
    }

    // Uniform index in [0, n).
    [[nodiscard]] std::size_t below(std::size_t n) {
        const auto index = static_cast<std::size_t>(uniform(0.0, static_cast<double>(n)));
        return std::min(index, n - 1);
    }

private:
    std::uint64_t state_;
};

// A sparse, strictly diagonally dominant, non-symmetric matrix with about six off-diagonal
// entries per row.
[[nodiscard]] SparseMatrix random_dominant_matrix(std::size_t n, ValueStream& rng) {
    std::vector<Eigen::Triplet<double>> triplets;
    for (std::size_t i = 0; i < n; ++i) {
        double row_sum = 0.0;
        for (std::size_t r = 0; r < 6; ++r) {
            const std::size_t j = rng.below(n);
            if (j == i) {
                continue;
            }
            const double value = rng.uniform(-1.0, 1.0);
            triplets.emplace_back(static_cast<int>(i), static_cast<int>(j), value);
            row_sum += std::abs(value);
        }
        triplets.emplace_back(static_cast<int>(i), static_cast<int>(i), (1.5 * row_sum) + 1.0);
    }
    SparseMatrix a(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
    a.setFromTriplets(triplets.begin(), triplets.end());
    a.makeCompressed();
    return a;
}

[[nodiscard]] Eigen::VectorXd random_vector(std::size_t n, ValueStream& rng) {
    Eigen::VectorXd v(static_cast<Eigen::Index>(n));
    for (double& i : v) {
        i = rng.uniform(-1.0, 1.0);
    }
    return v;
}

// The slab of the heterogeneous cases: four nodes along x (fuel, fuel, reflector, reflector),
// vacuum on the x faces, reflective on y and z.
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

TEST(EigenvalueSolve, InnerSolveReducesResidualToTheRequestedFraction) {
    ValueStream rng(0xA11CE001ULL);
    const std::size_t n = 300;
    const SparseMatrix a = random_dominant_matrix(n, rng);
    const Eigen::VectorXd b = random_vector(n, rng);
    Eigen::VectorXd x = random_vector(n, rng);
    const Eigen::VectorXd x0 = x;

    const InnerSolveResult result = solve_inner(a, b, x, 1.0e-3, 1000);
    EXPECT_DOUBLE_EQ(result.initial_residual, (b - (a * x0)).norm());
    EXPECT_DOUBLE_EQ(result.final_residual, (b - (a * x)).norm());
    EXPECT_LE(result.final_residual, 1.01 * 1.0e-3 * result.initial_residual);
    EXPECT_GE(result.iterations, 1);
    EXPECT_LT(result.iterations, 1000);
}

TEST(EigenvalueSolve, InnerSolveTighterReductionNeedsMoreIterations) {
    ValueStream rng(0xA11CE002ULL);
    const std::size_t n = 300;
    const SparseMatrix a = random_dominant_matrix(n, rng);
    const Eigen::VectorXd b = random_vector(n, rng);
    const Eigen::VectorXd x0 = random_vector(n, rng);

    Eigen::VectorXd x_loose = x0;
    const InnerSolveResult loose = solve_inner(a, b, x_loose, 1.0e-3, 1000);
    Eigen::VectorXd x_tight = x0;
    const InnerSolveResult tight = solve_inner(a, b, x_tight, 1.0e-9, 1000);
    EXPECT_LE(tight.final_residual, 1.01 * 1.0e-9 * tight.initial_residual);
    EXPECT_GT(tight.iterations, loose.iterations);
}

TEST(EigenvalueSolve, InnerSolveStopsAtTheIterationLimit) {
    ValueStream rng(0xA11CE003ULL);
    const std::size_t n = 300;
    const SparseMatrix a = random_dominant_matrix(n, rng);
    const Eigen::VectorXd b = random_vector(n, rng);
    Eigen::VectorXd x = random_vector(n, rng);

    const InnerSolveResult result = solve_inner(a, b, x, 1.0e-12, 2);
    EXPECT_EQ(result.iterations, 2);
    EXPECT_GT(result.final_residual, 1.0e-12 * result.initial_residual);
}

TEST(EigenvalueSolve, InnerSolveWithZeroStartingResidualReturnsAtOnce) {
    ValueStream rng(0xA11CE004ULL);
    const std::size_t n = 300;
    const SparseMatrix a = random_dominant_matrix(n, rng);
    const Eigen::VectorXd x_star = random_vector(n, rng);
    const Eigen::VectorXd b = a * x_star;
    Eigen::VectorXd x = x_star;

    const InnerSolveResult result = solve_inner(a, b, x, 0.01, 10);
    EXPECT_EQ(result.iterations, 0);
    EXPECT_EQ(result.initial_residual, 0.0);
    EXPECT_EQ(result.final_residual, 0.0);
    EXPECT_EQ(x, x_star);
}

TEST(EigenvalueSolve, InnerSolveWithZeroRightHandSideGivesZero) {
    ValueStream rng(0xA11CE005ULL);
    const std::size_t n = 300;
    const SparseMatrix a = random_dominant_matrix(n, rng);
    const Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(n));
    Eigen::VectorXd x = random_vector(n, rng);

    const InnerSolveResult result = solve_inner(a, b, x, 0.01, 10);
    EXPECT_EQ(result.iterations, 0);
    EXPECT_EQ(result.final_residual, 0.0);
    EXPECT_EQ(x, Eigen::VectorXd::Zero(static_cast<Eigen::Index>(n)));
}

TEST(EigenvalueSolve, InfiniteMediumGivesTheInfiniteMultiplicationFactor) {
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::reflective)};
    const ReactorDescription reactor =
        make_reactor(uniform_widths(2.0, 2), uniform_widths(2.0, 2), uniform_widths(2.0, 2), faces);
    const std::vector<NodeCrossSections> xs(reactor.node_count(), example_fuel());
    const CmfdSystem system = assemble_cmfd_system(reactor, make_cmfd_coupling(reactor, xs), xs);

    const EigenvalueSolution solution = solve_eigenvalue(system, verification_options());
    // k∞ = (νΣ_f1 + νΣ_f2 Σ_12 / Σ_r2) / Σ_r1 = 0.029375 / 0.027 (TM §4.1 homogeneous limit).
    const double k_inf = 0.029375 / 0.027;
    EXPECT_NEAR(solution.k_eff, k_inf, 1.0e-11);
    EXPECT_TRUE(solution.convergence.converged);
    ASSERT_EQ(solution.flux.size(), 2 * reactor.node_count());
    // Flat in space, positive, with the infinite-medium spectrum φ2/φ1 = Σ_12 / Σ_a2.
    const double fast = solution.flux[unknown_index(0, 0)];
    EXPECT_GT(fast, 0.0);
    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        EXPECT_NEAR(solution.flux[unknown_index(node, 0)], fast, 1.0e-9 * fast);
        EXPECT_NEAR(solution.flux[unknown_index(node, 1)], fast * 0.017 / 0.08, 1.0e-9 * fast);
    }
}

TEST(EigenvalueSolve, HeterogeneousSlabIsIndependentOfTheWielandtShift) {
    const Slab slab = make_slab();
    const CmfdSystem system =
        assemble_cmfd_system(slab.reactor, make_cmfd_coupling(slab.reactor, slab.xs), slab.xs);

    SolverOptions options = verification_options();
    const EigenvalueSolution first = solve_eigenvalue(system, options);
    options.wielandt_shift = 0.2;
    const EigenvalueSolution second = solve_eigenvalue(system, options);

    EXPECT_TRUE(first.convergence.converged);
    EXPECT_TRUE(second.convergence.converged);
    EXPECT_GT(first.k_eff, 0.0);
    EXPECT_NEAR(first.k_eff, second.k_eff, 1.0e-10);
    EXPECT_LE(first.convergence.residual, 1.0e-9);
    EXPECT_LT(first.convergence.eigenvalue_change, verification_options().eigenvalue_tolerance);
    EXPECT_LT(first.convergence.fission_source_l2_change,
              verification_options().fission_source_l2_tolerance);
    EXPECT_LT(first.convergence.fission_source_linf_change,
              verification_options().fission_source_linf_tolerance);
    EXPECT_GT(first.convergence.inner_iterations, 0);
    EXPECT_EQ(first.convergence.nodal_updates, 0);
}

TEST(EigenvalueSolve, OuterIterationLimitStopsWithoutConvergence) {
    const Slab slab = make_slab();
    const CmfdSystem system =
        assemble_cmfd_system(slab.reactor, make_cmfd_coupling(slab.reactor, slab.xs), slab.xs);

    SolverOptions options = verification_options();
    options.outer_iteration_limit = 2;
    const EigenvalueSolution solution = solve_eigenvalue(system, options);
    EXPECT_FALSE(solution.convergence.converged);
    EXPECT_EQ(solution.convergence.outer_iterations, 2);
    EXPECT_GT(solution.k_eff, 0.0);
}

} // namespace nts::test
