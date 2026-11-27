// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/solve/steady_state.hpp"

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/common/diagnostic.hpp"
#include "nts/common/invariant.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nts {

Diagnostics check_solvable(const ProblemInput& input, const SolverOptions& options,
                           std::span<const NodeCrossSections> cross_sections) {
    // A settings check names the settings document's file: the problem input when that document
    // is in place or absent, else its linked file. The location is `/settings/<name>` in the
    // problem input and `/<name>` in a linked settings file.
    const std::filesystem::path& settings_document =
        input.document_files[static_cast<std::size_t>(InputDocument::settings)];
    const bool in_problem_input = settings_document.empty() || settings_document == input.file;
    const std::string settings_file = (in_problem_input ? input.file : settings_document).string();
    const std::string settings_prefix = in_problem_input ? "/settings/" : "/";

    const std::string file = input.file.string();
    Diagnostics diagnostics;
    if (options.kernel != KernelChoice::finite_difference) {
        diagnostics.push_back(Diagnostic{
            .file = settings_file,
            .location = settings_prefix + "kernel",
            .entry = {},
            .message = std::format(
                R"(kernel "{}" is not available in this engine version; only "finite_difference" is)",
                kernel_choice_name(options.kernel)),
        });
    }
    if (options.adjoint) {
        diagnostics.push_back(Diagnostic{
            .file = settings_file,
            .location = settings_prefix + "adjoint",
            .entry = {},
            .message = "the adjoint problem is not available in this engine version",
        });
    }
    if (options.wielandt_shift < minimum_wielandt_shift) {
        diagnostics.push_back(Diagnostic{
            .file = settings_file,
            .location = settings_prefix + "wielandt_shift",
            .entry = {},
            .message = std::format("wielandt_shift {:g} is below the minimum {:g}; a smaller shift "
                                   "cannot move k in the Wielandt iteration",
                                   options.wielandt_shift, minimum_wielandt_shift),
        });
    }

    bool has_production = false;
    bool has_fission_energy = false;
    for (const NodeCrossSections& xs : cross_sections) {
        has_production =
            has_production || (xs.nu_fission_per_cm[0] + xs.nu_fission_per_cm[1] > 0.0);
        has_fission_energy = has_fission_energy ||
                             (xs.kappa_fission_j_per_cm[0] + xs.kappa_fission_j_per_cm[1] > 0.0);
    }
    if (!has_production) {
        diagnostics.push_back(Diagnostic{
            .file = file,
            .location = "cross sections",
            .entry = {},
            .message =
                "no node has fission production (nu Sigma_f), so the problem has no eigenvalue",
        });
    }
    if (!has_fission_energy) {
        diagnostics.push_back(Diagnostic{
            .file = file,
            .location = "cross sections",
            .entry = {},
            .message = "no node has fission energy (kappa Sigma_f), so the flux cannot be scaled "
                       "to the thermal power",
        });
    }
    return diagnostics;
}

SteadyState solve_steady_state(const ReactorDescription& reactor,
                               std::span<const NodeCrossSections> cross_sections,
                               const SolverOptions& options, double thermal_power_w) {
    NTS_ASSERT(thermal_power_w > 0.0, "the thermal power must be positive");
    NTS_ASSERT(cross_sections.size() == reactor.node_count(),
               "one cross-section set is needed per node");

    const CmfdCoupling coupling = make_cmfd_coupling(reactor, cross_sections);
    const CmfdSystem system = assemble_cmfd_system(reactor, coupling, cross_sections);
    EigenvalueSolution eigen = solve_eigenvalue(system, options);

    SteadyState result;
    result.k_eff = eigen.k_eff;
    result.convergence = eigen.convergence;
    result.flux = std::move(eigen.flux);

    // TM Eq. (3.10): P^m = V^m (κΣ_f1 φ_1 + κΣ_f2 φ_2), scaled so that Σ P^m = thermal power.
    const std::size_t node_count = reactor.node_count();
    const std::size_t nx = reactor.nx();
    const std::size_t ny = reactor.ny();
    result.volume_cm3.resize(node_count);
    result.power_w.resize(node_count);
    double total_w = 0.0;
    for (std::size_t node = 0; node < node_count; ++node) {
        const std::size_t position = reactor.node_positions[node];
        const std::size_t i = position % nx;
        const std::size_t j = (position / nx) % ny;
        const std::size_t k = position / (nx * ny);
        result.volume_cm3[node] =
            reactor.width_x_cm[i] * reactor.width_y_cm[j] * reactor.width_z_cm[k];
        const NodeCrossSections& xs = cross_sections[node];
        const double fission_energy_per_volume =
            (xs.kappa_fission_j_per_cm[0] * result.flux[unknown_index(node, 0)]) +
            (xs.kappa_fission_j_per_cm[1] * result.flux[unknown_index(node, 1)]);
        result.power_w[node] = result.volume_cm3[node] * fission_energy_per_volume;
        total_w += result.power_w[node];
    }
    NTS_ASSERT(total_w > 0.0, "the fission power must be positive to scale the flux");

    const double scale = thermal_power_w / total_w;
    for (double& power : result.power_w) {
        power *= scale;
    }
    for (double& phi : result.flux) {
        phi *= scale;
    }
    return result;
}

} // namespace nts
