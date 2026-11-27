// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The steady-state eigenvalue of the CMFD system: Wielandt-shifted inverse iteration
// (TM §4.4-§4.6, Eqs. (4.8)-(4.10)).

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/io/problem.hpp"

#include <Eigen/Core>

#include <cstdint>
#include <vector>

namespace nts {

// The convergence state of one eigenvalue solve (output contract §6, `convergence/`).
// The changes are dimensionless.
struct ConvergenceState {
    bool converged = false;
    std::int64_t outer_iterations = 0;
    std::int64_t inner_iterations = 0;       // BiCGSTAB iterations summed over all outer iterations
    std::int64_t nodal_updates = 0;          // 0 for the finite-difference kernel
    double eigenvalue_change = 0.0;          // δ_k of the last outer iteration, TM Eq. (4.10)
    double fission_source_l2_change = 0.0;   // δ_L2 of the last outer iteration
    double fission_source_linf_change = 0.0; // δ_L∞, over nodes with νΣ_f > 0
    double residual = 0.0; // ‖Mφ − λFφ‖₂ / ‖λFφ‖₂ at the returned flux and eigenvalue, λ = 1/k
    double coupling_correction_change = 0.0; // largest D̂ change of the last nodal update
};

// The result of one linear solve (TM §4.5). Residual norms are ‖b − A x‖₂.
struct InnerSolveResult {
    std::int64_t iterations = 0;
    double initial_residual = 0.0;
    double final_residual = 0.0;
};

// Solves A x = b by BiCGSTAB with an incomplete-LU (Eigen IncompleteLUT) preconditioner,
// starting from the given x. Stops when the residual has fallen to `reduction` times its value
// at the starting x, or after `iteration_limit` iterations, whichever comes first (TM §4.5;
// the tolerance passed to Eigen is the absolute residual reduction ‖b − A x0‖, the criterion
// Eigen 5 applies). A zero starting residual returns at once.
// Preconditions: A square and non-singular, sizes agree, 0 < reduction < 1, iteration_limit >= 1.
[[nodiscard]] InnerSolveResult solve_inner(const SparseMatrix& a, const Eigen::VectorXd& b,
                                           Eigen::VectorXd& x, double reduction,
                                           std::int64_t iteration_limit);

struct EigenvalueSolution {
    double k_eff = 0.0;       // the eigenvalue of the last outer iteration
    std::vector<double> flux; // unknown_index layout; arbitrary amplitude, Σ Fφ > 0
    ConvergenceState convergence;
};

// TM Eqs. (4.8)-(4.10), starting from φ = 1 in every unknown and k = 1. Each outer iteration:
// k_s = k + options.wielandt_shift; A = M − F / k_s; ψ = F φ; solve
// A φ' = (1/k − 1/k_s) ψ by solve_inner from φ (reduction options.inner_reduction, limit
// options.inner_iteration_limit); ψ' = F φ'; γ = ⟨ψ', ψ⟩ / ⟨ψ', ψ'⟩;
// 1/k' = 1/k_s + γ (1/k − 1/k_s); then the tests of Eq. (4.10) against
// options.eigenvalue_tolerance, fission_source_l2_tolerance and fission_source_linf_tolerance
// (δ_L∞ over nodes whose F row is non-zero). φ is not rescaled between iterations. Stops when
// all three tests hold (converged) or after options.outer_iteration_limit iterations (not
// converged). The other fields of `options` are not used.
// Preconditions: M and F square of the same size, F has a non-zero entry.
[[nodiscard]] EigenvalueSolution solve_eigenvalue(const CmfdSystem& system,
                                                  const SolverOptions& options);

} // namespace nts
