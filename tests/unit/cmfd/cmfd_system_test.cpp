// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/cmfd/cmfd_system.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <Eigen/Core>
#include <Eigen/SparseCore>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "../solve/core_fixture.hpp"

namespace nts::test {
namespace {

// Deterministic uniform values (splitmix64); no standard distributions, so the sequence is
// the same on every platform.
class ValueStream {
public:
    explicit ValueStream(std::uint64_t seed) : state_(seed) {}

    // Uniform in [lo, hi).
    [[nodiscard]] double uniform(double lo, double hi) {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        const double unit = static_cast<double>(z >> 11U) * 0x1.0p-53;
        return lo + ((hi - lo) * unit);
    }

private:
    std::uint64_t state_;
};

// Cell indices (i along x, j along y, k along z) of a node.
[[nodiscard]] std::array<std::size_t, 3> cell_of_node(const ReactorDescription& reactor,
                                                      std::size_t node) {
    const std::size_t position = reactor.node_positions[node];
    const std::size_t nx = reactor.nx();
    const std::size_t ny = reactor.ny();
    return {position % nx, (position / nx) % ny, position / (nx * ny)};
}

[[nodiscard]] const std::vector<double>& widths_of_axis(const ReactorDescription& reactor,
                                                        std::size_t axis) {
    if (axis == 0) {
        return reactor.width_x_cm;
    }
    if (axis == 1) {
        return reactor.width_y_cm;
    }
    return reactor.width_z_cm;
}

[[nodiscard]] double width_of(const ReactorDescription& reactor, std::size_t node,
                              std::size_t axis) {
    return widths_of_axis(reactor, axis)[cell_of_node(reactor, node)[axis]];
}

// True when the neighbour of `node` across `face` lies outside the box.
[[nodiscard]] bool outside_box(const ReactorDescription& reactor, std::size_t node, Face face) {
    const auto face_index = static_cast<std::size_t>(face);
    const std::size_t axis = face_index / 2;
    const std::size_t cell = cell_of_node(reactor, node)[axis];
    const bool plus = (face_index % 2) == 1;
    if (plus) {
        return cell + 1 >= widths_of_axis(reactor, axis).size();
    }
    return cell == 0;
}

// The node across `face` of `node` (independent of the implementation), or -1 when there is
// no node there (box boundary or excluded position).
[[nodiscard]] std::int64_t neighbour_of(const ReactorDescription& reactor, std::size_t node,
                                        Face face) {
    if (outside_box(reactor, node, face)) {
        return -1;
    }
    const auto face_index = static_cast<std::size_t>(face);
    const std::size_t axis = face_index / 2;
    const bool plus = (face_index % 2) == 1;
    std::array<std::size_t, 3> cell = cell_of_node(reactor, node);
    if (plus) {
        ++cell[axis];
    } else {
        --cell[axis];
    }
    const std::size_t position = (((cell[2] * reactor.ny()) + cell[1]) * reactor.nx()) + cell[0];
    return reactor.position_nodes[position];
}

// TM Eq. (4.2) with l the minus-side node.
[[nodiscard]] double expected_interior(double d_minus_cm, double d_plus_cm, double h_minus_cm,
                                       double h_plus_cm) {
    return (2.0 * d_minus_cm * d_plus_cm) / ((d_minus_cm * h_plus_cm) + (d_plus_cm * h_minus_cm));
}

// TM Eq. (4.4).
[[nodiscard]] double expected_boundary(double alpha, double d_cm, double h_cm) {
    return (2.0 * alpha * d_cm) / ((2.0 * d_cm) + (alpha * h_cm));
}

void expect_close(double got, double want) {
    EXPECT_NEAR(got, want, 1.0e-12 * std::abs(want));
}

[[nodiscard]] Eigen::VectorXd to_eigen(const std::vector<double>& values) {
    Eigen::VectorXd v(static_cast<Eigen::Index>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        v[static_cast<Eigen::Index>(i)] = values[i];
    }
    return v;
}

[[nodiscard]] std::array<BoundaryCondition, face_count>
all_faces(BoundaryKind kind, std::array<double, energy_group_count> albedo = {}) {
    std::array<BoundaryCondition, face_count> faces{};
    for (BoundaryCondition& face : faces) {
        face = boundary(kind, albedo);
    }
    return faces;
}

} // namespace

TEST(CmfdSolve, InteriorFaceUsesTmEquation42AndBothSidesAgree) {
    const ReactorDescription reactor =
        make_reactor({3.0, 5.0}, {1.0}, {1.0}, all_faces(BoundaryKind::reflective));
    const std::vector<NodeCrossSections> xs = {
        make_cross_sections(1.4, 0.4, 0.01, 0.08, 0.017, 0.006, 0.11),
        make_cross_sections(0.9, 0.2, 0.02, 0.10, 0.03, 0.004, 0.09)};
    const CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);
    ASSERT_EQ(coupling.node_count, 2U);
    EXPECT_EQ(coupling.faces[0][static_cast<std::size_t>(Face::x_plus)].neighbour, 1);
    EXPECT_EQ(coupling.faces[1][static_cast<std::size_t>(Face::x_minus)].neighbour, 0);

