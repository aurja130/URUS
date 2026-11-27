// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "core_fixture.hpp"

#include "nts/common/invariant.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace nts::test {
namespace {

// Joules per fission (200 MeV) and neutrons per fission (2.43) of the example data.
constexpr double joules_per_fission = 3.204e-11;
constexpr double neutrons_per_fission = 2.43;

} // namespace

BoundaryCondition boundary(BoundaryKind kind, std::array<double, energy_group_count> albedo) {
    BoundaryCondition condition;
    condition.kind = kind;
    switch (kind) {
    case BoundaryKind::reflective:
        condition.albedo.fill(reflective_albedo);
        break;
    case BoundaryKind::vacuum:
        condition.albedo.fill(vacuum_albedo);
        break;
    case BoundaryKind::zero_flux:
        condition.albedo.fill(zero_flux_albedo);
        break;
    case BoundaryKind::albedo:
        condition.albedo = albedo;
        break;
    }
    return condition;
}

ReactorDescription make_reactor(std::vector<double> width_x_cm, std::vector<double> width_y_cm,
                                std::vector<double> width_z_cm,
                                const std::array<BoundaryCondition, face_count>& faces,
                                const BoundaryCondition& exterior,
                                const std::vector<std::size_t>& excluded) {
    NTS_ASSERT(!width_x_cm.empty() && !width_y_cm.empty() && !width_z_cm.empty(),
               "every axis needs at least one node");
    ReactorDescription reactor;
    reactor.width_x_cm = std::move(width_x_cm);
    reactor.width_y_cm = std::move(width_y_cm);
    reactor.width_z_cm = std::move(width_z_cm);
    const std::size_t count = reactor.position_count();
    reactor.composition_map.assign(count, 0);
    for (const std::size_t position : excluded) {
        NTS_ASSERT(position < count, "excluded position out of range");
        reactor.composition_map[position] = -1;
    }
    reactor.position_nodes.assign(count, -1);
    for (std::size_t position = 0; position < count; ++position) {
        if (reactor.composition_map[position] != -1) {
            reactor.position_nodes[position] =
                static_cast<std::int64_t>(reactor.node_positions.size());
            reactor.node_positions.push_back(position);
        }
    }
    reactor.bounding_faces = faces;
    reactor.exterior = exterior;
    return reactor;
}

std::vector<double> uniform_widths(double length_cm, std::size_t count) {
    NTS_ASSERT(count > 0, "a width list needs at least one entry");
    return std::vector<double>(count, length_cm / static_cast<double>(count));
}

NodeCrossSections make_cross_sections(double d1_cm, double d2_cm, double absorption1_per_cm,
                                      double absorption2_per_cm, double down_scatter_per_cm,
                                      double nu_fission1_per_cm, double nu_fission2_per_cm) {
    NodeCrossSections xs;
    xs.diffusion_cm = {d1_cm, d2_cm};
    xs.absorption_per_cm = {absorption1_per_cm, absorption2_per_cm};
    xs.down_scatter_per_cm = down_scatter_per_cm;
    xs.removal_per_cm = {absorption1_per_cm + down_scatter_per_cm, absorption2_per_cm};
    xs.nu_fission_per_cm = {nu_fission1_per_cm, nu_fission2_per_cm};
    xs.kappa_fission_j_per_cm = {joules_per_fission * nu_fission1_per_cm / neutrons_per_fission,
                                 joules_per_fission * nu_fission2_per_cm / neutrons_per_fission};
    xs.discontinuity_factor.fill(1.0);
    return xs;
}

NodeCrossSections example_fuel() {
    return make_cross_sections(1.4, 0.4, 0.010, 0.08, 0.017, 0.006, 0.11);
}

NodeCrossSections example_reflector() {
    return make_cross_sections(1.3, 0.3, 0.002, 0.02, 0.03, 0.0, 0.0);
}

SolverOptions verification_options() {
    SolverOptions options;
    options.kernel = KernelChoice::finite_difference;
    options.eigenvalue_tolerance = 1.0e-12;
    options.fission_source_l2_tolerance = 1.0e-11;
    options.fission_source_linf_tolerance = 1.0e-10;
    options.inner_reduction = 0.01;
    options.inner_iteration_limit = 1;
    options.outer_iteration_limit = 10000;
    return options;
}

} // namespace nts::test
