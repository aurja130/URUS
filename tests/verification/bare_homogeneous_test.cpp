// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// Bare homogeneous two-group benchmark (plan §5): the CMFD eigenvalue of each reference case
// converges to the analytic k of tests/verification/bare_homogeneous_reference.csv at second
// order under mesh refinement.

#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/solve/steady_state.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core_fixture.hpp"

namespace nts::test {
namespace {

constexpr double thermal_power_w = 1.0e6;
constexpr double order_target = 2.0;
constexpr double order_tolerance = 0.1;
// Finest error must exceed the solver noise: 1e3 * eps_k with eps_k = 1e-12 (plan §5).
constexpr double finest_error_floor = 1.0e-9;

constexpr std::array<std::string_view, face_count> face_names{"x_minus", "x_plus",  "y_minus",
                                                              "y_plus",  "z_minus", "z_plus"};

struct ReferenceCase {
    std::string name;
    std::string geometry; // "slab_x", "slab_y", "slab_z" or "cuboid"
    std::array<BoundaryCondition, face_count> faces{};
    std::array<double, 3> lengths_cm{};
    NodeCrossSections xs{};
    double k_ref = 0.0;
};

[[nodiscard]] std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = line.find(',', start);
        const std::size_t end = (comma == std::string::npos) ? line.size() : comma;
        fields.push_back(line.substr(start, end - start));
        if (comma == std::string::npos) {
            return fields;
        }
        start = comma + 1;
    }
}

[[nodiscard]] std::size_t column_of(const std::vector<std::string>& header, std::string_view name) {
    for (std::size_t column = 0; column < header.size(); ++column) {
        if (header[column] == name) {
            return column;
        }
    }
    throw std::runtime_error("reference CSV has no column " + std::string(name));
}

[[nodiscard]] double parse_double(const std::string& text) {
    double value = 0.0;
    const char* last = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), last, value);
    if (ec != std::errc{} || ptr != last) {
        throw std::runtime_error("malformed number in reference CSV: " + text);
    }
    return value;
}

[[nodiscard]] BoundaryKind parse_kind(const std::string& text) {
    if (text == "reflective") {
        return BoundaryKind::reflective;
    }
    if (text == "vacuum") {
        return BoundaryKind::vacuum;
    }
    if (text == "zero_flux") {
        return BoundaryKind::zero_flux;
    }
    if (text == "albedo") {
        return BoundaryKind::albedo;
    }
    throw std::runtime_error("unknown boundary kind in reference CSV: " + text);
}

[[nodiscard]] std::vector<ReferenceCase> load_reference_cases() {
    std::ifstream in(NTS_BARE_HOMOGENEOUS_REFERENCE);
    if (!in) {
        throw std::runtime_error(std::string("cannot open ") + NTS_BARE_HOMOGENEOUS_REFERENCE);
    }
    std::string line;
    if (!std::getline(in, line)) {
        throw std::runtime_error("reference CSV is empty");
    }
    const std::vector<std::string> header = split_fields(line);

    std::vector<ReferenceCase> cases;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> row = split_fields(line);
        if (row.size() != header.size()) {
            throw std::runtime_error("reference CSV row has the wrong number of fields: " +
                                     row.front());
        }
        const auto field = [&](std::string_view name) -> const std::string& {
            return row[column_of(header, name)];
        };
        const auto number = [&](std::string_view name) { return parse_double(field(name)); };

        ReferenceCase reference;
        reference.name = field("case");
        reference.geometry = field("geometry");
        for (std::size_t face = 0; face < face_count; ++face) {
            const std::string prefix(face_names[face]);
            const BoundaryKind kind = parse_kind(field(prefix + "_kind"));
            const std::array<double, energy_group_count> albedo{number(prefix + "_albedo_1"),
                                                                number(prefix + "_albedo_2")};
            reference.faces[face] = boundary(kind, albedo);
        }
        reference.lengths_cm = {number("lx_cm"), number("ly_cm"), number("lz_cm")};
        reference.xs = make_cross_sections(
            number("d1_cm"), number("d2_cm"), number("sigma_a1_per_cm"), number("sigma_a2_per_cm"),
            number("sigma_12_per_cm"), number("nu_sigma_f1_per_cm"), number("nu_sigma_f2_per_cm"));
        reference.k_ref = number("k_ref");
        cases.push_back(std::move(reference));
    }
    if (cases.empty()) {
        throw std::runtime_error("reference CSV has no cases");
    }
    return cases;
}

