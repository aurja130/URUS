// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/cmfd/cmfd_system.hpp"

#include "nts/common/invariant.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <Eigen/SparseCore>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace nts {
namespace {

using Triplet = Eigen::Triplet<double>;

// Cell indices (i, j, k) of a position of the row-major map [z][y][x], x fastest.
[[nodiscard]] std::array<std::size_t, 3> cell_of(const ReactorDescription& reactor,
                                                 std::size_t position) {
    const std::size_t nx = reactor.nx();
    const std::size_t ny = reactor.ny();
    return {position % nx, (position / nx) % ny, position / (nx * ny)};
}

[[nodiscard]] std::size_t position_of(const ReactorDescription& reactor,
                                      const std::array<std::size_t, 3>& cell) {
    return (((cell[2] * reactor.ny()) + cell[1]) * reactor.nx()) + cell[0];
}

// Widths along axis 0 (x), 1 (y) or 2 (z).
[[nodiscard]] const std::vector<double>& widths_along(const ReactorDescription& reactor,
                                                      std::size_t axis) {
    if (axis == 0) {
        return reactor.width_x_cm;
    }
    if (axis == 1) {
        return reactor.width_y_cm;
    }
    return reactor.width_z_cm;
}

// Width h_u of node `node` along `axis` (cm).
[[nodiscard]] double node_width_cm(const ReactorDescription& reactor, std::size_t node,
                                   std::size_t axis) {
    const std::array<std::size_t, 3> cell = cell_of(reactor, reactor.node_positions[node]);
    return widths_along(reactor, axis)[cell[axis]];
}

// The position across `face` of `position`, or nullopt when the face is on the box boundary.
[[nodiscard]] std::optional<std::size_t> adjacent_position(const ReactorDescription& reactor,
                                                           std::size_t position, Face face) {
    const auto face_index = static_cast<std::size_t>(face);
    const std::size_t axis = face_index / 2;
    const bool plus = (face_index % 2) == 1;
    std::array<std::size_t, 3> cell = cell_of(reactor, position);
    if (plus) {
        if (cell[axis] + 1 >= widths_along(reactor, axis).size()) {
            return std::nullopt;
        }
        ++cell[axis];
    } else {
        if (cell[axis] == 0) {
            return std::nullopt;
        }
        --cell[axis];
    }
    return position_of(reactor, cell);
}

// TM Eq. (4.2): D̃ at an interior face; `left` is the node on the −u side of the face.
[[nodiscard]] double interior_d_tilde(double d_left_cm, double d_right_cm, double h_left_cm,
                                      double h_right_cm) {
    return (2.0 * d_left_cm * d_right_cm) / ((d_left_cm * h_right_cm) + (d_right_cm * h_left_cm));
}

// TM Eq. (4.4): D̃_b at a face without neighbour, for albedo α_b and node width h. α ≤ 1 uses
// 2αD/(2D+αh); α > 1 uses 2D/(2D/α+h), the same value. Both forms stay finite for every finite
// α ≥ 0, and the result is exactly 0 for α = 0.
[[nodiscard]] double boundary_d_tilde(double albedo, double d_cm, double h_cm) {
    if (albedo <= 1.0) {
        return (2.0 * albedo * d_cm) / ((2.0 * d_cm) + (albedo * h_cm));
    }
    return (2.0 * d_cm) / (((2.0 * d_cm) / albedo) + h_cm);
}

[[nodiscard]] int storage_index(std::size_t index) {
    NTS_ASSERT(index <= static_cast<std::size_t>(std::numeric_limits<int>::max()),
               "matrix index exceeds the sparse storage index");
    return static_cast<int>(index);
}

// Asserts that the faces of `node` and of its neighbour across `face` carry the same D̃ and D̂
// (TM Eqs. (4.1), (4.6): both nodes use one coupling per interface).
void check_shared_face(const CmfdCoupling& coupling, std::size_t node, Face face) {
    const auto face_index = static_cast<std::size_t>(face);
    const NodeFace& nf = coupling.faces[node][face_index];
    if (nf.neighbour < 0) {
        return;
    }
    const auto other = static_cast<std::size_t>(nf.neighbour);
    const auto opposite_index = face_index ^ std::size_t{1};
    const auto opposite = static_cast<Face>(opposite_index);
    NTS_ASSERT(std::cmp_equal(coupling.faces[other][opposite_index].neighbour, node),
               "interior face is not shared by its two nodes");
    for (std::size_t group = 0; group < energy_group_count; ++group) {
        NTS_ASSERT(coupling.d_tilde[face_slot(node, face, group)] ==
                       coupling.d_tilde[face_slot(other, opposite, group)],
                   "the two sides of an interior face disagree in D-tilde");
        NTS_ASSERT(coupling.d_hat[face_slot(node, face, group)] ==
                       coupling.d_hat[face_slot(other, opposite, group)],
                   "the two sides of an interior face disagree in D-hat");
    }
}

// Fills the D̃ of an interior face of `node` (TM Eq. (4.2)), evaluated once from the minus-side
// and plus-side nodes so that both nodes of the face hold bit-identical values.
void set_interior_faces(CmfdCoupling& coupling, const ReactorDescription& reactor,
                        std::span<const NodeCrossSections> cross_sections, std::size_t node,
                        Face face, std::size_t other) {
    const auto face_index = static_cast<std::size_t>(face);
    const std::size_t axis = face_index / 2;
    const bool plus = (face_index % 2) == 1;
    const std::size_t left = plus ? node : other;
    const std::size_t right = plus ? other : node;
    for (std::size_t group = 0; group < energy_group_count; ++group) {
        coupling.d_tilde[face_slot(node, face, group)] = interior_d_tilde(
            cross_sections[left].diffusion_cm[group], cross_sections[right].diffusion_cm[group],
            node_width_cm(reactor, left, axis), node_width_cm(reactor, right, axis));
    }
}

// Fills the D̃_b of a face without neighbour (TM Eq. (4.4)) from the albedo of `condition`.
void set_boundary_faces(CmfdCoupling& coupling, const ReactorDescription& reactor,
                        std::span<const NodeCrossSections> cross_sections, std::size_t node,
                        Face face, const BoundaryCondition& condition) {
    const auto face_index = static_cast<std::size_t>(face);
    const std::size_t axis = face_index / 2;
    NodeFace& nf = coupling.faces[node][face_index];
    nf.albedo = condition.albedo;
    const double h_cm = node_width_cm(reactor, node, axis);
    for (std::size_t group = 0; group < energy_group_count; ++group) {
        coupling.d_tilde[face_slot(node, face, group)] =
            boundary_d_tilde(nf.albedo[group], cross_sections[node].diffusion_cm[group], h_cm);
    }
}

void check_inputs(const ReactorDescription& reactor,
                  std::span<const NodeCrossSections> cross_sections) {
    NTS_ASSERT(cross_sections.size() == reactor.node_count(),
               "one cross-section set is needed per node");
    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        for (std::size_t group = 0; group < energy_group_count; ++group) {
            NTS_ASSERT(cross_sections[node].diffusion_cm[group] > 0.0,
                       "every diffusion coefficient must be positive");
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            NTS_ASSERT(node_width_cm(reactor, node, axis) > 0.0, "node widths must be positive");
        }
    }
}