    for (std::size_t g = 0; g < energy_group_count; ++g) {
        const double want =
            expected_interior(xs[0].diffusion_cm[g], xs[1].diffusion_cm[g], 3.0, 5.0);
        expect_close(coupling.d_tilde[face_slot(0, Face::x_plus, g)], want);
        // Bit-identical on the other side of the face.
        EXPECT_EQ(coupling.d_tilde[face_slot(1, Face::x_minus, g)],
                  coupling.d_tilde[face_slot(0, Face::x_plus, g)]);
        EXPECT_EQ(coupling.d_hat[face_slot(0, Face::x_plus, g)], 0.0);
    }
}

TEST(CmfdSolve, FaceCurrentFollowsTmEquation41WithCouplingCorrection) {
    const ReactorDescription reactor =
        make_reactor({3.0, 5.0}, {1.0}, {1.0}, all_faces(BoundaryKind::reflective));
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);
    const double d_tilde = coupling.d_tilde[face_slot(0, Face::x_plus, 0)];
    const std::vector<double> phi = {1.0, 0.0, 0.0, 0.0}; // group 1 of node 0 only

    // Without D̂: J = −D̃ (φ_r − φ_l) with l = node 0 (TM Eq. (4.1)).
    EXPECT_NEAR(face_current(coupling, phi, 0, Face::x_plus, 0), d_tilde, 1.0e-15);
    EXPECT_NEAR(face_current(coupling, phi, 1, Face::x_minus, 0), d_tilde, 1.0e-15);

    // With a symmetric D̂ = 0.2 on the face: J = −D̃ (φ_r − φ_l) − D̂ (φ_r + φ_l).
    const double d_hat = 0.2;
    coupling.d_hat[face_slot(0, Face::x_plus, 0)] = d_hat;
    coupling.d_hat[face_slot(1, Face::x_minus, 0)] = d_hat;
    EXPECT_NEAR(face_current(coupling, phi, 0, Face::x_plus, 0), d_tilde - d_hat, 1.0e-15);
    EXPECT_NEAR(face_current(coupling, phi, 1, Face::x_minus, 0), d_tilde - d_hat, 1.0e-15);
}

TEST(CmfdSolve, BoundaryFacesTakeTheAlbedoOfTheirCondition) {
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::reflective),         boundary(BoundaryKind::vacuum),
        boundary(BoundaryKind::zero_flux),          boundary(BoundaryKind::albedo, {0.3, 0.7}),
        boundary(BoundaryKind::albedo, {0.1, 0.2}), boundary(BoundaryKind::vacuum)};
    const ReactorDescription reactor =
        make_reactor({2.0}, {5.0}, {7.0}, faces); // one node, every face bounding
    const std::vector<NodeCrossSections> xs = {example_fuel()};
    const CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);

    const std::array<double, face_count> lengths = {2.0, 2.0, 5.0, 5.0, 7.0, 7.0};
    for (std::size_t f = 0; f < face_count; ++f) {
        const Face face = static_cast<Face>(f);
        EXPECT_EQ(coupling.faces[0][f].neighbour, -1);
        EXPECT_EQ(coupling.faces[0][f].albedo, faces[f].albedo);
        for (std::size_t g = 0; g < energy_group_count; ++g) {
            const double want =
                expected_boundary(faces[f].albedo[g], xs[0].diffusion_cm[g], lengths[f]);
            expect_close(coupling.d_tilde[face_slot(0, face, g)], want);
            EXPECT_EQ(coupling.d_hat[face_slot(0, face, g)], 0.0);
        }
    }
    // A zero_flux face is the limit α → ∞: D̃_b → 2D / h.
    expect_close(coupling.d_tilde[face_slot(0, Face::y_minus, 0)], 2.0 * 1.4 / 5.0);
    EXPECT_EQ(coupling.d_tilde[face_slot(0, Face::x_minus, 0)], 0.0);
}

