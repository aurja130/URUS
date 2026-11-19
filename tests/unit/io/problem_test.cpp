// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::BoundaryKind;
using nts::Face;
using nts::face_name;
using nts::KernelChoice;
using nts::load_problem;
using nts::Problem;
using nts::test::all_text;
using nts::test::expect_one;
using nts::test::FixtureGroup;
using nts::test::float64_scalar;
using nts::test::has_diagnostic;
using nts::test::has_entry_diagnostic;
using nts::test::integer_scalar;
using nts::test::make_valid_problem;
using nts::test::ProblemTest;
using nts::test::string_scalar;

// ----- The valid problem of problem_fixture.hpp, restated from contract §3 to §7 -------------

// Boundary albedos per kind (contract §4 table), and the `albedo` kind's values.
constexpr double reflective_albedo = 0.0;
constexpr double vacuum_albedo = 0.5;
constexpr double zero_flux_albedo = 1.0e30;
constexpr double fast_albedo = 0.3;
constexpr double thermal_albedo = 0.6;

struct FaceRow {
    Face face;
    BoundaryKind kind;
    std::array<double, 2> albedo;
};

// The six bounding faces of the valid problem.
constexpr std::array<FaceRow, 6> valid_faces{{
    {.face = Face::x_minus, .kind = BoundaryKind::vacuum, .albedo = {vacuum_albedo, vacuum_albedo}},
    {.face = Face::x_plus,
     .kind = BoundaryKind::reflective,
     .albedo = {reflective_albedo, reflective_albedo}},
    {.face = Face::y_minus,
     .kind = BoundaryKind::zero_flux,
     .albedo = {zero_flux_albedo, zero_flux_albedo}},
    {.face = Face::y_plus, .kind = BoundaryKind::albedo, .albedo = {fast_albedo, thermal_albedo}},
    {.face = Face::z_minus, .kind = BoundaryKind::vacuum, .albedo = {vacuum_albedo, vacuum_albedo}},
    {.face = Face::z_plus,
     .kind = BoundaryKind::reflective,
     .albedo = {reflective_albedo, reflective_albedo}},
}};

// Positions are row-major [z][y][x] with nx = 3, ny = 2, nz = 2: position = i + 3 j + 6 k.
// Excluded: (0, 0, 0) = 0 and (0, 0, 1) = 6. Position (2, 1, 1) = 11 is of composition 1.
constexpr std::size_t valid_node_count = 10;

// ----- Successful read (contract §1 to §7) --------------------------------------------------

