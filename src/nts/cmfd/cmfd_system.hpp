// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The coarse-mesh finite-difference (CMFD) system of the two-group diffusion equations
// (TM §4.1-§4.3): face couplings, the migration matrix M and the production matrix F of
// TM Eq. (4.7).

#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <Eigen/SparseCore>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nts {

using SparseMatrix = Eigen::SparseMatrix<double, Eigen::RowMajor>;

// The unknown of group g (0 fast, 1 thermal) of node n: the two groups of a node are adjacent
// (TM §4.3). Every flux vector of this module uses this layout and has 2 * node_count entries.
[[nodiscard]] constexpr std::size_t unknown_index(std::size_t node, std::size_t group) {
    return (node * energy_group_count) + group;
}

// One face of one node. A face with a neighbour is an interior face (TM Eq. (4.2)); a face
// without one is a bounding face or an exterior face and takes the albedo α_b of its boundary
// condition (TM Eq. (4.4), BoundaryCondition::albedo).
struct NodeFace {
    std::int64_t neighbour = -1;                     // node across the face, or -1
    std::array<double, energy_group_count> albedo{}; // α_b per group, when neighbour == -1
};

// Index of node `node`, face `face`, group `group` in CmfdCoupling::d_tilde and d_hat.
[[nodiscard]] constexpr std::size_t face_slot(std::size_t node, Face face, std::size_t group) {
    return (((node * face_count) + static_cast<std::size_t>(face)) * energy_group_count) + group;
}

// The couplings of every node face. D̃ and D̂ are dimensionless (D [cm] over h [cm]): a
// coefficient times a flux in cm^-2 s^-1 gives a current in cm^-2 s^-1. An interior face is
// shared by two nodes and both of its entries hold the same D̃ and the same D̂; assembly
// asserts it.
struct CmfdCoupling {
    std::size_t node_count = 0;
    std::vector<std::array<NodeFace, face_count>> faces; // per node, Face order
    // D̃: TM Eq. (4.2) at an interior face, Eq. (4.4) at a face without neighbour.
    std::vector<double> d_tilde; // [face_slot(node, face, group)]
    // D̂: the coupling correction of TM Eqs. (4.1) and (4.5).
    std::vector<double> d_hat; // [face_slot(node, face, group)]
};

// The faces of every node of `reactor` and D̃ by TM Eq. (4.2) at interior faces and Eq. (4.4)
// at bounding and exterior faces, from D_g of `cross_sections` (per node) and the node widths
// along the face normal; every D̂ = 0. A face on a bounding face takes that face's condition;
// a face next to an excluded position takes `reactor.exterior`.
// Preconditions: cross_sections.size() == reactor.node_count(); every D_g > 0; widths > 0.
[[nodiscard]] CmfdCoupling make_cmfd_coupling(const ReactorDescription& reactor,
                                              std::span<const NodeCrossSections> cross_sections);

// The net current of group `group` through face `face` of `node`, positive along +u:
// TM Eq. (4.1) at an interior face, Eq. (4.5) at a face without neighbour. `flux` in the
// unknown_index layout; the current has the units of the flux (cm^-2 s^-1).
[[nodiscard]] double face_current(const CmfdCoupling& coupling, std::span<const double> flux,
                                  std::size_t node, Face face, std::size_t group);

// M and F of TM Eq. (4.7), one row per unknown, per unit volume, in cm^-1:
//   row (n, 0): L_1 + Σ_r1 φ_1            = λ (νΣ_f1 φ_1 + νΣ_f2 φ_2)
//   row (n, 1): L_2 + Σ_r2 φ_2 - Σ_12 φ_1 = 0
// where L_g sums over the three axes L_u = (h_u L_u from TM Eq. (4.6)) / h_u (TM Eq. (3.9)).
// F has entries only in group-1 rows.
struct CmfdSystem {
    SparseMatrix migration;  // M
    SparseMatrix production; // F
};

// Preconditions: as for make_cmfd_coupling, and `coupling` made for the same reactor.
[[nodiscard]] CmfdSystem assemble_cmfd_system(const ReactorDescription& reactor,
                                              const CmfdCoupling& coupling,
                                              std::span<const NodeCrossSections> cross_sections);

} // namespace nts
