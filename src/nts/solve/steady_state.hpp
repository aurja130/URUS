// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The steady state without feedback: cross sections at the given node states, the CMFD
// eigenvalue problem, and the flux scaled to the thermal power (TM §3.3, §4).

#include "nts/common/diagnostic.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace nts {

struct SteadyState {
    double k_eff = 0.0;
    // Node-average flux in cm^-2 s^-1, unknown_index layout, scaled by TM Eq. (3.10) so that
    // Σ_m V^m (κΣ_f1 φ_1 + κΣ_f2 φ_2) = thermal_power_w.
    std::vector<double> flux;
    std::vector<double> power_w;    // per node: V^m (κΣ_f1 φ_1 + κΣ_f2 φ_2), W
    std::vector<double> volume_cm3; // per node: h_x h_y h_z
    ConvergenceState convergence;
};

// Checks that the options ask only for what this engine version provides and that the data
// can give an eigenvalue and a power (output contract §6, "Methods this engine version
// provides"): options.kernel == finite_difference, !options.adjoint, some node with
// νΣ_f1 + νΣ_f2 > 0 and some node with κΣ_f1 + κΣ_f2 > 0. One diagnostic per failed check;
// `file` names the problem input, `location` the setting or the data concerned.
[[nodiscard]] Diagnostics check_solvable(const std::filesystem::path& problem_file,
                                         const SolverOptions& options,
                                         std::span<const NodeCrossSections> cross_sections);

// Builds the coupling (make_cmfd_coupling), the system (assemble_cmfd_system), solves the
// eigenvalue (solve_eigenvalue with `options`) and scales the flux to `thermal_power_w`. The
// result of a solve that did not converge is returned the same way, with
// convergence.converged == false.
// Preconditions: check_solvable(…) is empty; cross_sections.size() == reactor.node_count();
// thermal_power_w > 0.
[[nodiscard]] SteadyState solve_steady_state(const ReactorDescription& reactor,
                                             std::span<const NodeCrossSections> cross_sections,
                                             const SolverOptions& options, double thermal_power_w);

} // namespace nts