TEST_F(ProblemTest, ReadsEveryFieldOfTheValidProblem) {
    const Problem loaded = accepted();
    const auto& reactor = loaded.input.reactor;

    // Geometry (§3).
    EXPECT_EQ(reactor.width_x_cm, (std::vector<double>{10.0, 21.5, 10.0}));
    EXPECT_EQ(reactor.width_y_cm, (std::vector<double>{15.0, 15.0}));
    EXPECT_EQ(reactor.width_z_cm, (std::vector<double>{20.0, 30.0}));
    const std::vector<std::int64_t> expected_map{-1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
    EXPECT_EQ(reactor.composition_map, expected_map);
    EXPECT_EQ(reactor.node_count(), valid_node_count);
    const std::vector<std::size_t> expected_nodes{1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
    EXPECT_EQ(reactor.node_positions, expected_nodes);
    const std::vector<std::int64_t> expected_position_nodes{-1, 0, 1, 2, 3, 4, -1, 5, 6, 7, 8, 9};
    EXPECT_EQ(reactor.position_nodes, expected_position_nodes);

    // Boundary conditions (§4), per face and per energy group, and the exterior face.
    for (const FaceRow& row : valid_faces) {
        SCOPED_TRACE(std::string(face_name(row.face)));
        const auto& condition = reactor.bounding_faces[static_cast<std::size_t>(row.face)];
        EXPECT_EQ(condition.kind, row.kind);
        for (std::size_t group = 0; group < row.albedo.size(); ++group) {
            EXPECT_DOUBLE_EQ(condition.albedo[group], row.albedo[group]) << "group " << group;
        }
    }
    EXPECT_EQ(reactor.exterior.kind, BoundaryKind::vacuum);
    for (const double albedo : reactor.exterior.albedo) {
        EXPECT_DOUBLE_EQ(albedo, vacuum_albedo);
    }

    // Library binding (§5).
    const auto& binding = loaded.input.binding;
    EXPECT_EQ(binding.library_file, library_file());
    ASSERT_EQ(binding.compositions.size(), 2U);
    EXPECT_EQ(binding.compositions[0].library_set, "test_set");
    EXPECT_EQ(binding.compositions[0].library_composition, "fuel");
    EXPECT_EQ(binding.compositions[1].library_set, "test_set");
    EXPECT_EQ(binding.compositions[1].library_composition, "reflector");
    ASSERT_EQ(loaded.compositions.size(), 2U);
    EXPECT_EQ(loaded.compositions[0].set, 0U);
    EXPECT_EQ(loaded.compositions[0].composition, 0U);
    EXPECT_EQ(loaded.compositions[1].set, 0U);
    EXPECT_EQ(loaded.compositions[1].composition, 1U);
    ASSERT_FALSE(loaded.library.composition_sets.empty());
    EXPECT_EQ(loaded.library.composition_sets.front().name, "test_set");

    // Operating state (§6): a scalar state applies to every node.
    const auto& state = loaded.input.operating_state;
    EXPECT_DOUBLE_EQ(state.thermal_power_w, 1.0e6);
    ASSERT_EQ(state.node_states.size(), valid_node_count);
    for (const auto& node : state.node_states) {
        EXPECT_DOUBLE_EQ(node.doppler_temperature_k, 700.0);
        EXPECT_DOUBLE_EQ(node.moderator_density_kg_m3, 700.0);
        EXPECT_DOUBLE_EQ(node.moderator_temperature_k, 560.0);
        EXPECT_DOUBLE_EQ(node.boron_ppm, 500.0);
    }

    // Solver options (§7): no group, so every default.
    const auto& options = loaded.input.solver_options;
    EXPECT_EQ(options.kernel, KernelChoice::hybrid);
    EXPECT_DOUBLE_EQ(options.kernel_switch_threshold, 0.01);
    EXPECT_DOUBLE_EQ(options.wielandt_shift, 0.04);
    EXPECT_DOUBLE_EQ(options.eigenvalue_tolerance, 5.0e-6);
    EXPECT_DOUBLE_EQ(options.fission_source_l2_tolerance, 5.0e-5);
    EXPECT_DOUBLE_EQ(options.fission_source_linf_tolerance, 5.0e-4);
    EXPECT_DOUBLE_EQ(options.inner_reduction, 0.01);
    EXPECT_EQ(options.inner_iteration_limit, 1);
    EXPECT_EQ(options.outer_iteration_limit, 1000);
    EXPECT_EQ(options.nodal_update_period, 5);
    EXPECT_DOUBLE_EQ(options.coupling_relaxation, 1.0);
    EXPECT_FALSE(options.adjoint);
}

// The working directory is not the problem's directory, so a read succeeds only when the
// relative path is resolved against the directory that holds the problem input (§5).
TEST_F(ProblemTest, ResolvesARelativeLibraryPathAgainstTheProblemDirectory) {
    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.binding.library_file, library_file());
}

TEST_F(ProblemTest, ReadsAnAbsoluteLibraryPathAsWritten) {
    problem().groups["library"].attributes["path"] = library_file().string();
    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.binding.library_file, library_file());
}

TEST_F(ProblemTest, IgnoresARootAttributeTheContractDoesNotName) {
    problem().attributes["comment"] = std::string{"not part of the contract"};
    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.reactor.node_count(), valid_node_count);
}

// ----- Check 1: the file exists and opens as HDF5 (§8) --------------------------------------

TEST_F(ProblemTest, RejectsAMissingFile) {
    // Nothing is written: the problem input does not exist.
    const auto result = load_problem(problem_file());
    ASSERT_FALSE(result.has_value());
    expect_one(result.error(), "/", "does not exist");
    EXPECT_EQ(result.error().front().file, problem_file().string());
}