// A size × size matrix holding the sum of the triplets at each position, compressed. Eigen's
// setFromTriplets would do the same, but its stack buffer trips clang-analyzer false positives
// (unix.Malloc, optin.portability.UnixAPI) reported inside Eigen, where a NOLINT cannot reach.
SparseMatrix matrix_from_triplets(Eigen::Index size, const std::vector<Triplet>& triplets) {
    Eigen::VectorXi row_counts = Eigen::VectorXi::Zero(size);
    for (const Triplet& triplet : triplets) {
        ++row_counts[triplet.row()];
    }
    SparseMatrix matrix(size, size);
    matrix.reserve(row_counts);
    for (const Triplet& triplet : triplets) {
        matrix.coeffRef(triplet.row(), triplet.col()) += triplet.value();
    }
    matrix.makeCompressed();
    return matrix;
}

} // namespace

CmfdCoupling make_cmfd_coupling(const ReactorDescription& reactor,
                                std::span<const NodeCrossSections> cross_sections) {
    check_inputs(reactor, cross_sections);
    CmfdCoupling coupling;
    coupling.node_count = reactor.node_count();
    coupling.faces.resize(coupling.node_count);
    const std::size_t slot_count = coupling.node_count * face_count * energy_group_count;
    coupling.d_tilde.assign(slot_count, 0.0);
    // No coupling correction: D̂ = 0 (TM §4.3 with the D̂ of Eqs. (4.1), (4.5) set to zero).
    coupling.d_hat.assign(slot_count, 0.0);

    for (std::size_t node = 0; node < coupling.node_count; ++node) {
        const std::size_t position = reactor.node_positions[node];
        for (std::size_t face_index = 0; face_index < face_count; ++face_index) {
            const Face face = static_cast<Face>(face_index);
            const std::optional<std::size_t> adjacent = adjacent_position(reactor, position, face);
            const std::int64_t across =
                adjacent ? reactor.position_nodes[*adjacent] : std::int64_t{-1};
            if (across >= 0) {
                coupling.faces[node][face_index].neighbour = across;
                set_interior_faces(coupling, reactor, cross_sections, node, face,
                                   static_cast<std::size_t>(across));
            } else {
                // No neighbour: the box boundary, or an excluded position (the exterior).
                const BoundaryCondition& condition =
                    adjacent ? reactor.exterior : reactor.bounding_faces[face_index];
                set_boundary_faces(coupling, reactor, cross_sections, node, face, condition);
            }
        }
    }
    return coupling;
}