// Mesh counts of the refinement study, coarse to fine. A slab is refined only along its axis
// (one transverse node), by factors of 2; the albedo slabs reach the asymptotic range only
// from 20 nodes on (observed orders 1.84 and 1.95 at 10-20-40-80). A cuboid is refined along all
// three axes in steps of 3 nodes: the incomplete-LU factorisation at every outer iteration
// makes 24^3 take minutes in a Debug build, so the finest cuboid mesh is 12^3.
[[nodiscard]] std::vector<std::size_t> mesh_counts(const std::string& geometry) {
    if (geometry == "cuboid") {
        return {3, 6, 9, 12};
    }
    return {20, 40, 80, 160};
}

[[nodiscard]] std::size_t slab_axis_of(const std::string& geometry) {
    if (geometry == "slab_x") {
        return 0;
    }
    if (geometry == "slab_y") {
        return 1;
    }
    if (geometry == "slab_z") {
        return 2;
    }
    throw std::runtime_error("unknown geometry in reference CSV: " + geometry);
}

[[nodiscard]] ReactorDescription build_reactor(const ReferenceCase& reference, std::size_t mesh) {
    const bool cuboid = reference.geometry == "cuboid";
    const std::size_t slab_axis = cuboid ? 0 : slab_axis_of(reference.geometry);
    std::array<std::vector<double>, 3> widths;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const bool refined = cuboid || axis == slab_axis;
        widths[axis] = refined ? uniform_widths(reference.lengths_cm[axis], mesh)
                               : std::vector<double>{reference.lengths_cm[axis]};
    }
    return make_reactor(widths[0], widths[1], widths[2], reference.faces);
}

[[nodiscard]] SteadyState solve(const ReferenceCase& reference, std::size_t mesh) {
    const ReactorDescription reactor = build_reactor(reference, mesh);
    const std::vector<NodeCrossSections> xs(reactor.node_count(), reference.xs);
    SteadyState state = solve_steady_state(reactor, xs, verification_options(), thermal_power_w);
    EXPECT_TRUE(state.convergence.converged) << reference.name << " mesh " << mesh;
    return state;
}

TEST(BareHomogeneous, MeshRefinementConvergesToAnalyticKAtSecondOrder) {
    for (const ReferenceCase& reference : load_reference_cases()) {
        SCOPED_TRACE(reference.name);
        const std::vector<std::size_t> meshes = mesh_counts(reference.geometry);
        std::vector<double> errors;
        errors.reserve(meshes.size());
        for (const std::size_t mesh : meshes) {
            const SteadyState state = solve(reference, mesh);
            const ConvergenceState& c = state.convergence;
            errors.push_back(std::abs(state.k_eff - reference.k_ref));
            std::println("{} mesh {}: k = {:.12f}, |k - k_ref| = {:.3e}; converged {}, outer {}, "
                         "inner {}, dk {:.1e}, dL2 {:.1e}, dLinf {:.1e}, residual {:.1e}",
                         reference.name, mesh, state.k_eff, errors.back(), c.converged,
                         c.outer_iterations, c.inner_iterations, c.eigenvalue_change,
                         c.fission_source_l2_change, c.fission_source_linf_change, c.residual);
        }

        // Orders from the three finest meshes, p = ln(e_coarse / e_fine) / ln(n_fine / n_coarse).
        const std::size_t n = errors.size();
        const bool resolved = errors[n - 3] > finest_error_floor &&
                              errors[n - 2] > finest_error_floor &&
                              errors[n - 1] > finest_error_floor;
        EXPECT_TRUE(resolved) << "finest errors do not exceed the solver noise floor";
        if (!resolved) {
            continue;
        }
        const auto order = [&](std::size_t coarse, std::size_t fine) {
            return std::log(errors[coarse] / errors[fine]) /
                   std::log(static_cast<double>(meshes[fine]) /
                            static_cast<double>(meshes[coarse]));
        };
        const double order_coarse = order(n - 3, n - 2);
        const double order_fine = order(n - 2, n - 1);
        EXPECT_NEAR(order_coarse, order_target, order_tolerance);
        EXPECT_NEAR(order_fine, order_target, order_tolerance);
    }
}

} // namespace
} // namespace nts::test
