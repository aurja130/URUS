// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/solve/eigenvalue.hpp"

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/common/invariant.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"

#include <Eigen/Core>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace nts {
namespace {

// num / den for the convergence measures: 0 when the numerator is 0, +inf when the
// denominator is not positive (no division by zero, so the FP traps stay quiet).
[[nodiscard]] double safe_ratio(double numerator, double denominator) {
    if (numerator == 0.0) {
        return 0.0;
    }
    if (denominator <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return numerator / denominator;
}

// Nodes whose row of F is non-zero (nodes with νΣ_f > 0), in increasing order.
[[nodiscard]] std::vector<std::size_t> fissile_nodes(const SparseMatrix& production) {
    const std::size_t node_count = static_cast<std::size_t>(production.rows()) / energy_group_count;
    std::vector<bool> has_production(node_count, false);
    for (Eigen::Index outer = 0; outer < production.outerSize(); ++outer) {
        for (SparseMatrix::InnerIterator it(production, outer); it; ++it) {
            if (it.value() != 0.0) {
                has_production[static_cast<std::size_t>(it.row()) / energy_group_count] = true;
            }
        }
    }
    std::vector<std::size_t> nodes;
    for (std::size_t node = 0; node < node_count; ++node) {
        if (has_production[node]) {
            nodes.push_back(node);
        }
    }
    return nodes;
}

} // namespace

InnerSolveResult solve_inner(const SparseMatrix& a, const Eigen::VectorXd& b, Eigen::VectorXd& x,
                             double reduction, std::int64_t iteration_limit) {
    NTS_ASSERT(a.rows() == a.cols(), "the inner matrix must be square");
    NTS_ASSERT(b.size() == a.rows() && x.size() == a.rows(), "sizes must agree");
    NTS_ASSERT(reduction > 0.0 && reduction < 1.0, "the reduction must lie in (0, 1)");
    NTS_ASSERT(iteration_limit >= 1, "at least one inner iteration is needed");

    const Eigen::VectorXd start_residual = b - (a * x);
    const double initial = start_residual.norm();
    if (initial == 0.0) {
        return {.iterations = 0, .initial_residual = 0.0, .final_residual = 0.0};
    }
    const double b_norm = b.norm();
    if (b_norm == 0.0) {
        // A non-singular: the solution of A x = 0 is x = 0, reached without iterating.
        x.setZero();
        return {.iterations = 0, .initial_residual = initial, .final_residual = 0.0};
    }

    // TM §4.5: stop at ‖b − A x‖ ≤ reduction ‖b − A x0‖. Eigen 5's internal::bicgstab
    // (BiCGSTAB.h) loops while ‖r‖ > tolerance, an absolute residual, although its
    // documentation calls the tolerance relative to ‖b‖ (as in Eigen 3.4); the toolchain pins
    // Eigen 5.0.1, so the absolute target is passed as is.
    static_assert(EIGEN_VERSION_AT_LEAST(3, 5, 0),
                  "solve_inner relies on the absolute BiCGSTAB tolerance of Eigen 5");
    Eigen::BiCGSTAB<SparseMatrix, Eigen::IncompleteLUT<double>> solver;
    solver.setMaxIterations(static_cast<Eigen::Index>(iteration_limit));
    solver.setTolerance(reduction * initial);
    solver.compute(a);
    NTS_ASSERT(solver.info() == Eigen::Success, "the incomplete-LU factorisation failed");

    // NoConvergence at the iteration limit is the normal outcome of an inner solve; the
    // residual is measured below rather than taken from the solver's status.
    const Eigen::VectorXd guess = x;
    x = solver.solveWithGuess(b, guess);

    InnerSolveResult result;
    result.iterations = static_cast<std::int64_t>(solver.iterations());
    result.initial_residual = initial;
    result.final_residual = (b - (a * x)).norm();
    return result;
}

EigenvalueSolution solve_eigenvalue(const CmfdSystem& system, const SolverOptions& options) {
    const SparseMatrix& m = system.migration;
    const SparseMatrix& f = system.production;
    NTS_ASSERT(m.rows() == m.cols(), "M must be square");
    NTS_ASSERT(f.rows() == m.rows() && f.cols() == m.cols(), "F must have the shape of M");
    NTS_ASSERT(m.rows() % static_cast<Eigen::Index>(energy_group_count) == 0,
               "the unknowns must come in groups of two per node");
    NTS_ASSERT(f.nonZeros() > 0, "F must have a non-zero entry");
    NTS_ASSERT(options.outer_iteration_limit >= 1, "at least one outer iteration is needed");

    const std::vector<std::size_t> fissile = fissile_nodes(f);
    const auto n = static_cast<Eigen::Index>(m.rows());
    Eigen::VectorXd phi = Eigen::VectorXd::Ones(n);
    double k = 1.0;

    EigenvalueSolution solution;
    ConvergenceState& conv = solution.convergence;
    while (!conv.converged && conv.outer_iterations < options.outer_iteration_limit) {
        ++conv.outer_iterations;
        // TM Eq. (4.8): Wielandt shift k_s = k + δk; A = M − F/k_s.
        const double inv_k = 1.0 / k;
        const double k_s = k + options.wielandt_shift;
        NTS_ASSERT(k_s > 0.0, "the shifted eigenvalue must be positive");
        const double inv_k_s = 1.0 / k_s;
        const SparseMatrix a = m - (inv_k_s * f);
        const Eigen::VectorXd psi = f * phi;
        const Eigen::VectorXd rhs = (inv_k - inv_k_s) * psi;

        Eigen::VectorXd phi_new = phi;
        const InnerSolveResult inner =
            solve_inner(a, rhs, phi_new, options.inner_reduction, options.inner_iteration_limit);
        conv.inner_iterations += inner.iterations;

        // TM Eq. (4.8): γ = ⟨ψ', ψ⟩ / ⟨ψ', ψ'⟩ and 1/k' = 1/k_s + γ (1/k − 1/k_s).
        const Eigen::VectorXd psi_new = f * phi_new;
        const double overlap = psi_new.dot(psi);
        const double self = psi_new.dot(psi_new);
        NTS_ASSERT(self > 0.0, "the fission source of the new flux must not vanish");
        const double gamma = overlap / self;
        const double inv_k_new = inv_k_s + (gamma * (inv_k - inv_k_s));
        NTS_ASSERT(inv_k_new > 0.0, "the Wielandt eigenvalue must be positive");
        const double k_new = 1.0 / inv_k_new;

        // TM Eq. (4.10).
        conv.eigenvalue_change = std::abs(k_new - k);
        const double l2_norm_overlap = overlap > 0.0 ? std::sqrt(overlap) : 0.0;
        conv.fission_source_l2_change = safe_ratio((psi_new - psi).norm(), l2_norm_overlap);
        double linf = 0.0;
        for (const std::size_t node : fissile) {
            const std::size_t row = unknown_index(node, 0);
            linf = std::max(linf, safe_ratio(std::abs(psi_new[static_cast<Eigen::Index>(row)] -
                                                      psi[static_cast<Eigen::Index>(row)]),
                                             std::abs(psi_new[static_cast<Eigen::Index>(row)])));
        }
        conv.fission_source_linf_change = linf;
        conv.converged = conv.eigenvalue_change < options.eigenvalue_tolerance &&
                         conv.fission_source_l2_change < options.fission_source_l2_tolerance &&
                         conv.fission_source_linf_change < options.fission_source_linf_tolerance;

        phi = phi_new;
        k = k_new;
    }

    // Final residual ‖Mφ − λFφ‖₂ / ‖λFφ‖₂ with λ = 1/k (TM §4.6).
    const double lambda = 1.0 / k;
    const Eigen::VectorXd lambda_f_phi = lambda * (f * phi);
    solution.convergence.residual =
        safe_ratio((m * phi - lambda_f_phi).norm(), lambda_f_phi.norm());
    solution.k_eff = k;
    // While k_s lies below the eigenvalue, A is indefinite and each solve flips the sign of φ;
    // Eq. (4.9) is invariant under that. Return the eigenvector with a positive total fission
    // source.
    if ((f * phi).sum() < 0.0) {
        phi = -phi;
    }
    solution.flux.assign(phi.data(), phi.data() + phi.size());
    return solution;
}

} // namespace nts
