// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// Properties of the CMFD solution that hold for any reactor: the analytic eigenvalue of an
// infinite medium, invariance under mirror reflection along each axis, and neutron balance with
// the node-wise residual of TM Eq. (4.7) built from face currents (TM Eqs. (3.9), (4.1), (4.5)).

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/eigenvalue.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <print>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core_fixture.hpp"

namespace nts::test {
namespace {

constexpr double thermal_power_w = 1.0e6;
constexpr double mirror_tolerance = 1.0e-10;
constexpr double balance_tolerance = 1.0e-9;
constexpr double infinite_medium_tolerance = 1.0e-11;
// k of the example fuel in an infinite medium: (nuSigma_f1 + nuSigma_f2 Sigma_12 / Sigma_a2) /
// Sigma_r1 = 0.029375 / 0.027 (fixture documentation).
constexpr double example_k_infinity = 0.029375 / 0.027;

constexpr std::int64_t fuel_composition = 0;
constexpr std::int64_t reflector_composition = 1;
constexpr std::int64_t excluded_composition = -1;

// Reactor layout in row-major [z][y][x] positions. The composition is the per-position value of
// ReactorDescription::composition_map; excluded positions carry excluded_composition.
struct Layout {
    std::vector<double> width_x_cm;
    std::vector<double> width_y_cm;
    std::vector<double> width_z_cm;
    std::vector<std::int64_t> composition;
    std::array<BoundaryCondition, face_count> faces{};
    BoundaryCondition exterior{};
};

[[nodiscard]] std::array<std::size_t, 3> unpack(std::size_t position, std::size_t nx,
                                                std::size_t ny) {
    return {position % nx, (position / nx) % ny, position / (nx * ny)};
}

[[nodiscard]] std::size_t pack(const std::array<std::size_t, 3>& index, std::size_t nx,
                               std::size_t ny) {
    return (((index[2] * ny) + index[1]) * nx) + index[0];
}

// The position reached by reflecting `position` through the mid-plane normal to `axis`.
[[nodiscard]] std::size_t reflected_position(const Layout& layout, std::size_t position,
                                             std::size_t axis) {
    const std::size_t nx = layout.width_x_cm.size();
    const std::size_t ny = layout.width_y_cm.size();
    const std::array<std::size_t, 3> counts{nx, ny, layout.width_z_cm.size()};
    std::array<std::size_t, 3> index = unpack(position, nx, ny);
    index[axis] = counts[axis] - 1 - index[axis];
    return pack(index, nx, ny);
}

[[nodiscard]] std::vector<double>& widths_of(Layout& layout, std::size_t axis) {
    if (axis == 0) {
        return layout.width_x_cm;
    }
    if (axis == 1) {
        return layout.width_y_cm;
    }
    return layout.width_z_cm;
}

// The layout reflected along `axis`: widths reversed, the face pair of the axis swapped, and
// every position's composition moved to its reflected position.
[[nodiscard]] Layout mirror(const Layout& layout, std::size_t axis) {
    Layout out = layout;
    std::ranges::reverse(widths_of(out, axis));
    std::swap(out.faces[2 * axis], out.faces[(2 * axis) + 1]);
    for (std::size_t position = 0; position < layout.composition.size(); ++position) {
        out.composition[reflected_position(layout, position, axis)] = layout.composition[position];
    }
    return out;
}

[[nodiscard]] ReactorDescription build_reactor(const Layout& layout) {
    std::vector<std::size_t> excluded;
    for (std::size_t position = 0; position < layout.composition.size(); ++position) {
        if (layout.composition[position] == excluded_composition) {
            excluded.push_back(position);
        }
    }
    ReactorDescription reactor =
        make_reactor(layout.width_x_cm, layout.width_y_cm, layout.width_z_cm, layout.faces,
                     layout.exterior, excluded);
    reactor.composition_map = layout.composition;
    return reactor;
}

// Node data by composition: the example fuel or the example reflector.
[[nodiscard]] std::vector<NodeCrossSections>
node_cross_sections(const ReactorDescription& reactor) {
    const NodeCrossSections fuel = example_fuel();
    const NodeCrossSections reflector = example_reflector();
    std::vector<NodeCrossSections> xs;
    xs.reserve(reactor.node_count());
    for (const std::size_t position : reactor.node_positions) {
        xs.push_back(reactor.composition_map[position] == fuel_composition ? fuel : reflector);
    }
    return xs;
}

// Mesh widths (cm) of the node at `node` along x, y and z.
[[nodiscard]] std::array<double, 3> node_widths(const ReactorDescription& reactor,
                                                std::size_t node) {
    const std::array<std::size_t, 3> index =
        unpack(reactor.node_positions[node], reactor.nx(), reactor.ny());
    return {reactor.width_x_cm[index[0]], reactor.width_y_cm[index[1]],
            reactor.width_z_cm[index[2]]};
}

// Net leakage per unit volume of `group` from `node`: sum over axes of (J_plus - J_minus) / h_u,
// with J positive along +u (TM Eq. (3.9) with the face currents of TM Eqs. (4.1) and (4.5)).
[[nodiscard]] double leakage_per_cm3(const CmfdCoupling& coupling, std::span<const double> flux,
                                     const std::array<double, 3>& widths_cm, std::size_t node,
                                     std::size_t group) {
    double total = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto minus = static_cast<Face>(2 * axis);
        const auto plus = static_cast<Face>((2 * axis) + 1);
        total += (face_current(coupling, flux, node, plus, group) -
                  face_current(coupling, flux, node, minus, group)) /
                 widths_cm[axis];
    }
    return total;
}

// Heterogeneous 4 x 3 x 2 core with one excluded position, mixed boundary kinds and an
// asymmetric composition, so that a mirror error cannot cancel.
[[nodiscard]] Layout heterogeneous_layout() {
    Layout layout;
    layout.width_x_cm = {5.0, 8.0, 3.0, 6.0};
    layout.width_y_cm = {4.0, 7.0, 5.0};
    layout.width_z_cm = {10.0, 6.0};
    layout.composition.resize(std::size_t{4} * 3 * 2);
    for (std::size_t position = 0; position < layout.composition.size(); ++position) {
        if (position == 5) {
            layout.composition[position] = excluded_composition;
        } else if (position % 3 == 1) {
            layout.composition[position] = reflector_composition;
        } else {
            layout.composition[position] = fuel_composition;
        }
    }
    layout.faces = {boundary(BoundaryKind::vacuum),
                    boundary(BoundaryKind::albedo, {0.3, 0.6}),
                    boundary(BoundaryKind::zero_flux),
                    boundary(BoundaryKind::reflective),
                    boundary(BoundaryKind::albedo, {0.2, 0.4}),
                    boundary(BoundaryKind::vacuum)};
    layout.exterior = boundary(BoundaryKind::vacuum);
    return layout;
}

// Prints the eigenvalue and convergence state so every run reports them.
void report(std::string_view label, const SteadyState& state) {
    const ConvergenceState& c = state.convergence;
    std::println("{}: k = {:.12f}; converged {}, outer {}, inner {}, dk {:.1e}, dL2 {:.1e}, "
                 "dLinf {:.1e}, residual {:.1e}",
                 label, state.k_eff, c.converged, c.outer_iterations, c.inner_iterations,
                 c.eigenvalue_change, c.fission_source_l2_change, c.fission_source_linf_change,
                 c.residual);
}

void expect_mirror_invariant(std::size_t axis) {
    const Layout layout = heterogeneous_layout();
    const Layout mirrored_layout = mirror(layout, axis);
    const ReactorDescription reactor = build_reactor(layout);
    const ReactorDescription mirrored = build_reactor(mirrored_layout);
    const std::vector<NodeCrossSections> xs = node_cross_sections(reactor);
    const std::vector<NodeCrossSections> mirrored_xs = node_cross_sections(mirrored);

    const SteadyState original_state =
        solve_steady_state(reactor, xs, verification_options(), thermal_power_w);
    const SteadyState mirror_state =
        solve_steady_state(mirrored, mirrored_xs, verification_options(), thermal_power_w);
    report(std::format("mirror axis {} original", axis), original_state);
    report(std::format("mirror axis {} mirrored", axis), mirror_state);
    EXPECT_TRUE(original_state.convergence.converged);
    EXPECT_TRUE(mirror_state.convergence.converged);
    EXPECT_LE(std::abs(original_state.k_eff - mirror_state.k_eff), mirror_tolerance);

    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        const std::size_t position = reactor.node_positions[node];
        const std::int64_t mirror_node =
            mirrored.position_nodes[reflected_position(layout, position, axis)];
        ASSERT_GE(mirror_node, 0);
        const auto mirror_index = static_cast<std::size_t>(mirror_node);
        for (std::size_t group = 0; group < energy_group_count; ++group) {
            const double original = original_state.flux[unknown_index(node, group)];
            const double reflected = mirror_state.flux[unknown_index(mirror_index, group)];
            EXPECT_LE(std::abs(original - reflected),
                      mirror_tolerance * std::max(std::abs(original), std::abs(reflected)))
                << "axis " << axis << " node " << node << " group " << group;
        }
    }
}