double face_current(const CmfdCoupling& coupling, std::span<const double> flux, std::size_t node,
                    Face face, std::size_t group) {
    NTS_ASSERT(flux.size() == coupling.node_count * energy_group_count,
               "flux must have two entries per node");
    const auto face_index = static_cast<std::size_t>(face);
    const bool plus = (face_index % 2) == 1;
    const double phi_node = flux[unknown_index(node, group)];
    const double d_tilde = coupling.d_tilde[face_slot(node, face, group)];
    const double d_hat = coupling.d_hat[face_slot(node, face, group)];
    const NodeFace& nf = coupling.faces[node][face_index];

    if (nf.neighbour < 0) {
        // TM Eq. (4.5): the missing neighbour's flux is zero.
        return plus ? ((d_tilde - d_hat) * phi_node) : -((d_tilde + d_hat) * phi_node);
    }
    const double phi_across = flux[unknown_index(static_cast<std::size_t>(nf.neighbour), group)];
    if (plus) {
        // TM Eq. (4.1) with `node` the left node l and the neighbour the right node r.
        return -(d_tilde * (phi_across - phi_node)) - (d_hat * (phi_across + phi_node));
    }
    // TM Eq. (4.1) with the neighbour the left node l and `node` the right node r.
    return -(d_tilde * (phi_node - phi_across)) - (d_hat * (phi_node + phi_across));
}

