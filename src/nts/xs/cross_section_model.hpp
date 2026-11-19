// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The cross-section model: node data interpolated from the library at the node state
// (TM §7.1, §7.2, §7.4 steps 1 and 4; rods and poisons are not modelled yet).

#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace nts {

// The macroscopic data of one node. Group index 0 is fast. Units are cm, s and J.
struct NodeCrossSections {
    std::array<double, energy_group_count> diffusion_cm{};      // D_g, from interpolated 1/(3D_g)
    std::array<double, energy_group_count> absorption_per_cm{}; // Σ_ag
    double down_scatter_per_cm = 0.0;                           // Σ_12, effective (TM Eq. (3.5))
    std::array<double, energy_group_count> removal_per_cm{};    // Σ_r1 = Σ_a1 + Σ_12, Σ_r2 = Σ_a2
    std::array<double, energy_group_count> nu_fission_per_cm{}; // νΣ_fg
    std::array<double, energy_group_count> kappa_fission_j_per_cm{};      // κΣ_fg
    std::optional<std::array<double, energy_group_count>> fission_per_cm; // Σ_fg, when present
    std::array<double, energy_group_count> inverse_velocity_s_per_cm{};   // 1/v_g
    std::array<double, delayed_group_count> delayed_fraction{};           // β_k
    std::array<double, delayed_group_count> decay_constant_per_s{};       // λ_k
    // f_{g,face} at [face * energy_group_count + g], faces in Face order; 1 when absent.
    std::array<double, face_count * energy_group_count> discontinuity_factor{};
};

// Where a node state lies on one state axis: inside the axis range, or clamped to its first
// (below) or last (above) point (TM §7.2).
enum class ClampSide : std::uint8_t { none, below, above };

// Indexed by StateAxis; `none` for an axis the composition set does not have.
using StateClamps = std::array<ClampSide, state_axis_count>;

struct InterpolatedComposition {
    NodeCrossSections cross_sections;
    StateClamps clamps{};
};

// TM Eq. (7.2): composition `composition` of `set` at `state`, multilinear over the axes of
// the set in the coordinates √T_D, ρ_m, T_m and C_B; a coordinate outside an axis is clamped
// to its nearest end point. Preconditions: every entry of the composition is valid; no state
// value is NaN.
[[nodiscard]] InterpolatedComposition
interpolate_composition(const CompositionSet& set, std::size_t composition, const NodeState& state);

// Clamp totals: [StateAxis][0] below, [StateAxis][1] above, counted over nodes.
using ClampCounts = std::array<std::array<std::size_t, 2>, state_axis_count>;

struct CrossSectionField {
    std::vector<NodeCrossSections> nodes; // per node
    std::vector<StateClamps> clamps;      // per node
    ClampCounts clamp_counts{};
};

// The data of every node of `problem` at its node state (TM §7.4 steps 1 and 4).
[[nodiscard]] CrossSectionField evaluate_cross_sections(const Problem& problem);

} // namespace nts