// TM Eq. (4.4) for albedos where 2αD overflows: D̃_b stays finite and tends to 2D/h, the
// zero_flux limit. The x_minus face has h = 2 cm, so the limit is D.
TEST(CmfdSolve, HugeAlbedoStaysFiniteAndTendsToTheZeroFluxLimit) {
    const double largest = std::numeric_limits<double>::max();
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::albedo, {1.0e307, largest}),
        boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective)};
    const ReactorDescription reactor = make_reactor({2.0}, {5.0}, {7.0}, faces);
    const std::vector<NodeCrossSections> xs = {example_fuel()};
    const CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);

    for (std::size_t g = 0; g < energy_group_count; ++g) {
        const double d_tilde = coupling.d_tilde[face_slot(0, Face::x_minus, g)];
        EXPECT_TRUE(std::isfinite(d_tilde));
        expect_close(d_tilde, xs[0].diffusion_cm[g]);
    }
}

TEST(CmfdSolve, ExcludedNeighbourUsesTheExteriorCondition) {
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::albedo, {0.3, 0.7}), boundary(BoundaryKind::zero_flux),
        boundary(BoundaryKind::reflective),         boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::reflective),         boundary(BoundaryKind::reflective)};
    const ReactorDescription reactor =
        make_reactor({2.0, 3.0, 4.0}, {5.0}, {7.0}, faces, boundary(BoundaryKind::vacuum), {1});
    ASSERT_EQ(reactor.node_count(), 2U);
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_reflector()};
    const CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);

    // Node 0 (x = 0, width 2) sees the excluded node 1 on its x_plus face: the exterior.
    EXPECT_EQ(coupling.faces[0][static_cast<std::size_t>(Face::x_plus)].neighbour, -1);
    expect_close(coupling.d_tilde[face_slot(0, Face::x_plus, 0)],
                 expected_boundary(vacuum_albedo, 1.4, 2.0));
    expect_close(coupling.d_tilde[face_slot(0, Face::x_plus, 1)],
                 expected_boundary(vacuum_albedo, 0.4, 2.0));
    // Node 1 (x = 2, width 4) sees it on x_minus.
    EXPECT_EQ(coupling.faces[1][static_cast<std::size_t>(Face::x_minus)].neighbour, -1);
    expect_close(coupling.d_tilde[face_slot(1, Face::x_minus, 0)],
                 expected_boundary(vacuum_albedo, 1.3, 4.0));
    // The bounding faces keep their own conditions.
    expect_close(coupling.d_tilde[face_slot(0, Face::x_minus, 0)],
                 expected_boundary(0.3, 1.4, 2.0));
    expect_close(coupling.d_tilde[face_slot(0, Face::x_minus, 1)],
                 expected_boundary(0.7, 0.4, 2.0));
    expect_close(coupling.d_tilde[face_slot(1, Face::x_plus, 0)],
                 expected_boundary(zero_flux_albedo, 1.3, 4.0));
    EXPECT_EQ(coupling.d_tilde[face_slot(1, Face::y_plus, 0)], 0.0);
}

