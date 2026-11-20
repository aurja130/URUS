// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The problem input (docs/contracts/problem_input.md, format version 2) and the problem it
// defines once bound to its nodal library. Section numbers below are the contract's.

#include "nts/common/diagnostic.hpp"
#include "nts/io/nodal_library.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nts {

// ----- Reactor description (§3, §4) --------------------------------------------------------

// The six bounding faces of the box, in the order of the library's discontinuity factors.
enum class Face : std::uint8_t { x_minus, x_plus, y_minus, y_plus, z_minus, z_plus };

enum class BoundaryKind : std::uint8_t { reflective, vacuum, zero_flux, albedo };

// α_b of TM Eq. (4.4) for each kind but `albedo` (TM §4.2).
inline constexpr double reflective_albedo = 0.0;
inline constexpr double vacuum_albedo = 0.5;
inline constexpr double zero_flux_albedo = 1.0e30;

// J_out = α_b φ_s per energy group (TM Eq. (4.4)). `albedo` holds α_b for every kind: the
// fixed value of the kind, or the input values for BoundaryKind::albedo.
struct BoundaryCondition {
    BoundaryKind kind = BoundaryKind::reflective;
    std::array<double, energy_group_count> albedo{};
};

// Positions are row-major [z][y][x], x fastest; nodes are the positions whose map value is
// not -1, numbered in position order.
struct ReactorDescription {
    std::vector<double> width_x_cm;
    std::vector<double> width_y_cm;
    std::vector<double> width_z_cm;
    std::vector<std::int64_t> composition_map; // per position; -1 excluded
    std::vector<std::size_t> node_positions;   // per node: its position
    std::vector<std::int64_t> position_nodes;  // per position: its node, or -1 when excluded
    std::array<BoundaryCondition, face_count> bounding_faces{}; // in Face order
    BoundaryCondition exterior{}; // reflective when absent (allowed only without exclusions)

    [[nodiscard]] std::size_t nx() const { return width_x_cm.size(); }
    [[nodiscard]] std::size_t ny() const { return width_y_cm.size(); }
    [[nodiscard]] std::size_t nz() const { return width_z_cm.size(); }
    [[nodiscard]] std::size_t position_count() const { return nx() * ny() * nz(); }
    [[nodiscard]] std::size_t node_count() const { return node_positions.size(); }
};

// ----- Library binding (§5) ----------------------------------------------------------------

struct InputComposition {
    std::string library_set;
    std::string library_composition;
};

struct LibraryBinding {
    std::filesystem::path library_file; // resolved against the reactor document file's directory
    std::vector<InputComposition> compositions;
};

// ----- Operating state (§6) ----------------------------------------------------------------

// The state a node's cross sections are interpolated at (TM §7.1).
struct NodeState {
    double doppler_temperature_k = 0.0;
    double moderator_density_kg_m3 = 0.0;
    double moderator_temperature_k = 0.0;
    double boron_ppm = 0.0;
};

struct OperatingState {
    double thermal_power_w = 0.0;
    std::vector<NodeState> node_states; // per node
};

// ----- Solver options (§7) -----------------------------------------------------------------

enum class KernelChoice : std::uint8_t { finite_difference, nem, anm, hybrid };

// Defaults: contract §7.
struct SolverOptions {
    KernelChoice kernel = KernelChoice::hybrid;
    double kernel_switch_threshold = 0.01;         // TM Eq. (5.27)
    double wielandt_shift = 0.04;                  // TM Eq. (4.8)
    double eigenvalue_tolerance = 5.0e-6;          // TM Eq. (4.10)
    double fission_source_l2_tolerance = 5.0e-5;   // TM Eq. (4.10)
    double fission_source_linf_tolerance = 5.0e-4; // TM Eq. (4.10)
    double inner_reduction = 0.01;                 // TM §4.5
    std::int64_t inner_iteration_limit = 1;
    std::int64_t outer_iteration_limit = 1000;
    std::int64_t nodal_update_period = 5; // TM §6.1
    double coupling_relaxation = 1.0;     // TM §6.2
    bool adjoint = false;
};

// ----- The problem -------------------------------------------------------------------------

// The four input documents (§2), in the order of the problem input's root table.
enum class InputDocument : std::uint8_t { reactor, initial_state, control_signals, settings };
inline constexpr std::size_t input_document_count = 4;

struct ProblemInput {
    std::filesystem::path file; // the problem input
    // The file holding each document, in InputDocument order: `file` for a document in place,
    // its document file when linked, empty when an optional document is absent.
    std::array<std::filesystem::path, input_document_count> document_files;
    ReactorDescription reactor;
    LibraryBinding binding;          // library_file resolved against the reactor document's file
    OperatingState operating_state;  // the initial state document (§6)
    SolverOptions solver_options;    // the settings document (§7)
    bool allow_table_clamps = false; // the settings document (§7)
};

// Input composition c is composition `composition` of library.composition_sets[set].
struct BoundComposition {
    std::size_t set = 0;
    std::size_t composition = 0;
};

struct Problem {
    ProblemInput input;
    NodalLibrary library;
    std::vector<BoundComposition> compositions; // per input composition
};

// Reads the problem input and its library and checks every rule of contract §8; all
// failures are returned together.
[[nodiscard]] std::expected<Problem, Diagnostics> load_problem(const std::filesystem::path& file);

[[nodiscard]] std::string_view input_document_name(InputDocument document); // "reactor", ...

[[nodiscard]] std::string_view face_name(Face face);                    // "x_minus", ...
[[nodiscard]] std::string_view boundary_kind_name(BoundaryKind kind);   // "reflective", ...
[[nodiscard]] std::string_view kernel_choice_name(KernelChoice kernel); // "hybrid", ...

} // namespace nts