CmfdSystem assemble_cmfd_system(const ReactorDescription& reactor, const CmfdCoupling& coupling,
                                std::span<const NodeCrossSections> cross_sections) {
    NTS_ASSERT(coupling.node_count == reactor.node_count(),
               "the coupling was made for another reactor");
    NTS_ASSERT(cross_sections.size() == reactor.node_count(),
               "one cross-section set is needed per node");
    const std::size_t unknown_count = coupling.node_count * energy_group_count;
    std::vector<Triplet> migration_triplets;
    std::vector<Triplet> production_triplets;
    // Entries per node: per group, 3 axes × (diagonal + up to 2 neighbours) = 9, so 18 for the
    // two groups; plus removal (2) and scatter (1). Production: at most 2 per node.
    migration_triplets.reserve(coupling.node_count * ((2 * 9) + 3));
    production_triplets.reserve(coupling.node_count * 2);

    for (std::size_t node = 0; node < coupling.node_count; ++node) {
        const NodeCrossSections& xs = cross_sections[node];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double h_cm = node_width_cm(reactor, node, axis);
            const auto minus = static_cast<Face>(2 * axis);
            const auto plus = static_cast<Face>((2 * axis) + 1);
            check_shared_face(coupling, node, minus);
            check_shared_face(coupling, node, plus);
            const NodeFace& nf_minus = coupling.faces[node][static_cast<std::size_t>(minus)];
            const NodeFace& nf_plus = coupling.faces[node][static_cast<std::size_t>(plus)];

            for (std::size_t group = 0; group < energy_group_count; ++group) {
                const std::size_t row = unknown_index(node, group);
                const double tilde_plus = coupling.d_tilde[face_slot(node, plus, group)];
                const double hat_plus = coupling.d_hat[face_slot(node, plus, group)];
                const double tilde_minus = coupling.d_tilde[face_slot(node, minus, group)];
                const double hat_minus = coupling.d_hat[face_slot(node, minus, group)];

                // TM Eq. (4.6) divided by h_u (TM Eq. (3.9)): the node's own flux.
                const double diagonal = (tilde_plus - hat_plus + tilde_minus + hat_minus) / h_cm;
                migration_triplets.emplace_back(storage_index(row), storage_index(row), diagonal);
                // TM Eq. (4.6): the upper neighbour's flux, −(D̃⁺ + D̂⁺)/h.
                if (nf_plus.neighbour >= 0) {
                    const auto upper = static_cast<std::size_t>(nf_plus.neighbour);
                    migration_triplets.emplace_back(storage_index(row),
                                                    storage_index(unknown_index(upper, group)),
                                                    -(tilde_plus + hat_plus) / h_cm);
                }
                // TM Eq. (4.6): the lower neighbour's flux, −(D̃⁻ − D̂⁻)/h.
                if (nf_minus.neighbour >= 0) {
                    const auto lower = static_cast<std::size_t>(nf_minus.neighbour);
                    migration_triplets.emplace_back(storage_index(row),
                                                    storage_index(unknown_index(lower, group)),
                                                    -(tilde_minus - hat_minus) / h_cm);
                }
            }
        }

        // TM Eq. (4.7): removal in both rows, −Σ_12 in the group-2 row.
        const std::size_t row1 = unknown_index(node, 0);
        const std::size_t row2 = unknown_index(node, 1);
        migration_triplets.emplace_back(storage_index(row1), storage_index(row1),
                                        xs.removal_per_cm[0]);
        migration_triplets.emplace_back(storage_index(row2), storage_index(row2),
                                        xs.removal_per_cm[1]);
        migration_triplets.emplace_back(storage_index(row2), storage_index(row1),
                                        -xs.down_scatter_per_cm);

        // TM Eq. (4.7): fission production in the group-1 row only. Zero entries are left out,
        // so a row of F is non-zero exactly at a node with νΣ_f > 0.
        if (xs.nu_fission_per_cm[0] != 0.0) {
            production_triplets.emplace_back(storage_index(row1), storage_index(row1),
                                             xs.nu_fission_per_cm[0]);
        }
        if (xs.nu_fission_per_cm[1] != 0.0) {
            production_triplets.emplace_back(storage_index(row1), storage_index(row2),
                                             xs.nu_fission_per_cm[1]);
        }
    }

    const auto size = static_cast<Eigen::Index>(unknown_count);
    CmfdSystem system{.migration = matrix_from_triplets(size, migration_triplets),
                      .production = matrix_from_triplets(size, production_triplets)};
    return system;
}

} // namespace nts