TEST(CmfdSolve, LatticeWithExclusionMatchesNeighbourOracle) {
    const ReactorDescription reactor =
        make_reactor(uniform_widths(9.0, 3), uniform_widths(9.0, 3), uniform_widths(9.0, 3),
                     all_faces(BoundaryKind::reflective), boundary(BoundaryKind::vacuum), {26});
    ASSERT_EQ(reactor.node_count(), 26U);
    std::vector<NodeCrossSections> xs;
    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        const double d = 1.0 + (0.01 * static_cast<double>(node));
        xs.push_back(make_cross_sections(d, 0.4 + (0.005 * static_cast<double>(node)), 0.01, 0.08,
                                         0.017, 0.006, 0.11));
    }
    const CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);

    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        for (std::size_t f = 0; f < face_count; ++f) {
            const Face face = static_cast<Face>(f);
            const std::int64_t want_neighbour = neighbour_of(reactor, node, face);
            EXPECT_EQ(coupling.faces[node][f].neighbour, want_neighbour);
            const std::size_t axis = f / 2;
            const bool plus = (f % 2) == 1;
            const double h_cm = width_of(reactor, node, axis);
            for (std::size_t g = 0; g < energy_group_count; ++g) {
                const double d_cm = xs[node].diffusion_cm[g];
                const double got = coupling.d_tilde[face_slot(node, face, g)];
                if (want_neighbour >= 0) {
                    const auto other = static_cast<std::size_t>(want_neighbour);
                    const std::size_t left = plus ? node : other;
                    const std::size_t right = plus ? other : node;
                    expect_close(got, expected_interior(xs[left].diffusion_cm[g],
                                                        xs[right].diffusion_cm[g],
                                                        width_of(reactor, left, axis),
                                                        width_of(reactor, right, axis)));
                    // The shared face of the other node is the same coupling.
                    const auto opposite = static_cast<Face>(f ^ std::size_t{1});
                    EXPECT_EQ(coupling.faces[other][f ^ std::size_t{1}].neighbour,
                              static_cast<std::int64_t>(node));
                    EXPECT_EQ(coupling.d_tilde[face_slot(other, opposite, g)], got);
                } else if (outside_box(reactor, node, face)) {
                    expect_close(got, 0.0); // reflective bounding face
                } else {
                    expect_close(got, expected_boundary(vacuum_albedo, d_cm, h_cm)); // exterior
                }
            }
        }
    }

    // Spot checks of the excluded corner (2, 2, 2) = position 26.
    // Node 25 is position (1, 2, 2): its x_plus face meets the excluded position.
    EXPECT_EQ(coupling.faces[25][static_cast<std::size_t>(Face::x_plus)].neighbour, -1);
    expect_close(coupling.d_tilde[face_slot(25, Face::x_plus, 0)],
                 expected_boundary(vacuum_albedo, xs[25].diffusion_cm[0], 3.0));
    // Node 17 is position (2, 2, 1): its z_plus face meets the excluded corner, its x_plus face
    // the box boundary.
    EXPECT_EQ(coupling.faces[17][static_cast<std::size_t>(Face::z_plus)].neighbour, -1);
    expect_close(coupling.d_tilde[face_slot(17, Face::z_plus, 1)],
                 expected_boundary(vacuum_albedo, xs[17].diffusion_cm[1], 3.0));
    expect_close(coupling.d_tilde[face_slot(17, Face::x_plus, 0)], 0.0);
}

namespace {

// Random D̂: one value per interface, shared by both nodes (the assembly asserts this), and a
// random value on every face without neighbour.
void randomize_d_hat(CmfdCoupling& coupling, ValueStream& rng) {
    for (std::size_t node = 0; node < coupling.faces.size(); ++node) {
        for (std::size_t f = 0; f < face_count; ++f) {
            const std::int64_t neighbour = coupling.faces[node][f].neighbour;
            if (neighbour >= 0 && (f % 2) == 0) {
                continue; // set from the plus side of the same interface
            }
            for (std::size_t g = 0; g < energy_group_count; ++g) {
                const double value = rng.uniform(-0.2, 0.2);
                coupling.d_hat[face_slot(node, static_cast<Face>(f), g)] = value;
                if (neighbour >= 0) {
                    const auto other = static_cast<std::size_t>(neighbour);
                    coupling.d_hat[face_slot(other, static_cast<Face>(f - 1), g)] = value;
                }
            }
        }
    }
}

} // namespace