TEST(CmfdProperties, InfiniteMediumReproducesAnalyticEigenvalue) {
    Layout layout;
    layout.width_x_cm = {3.0, 5.0, 4.0};
    layout.width_y_cm = {2.0, 6.0};
    layout.width_z_cm = {7.0};
    layout.composition.assign(std::size_t{3} * 2 * 1, fuel_composition);

    const ReactorDescription reactor = build_reactor(layout);
    const std::vector<NodeCrossSections> xs = node_cross_sections(reactor);
    const SteadyState state =
        solve_steady_state(reactor, xs, verification_options(), thermal_power_w);
    report("infinite medium", state);
    EXPECT_TRUE(state.convergence.converged);
    EXPECT_LE(std::abs(state.k_eff - example_k_infinity), infinite_medium_tolerance);
}

TEST(CmfdProperties, MirrorAlongXLeavesSolutionInvariant) {
    expect_mirror_invariant(0);
}

TEST(CmfdProperties, MirrorAlongYLeavesSolutionInvariant) {
    expect_mirror_invariant(1);
}

TEST(CmfdProperties, MirrorAlongZLeavesSolutionInvariant) {
    expect_mirror_invariant(2);
}

TEST(CmfdProperties, NeutronBalanceAndNodeResidualHoldOnHeterogeneousCore) {
    const Layout layout = heterogeneous_layout();
    const ReactorDescription reactor = build_reactor(layout);
    const std::vector<NodeCrossSections> xs = node_cross_sections(reactor);
    const SteadyState state =
        solve_steady_state(reactor, xs, verification_options(), thermal_power_w);
    report("balance", state);
    ASSERT_TRUE(state.convergence.converged);
    const CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);
    const std::span<const double> flux = state.flux;
    const double k = state.k_eff;

    double core_leak1 = 0.0;
    double core_removal1 = 0.0;
    double core_production = 0.0;
    double core_leak2 = 0.0;
    double core_removal2 = 0.0;
    double core_scatter = 0.0;
    double residual_sq = 0.0;
    double source_sq = 0.0;
    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        const std::array<double, 3> widths_cm = node_widths(reactor, node);
        const double volume_cm3 = widths_cm[0] * widths_cm[1] * widths_cm[2];
        const NodeCrossSections& node_xs = xs[node];
        const double phi1 = flux[unknown_index(node, 0)];
        const double phi2 = flux[unknown_index(node, 1)];

        // lambda = 1/k (TM Eq. (4.7)).
        const double production =
            ((node_xs.nu_fission_per_cm[0] * phi1) + (node_xs.nu_fission_per_cm[1] * phi2)) / k;
        const double scatter = node_xs.down_scatter_per_cm * phi1;
        const double removal1 = node_xs.removal_per_cm[0] * phi1;
        const double removal2 = node_xs.removal_per_cm[1] * phi2;
        const double leak1 = leakage_per_cm3(coupling, flux, widths_cm, node, 0);
        const double leak2 = leakage_per_cm3(coupling, flux, widths_cm, node, 1);

        const double row1 = leak1 + removal1 - production;
        const double row2 = leak2 + removal2 - scatter;
        residual_sq += (row1 * row1) + (row2 * row2);
        source_sq += production * production;

        core_leak1 += volume_cm3 * leak1;
        core_removal1 += volume_cm3 * removal1;
        core_production += volume_cm3 * production;
        core_leak2 += volume_cm3 * leak2;
        core_removal2 += volume_cm3 * removal2;
        core_scatter += volume_cm3 * scatter;
    }

    EXPECT_LE(std::abs(core_leak1 + core_removal1 - core_production),
              balance_tolerance * core_production);
    EXPECT_LE(std::abs(core_leak2 + core_removal2 - core_scatter),
              balance_tolerance * (core_removal2 + core_scatter));
    EXPECT_LE(std::sqrt(residual_sq), balance_tolerance * std::sqrt(source_sq));
}

} // namespace
} // namespace nts::test
