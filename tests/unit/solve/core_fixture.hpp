// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// In-memory reactors, node data and options for the CMFD and eigenvalue tests; no files.

#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace nts::test {

// A boundary condition of `kind` with BoundaryCondition::albedo filled as the engine's loader
// does: reflective_albedo, vacuum_albedo or zero_flux_albedo in both groups, or `albedo` for
// BoundaryKind::albedo.
[[nodiscard]] BoundaryCondition boundary(BoundaryKind kind,
                                         std::array<double, energy_group_count> albedo = {});

// A reactor with the given widths (cm). Every position holds composition 0 except the
// positions listed in `excluded` (row-major [z][y][x] indices), which are excluded; nodes,
// node_positions and position_nodes are filled as the loader does. `faces` in Face order.
[[nodiscard]] ReactorDescription
make_reactor(std::vector<double> width_x_cm, std::vector<double> width_y_cm,
             std::vector<double> width_z_cm, const std::array<BoundaryCondition, face_count>& faces,
             const BoundaryCondition& exterior = {}, const std::vector<std::size_t>& excluded = {});

// `count` equal widths summing to `length_cm`.
[[nodiscard]] std::vector<double> uniform_widths(double length_cm, std::size_t count);

// Two-group node data, cm^-1 (D in cm); removal Σ_r1 = Σ_a1 + Σ_12, Σ_r2 = Σ_a2; κΣ_fg =
// 3.204e-11 J x νΣ_fg / 2.43 (200 MeV per fission, 2.43 neutrons per fission); discontinuity
// factors 1; kinetics data zero.
[[nodiscard]] NodeCrossSections
make_cross_sections(double d1_cm, double d2_cm, double absorption1_per_cm,
                    double absorption2_per_cm, double down_scatter_per_cm,
                    double nu_fission1_per_cm, double nu_fission2_per_cm);

// The example fuel of the bare homogeneous cases: D1 = 1.4, D2 = 0.4, Σ_a1 = 0.010,
// Σ_a2 = 0.08, Σ_12 = 0.017, νΣ_f1 = 0.006, νΣ_f2 = 0.11 (k∞ = 0.029375 / 0.027 = 1.0879629…).
[[nodiscard]] NodeCrossSections example_fuel();

// A non-multiplying reflector: D1 = 1.3, D2 = 0.3, Σ_a1 = 0.002, Σ_a2 = 0.02, Σ_12 = 0.03.
[[nodiscard]] NodeCrossSections example_reflector();

// The milestone 1 plan §2.1 verification tolerances: finite_difference kernel, ε_k = 1e-12,
// ε_L2 = 1e-11, ε_L∞ = 1e-10, ε_erf = 0.01, N_in = 1, outer limit 10000; other options at
// their defaults.
[[nodiscard]] SolverOptions verification_options();

} // namespace nts::test