TEST(CmfdSolve, RandomReactorMatchesFaceCurrentsAndSystemMatrices) {
    const std::array<BoundaryCondition, face_count> faces = {
        boundary(BoundaryKind::vacuum),     boundary(BoundaryKind::reflective),
        boundary(BoundaryKind::zero_flux),  boundary(BoundaryKind::albedo, {0.25, 0.4}),
        boundary(BoundaryKind::reflective), boundary(BoundaryKind::albedo, {0.1, 0.9})};
    const ReactorDescription reactor =
        make_reactor({1.5, 2.5, 3.0}, {2.0, 4.0}, {1.0, 2.0}, faces,
                     boundary(BoundaryKind::albedo, {0.6, 0.3}), {7});
    const std::size_t node_count = reactor.node_count();
    ASSERT_EQ(node_count, 11U);

    ValueStream rng(0x5EED0001ULL);
    std::vector<NodeCrossSections> xs;
    xs.reserve(node_count);
    for (std::size_t node = 0; node < node_count; ++node) {
        xs.push_back(make_cross_sections(rng.uniform(0.3, 1.5), rng.uniform(0.2, 0.9), 0.01, 0.08,
                                         0.017, rng.uniform(0.001, 0.02), rng.uniform(0.05, 0.2)));
    }

    CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);
    randomize_d_hat(coupling, rng);

    const CmfdSystem system = assemble_cmfd_system(reactor, coupling, xs);
    std::vector<double> phi(2 * node_count);
    for (double& value : phi) {
        value = rng.uniform(0.5, 2.0);
    }
    const Eigen::VectorXd phi_vec = to_eigen(phi);
    const Eigen::VectorXd m_phi = system.migration * phi_vec;

    // Row by row: M φ = Σ_u (J_{u+} − J_{u−}) / h_u + Σ_r φ − (group 2) Σ_12 φ_1 (TM Eqs. (3.9),
    // (4.6), (4.7)), with the currents of TM Eqs. (4.1), (4.5).
    for (std::size_t node = 0; node < node_count; ++node) {
        for (std::size_t g = 0; g < energy_group_count; ++g) {
            double want = 0.0;
            double scale = 0.0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto minus = static_cast<Face>(2 * axis);
                const auto plus = static_cast<Face>((2 * axis) + 1);
                const double h_cm = width_of(reactor, node, axis);
                const double j_plus = face_current(coupling, phi, node, plus, g);
                const double j_minus = face_current(coupling, phi, node, minus, g);
                want += (j_plus - j_minus) / h_cm;
                scale += (std::abs(j_plus) + std::abs(j_minus)) / h_cm;
            }
            const double removal = xs[node].removal_per_cm[g] * phi[unknown_index(node, g)];
            want += removal;
            scale += std::abs(removal);
            if (g == 1) {
                const double scatter = -xs[node].down_scatter_per_cm * phi[unknown_index(node, 0)];
                want += scatter;
                scale += std::abs(scatter);
            }
            EXPECT_NEAR(m_phi[static_cast<Eigen::Index>(unknown_index(node, g))], want,
                        1.0e-12 * scale);
        }
    }

    // F: only group-1 rows, (F φ)_{n,1} = νΣ_f1 φ_1 + νΣ_f2 φ_2, and no group-2 entries.
    const Eigen::VectorXd f_phi = system.production * phi_vec;
    for (std::size_t node = 0; node < node_count; ++node) {
        const auto row1 = static_cast<Eigen::Index>(unknown_index(node, 0));
        const auto row2 = static_cast<Eigen::Index>(unknown_index(node, 1));
        EXPECT_EQ(system.production.innerVector(row2).nonZeros(), 0);
        const double want = (xs[node].nu_fission_per_cm[0] * phi[unknown_index(node, 0)]) +
                            (xs[node].nu_fission_per_cm[1] * phi[unknown_index(node, 1)]);
        EXPECT_NEAR(f_phi[row1], want, 1.0e-12 * (std::abs(want) + 1.0));
    }
}

TEST(CmfdSolve, AssemblyRejectsAsymmetricCouplingOnSharedFace) {
    const ReactorDescription reactor =
        make_reactor({1.0, 1.0}, {1.0}, {1.0}, all_faces(BoundaryKind::reflective));
    const std::vector<NodeCrossSections> xs = {example_fuel(), example_fuel()};
    CmfdCoupling coupling = make_cmfd_coupling(reactor, xs);
    // D̂ set on one side of the interface only: the two nodes disagree on the face.
    coupling.d_hat[face_slot(0, Face::x_plus, 0)] = 0.1;
    EXPECT_DEATH(static_cast<void>(assemble_cmfd_system(reactor, coupling, xs)), "");
}

} // namespace nts::test