TEST_F(ProblemTest, RejectsAFileThatIsNotHdf5) {
    std::filesystem::create_directories(problem_file().parent_path());
    std::ofstream(problem_file()) << "not an HDF5 file\n";
    const auto result = load_problem(problem_file());
    ASSERT_FALSE(result.has_value());
    expect_one(result.error(), "/", "not an HDF5 file");
}

// ----- Check 2: format_version; on failure nothing else is checked (§2, §8) -----------------

// The geometry is erased as well: nothing else may be reported once the version fails.
TEST_F(ProblemTest, RejectsAMissingFormatVersion) {
    problem().attributes.erase("format_version");
    problem().groups.erase("geometry");
    expect_one(rejected(), "/@format_version", "attribute is missing");
}

TEST_F(ProblemTest, RejectsAFormatVersionThatIsNotAnInteger) {
    problem().attributes["format_version"] = std::string{"1"};
    problem().groups.erase("geometry");
    expect_one(rejected(), "/@format_version", "attribute is not an integer");
}

TEST_F(ProblemTest, RejectsAnUnsupportedFormatVersion) {
    problem().attributes["format_version"] = std::int64_t{2};
    problem().groups.erase("geometry");
    expect_one(rejected(), "/@format_version", "format version 2 is not supported");
}

// ----- Check 3: root members (§2) and unknown members (§1, §8) ------------------------------

TEST_F(ProblemTest, RejectsAnUnknownRootGroup) {
    problem().groups["extra"] = FixtureGroup{};
    expect_one(rejected(), "/extra", "not a member of the problem input contract");
}

TEST_F(ProblemTest, RejectsAnUnknownRootDataset) {
    problem().datasets["extra"] = integer_scalar(1);
    expect_one(rejected(), "/extra", "not a member of the problem input contract");
}

TEST_F(ProblemTest, RejectsEachMissingRequiredRootGroup) {
    // A missing compositions group also leaves the map's upper bound unchecked, so the count of
    // diagnostics is not asserted; the missing group itself must be reported.
    for (const std::string group :
         {"geometry", "boundary_conditions", "library", "compositions", "operating_state"}) {
        SCOPED_TRACE(group);
        problem() = make_valid_problem("library.h5");
        problem().groups.erase(group);
        EXPECT_TRUE(has_diagnostic(rejected(), "/" + group, "group is missing"));
    }
}

TEST_F(ProblemTest, ReportsAMissingRootGroupBeforeTheGeometry) {
    // §8 item 3 (root members) precedes item 4 (geometry): the missing group is reported first.
    problem().groups["geometry"].datasets["width_x"].numbers[1] = -1.0;
    problem().groups.erase("operating_state");
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics[0].location, "/operating_state");
    EXPECT_EQ(diagnostics[0].message, "group is missing");
    EXPECT_EQ(diagnostics[1].location, "/geometry/width_x");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/width_x", "index 1",
                                     "width -1 cm is not finite and positive"));
}

TEST_F(ProblemTest, ReportsARootMemberOfTheWrongKindOnce) {
    // §2: boundary_conditions must be a group; its section does not report the defect again.
    problem().groups.erase("boundary_conditions");
    problem().datasets["boundary_conditions"] = integer_scalar(1);
    expect_one(rejected(), "/boundary_conditions", "not a group");
}

// ----- Collection: every failure is reported, in contract §8 order -------------------------

TEST_F(ProblemTest, ReportsTheDefectsOfThreeSectionsInContractOrder) {
    // Geometry (§3): width_x[1] is not positive.
    problem().groups["geometry"].datasets["width_x"].numbers[1] = -1.0;
    // Operating state (§6): boron is not non-negative.
    problem().groups["operating_state"].datasets["boron"] = float64_scalar(-1.0, "ppm");
    // Solver options (§7): an unknown kernel.
    problem().groups["solver_options"].datasets["kernel"] = string_scalar("bogus");

    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 3U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics[0].location, "/geometry/width_x");
    EXPECT_EQ(diagnostics[1].location, "/operating_state/boron");
    EXPECT_EQ(diagnostics[2].location, "/solver_options/kernel");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/width_x", "index 1",
                                     "width -1 cm is not finite and positive"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/operating_state/boron", "-1 ppm"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/solver_options/kernel", "unknown kernel 'bogus'"));
}

} // namespace
