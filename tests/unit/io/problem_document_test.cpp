// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// The four documents of the problem input (docs/contracts/problem_input.md §2): linking, the
// checks each document meets, the library coverage of the node states on all four axes (§6,
// check 11), and the resolution of relative names.

#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "library_fixture.hpp"
#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::InputDocument;
using nts::Problem;
using nts::test::all_text;
using nts::test::FixtureAxis;
using nts::test::FixtureDataset;
using nts::test::FixtureExternalLink;
using nts::test::FixtureType;
using nts::test::float64_array;
using nts::test::float64_scalar;
using nts::test::has_diagnostic;
using nts::test::has_entry_diagnostic;
using nts::test::has_location;
using nts::test::make_valid_fixture_on_axes;
using nts::test::ProblemTest;
using nts::test::ScopedWorkingDirectory;
using nts::test::string_array;
using nts::test::string_scalar;

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

constexpr auto reactor_document = static_cast<std::size_t>(InputDocument::reactor);

// One node-state field of /initial_state: its name, its units, the axis of the library it is
// checked against, and the axis' points (the fixture's axis is the same name).
struct AxisCase {
    std::string_view field;
    std::string_view axis;
    std::string_view units;
    double first;
    double last;
};

// In the order of the state axis vocabulary (§9): fuel_temperature, moderator_density,
// moderator_temperature, boron.
constexpr std::array<AxisCase, 4> axis_cases{{
    {.field = "doppler_temperature",
     .axis = "fuel_temperature",
     .units = "K",
     .first = 600.0,
     .last = 900.0},
    {.field = "moderator_density",
     .axis = "moderator_density",
     .units = "kg m^-3",
     .first = 600.0,
     .last = 800.0},
    {.field = "moderator_temperature",
     .axis = "moderator_temperature",
     .units = "K",
     .first = 500.0,
     .last = 600.0},
    {.field = "boron", .axis = "boron", .units = "ppm", .first = 100.0, .last = 1000.0},
}};

// Node positions probed by the coverage tests, with their "position (i, j, k)" entries. The valid
// grid is 3 x 2 x 2, so every node touches the box: (1, 0, 0) is mid-edge, (2, 1, 0) and
// (2, 1, 1) are corners. An interior node is probed on a 3 x 3 x 3 grid separately.
constexpr std::array<std::size_t, 3> probed_positions{1, 5, 11};
constexpr std::array<std::string_view, 3> probed_entries{"position (1, 0, 0)", "position (2, 1, 0)",
                                                         "position (2, 1, 1)"};

// The four state axes of the library, in vocabulary order, on the points of `axis_cases`.
std::vector<FixtureAxis> four_axes() {
    std::vector<FixtureAxis> axes;
    axes.reserve(axis_cases.size());
    for (const AxisCase& axis : axis_cases) {
        axes.push_back(FixtureAxis{.name = std::string(axis.axis),
                                   .units = std::string(axis.units),
                                   .points = {axis.first, axis.last}});
    }
    return axes;
}

double middle(const AxisCase& axis) {
    return (axis.first + axis.last) / 2.0;
}

// The node states of the valid grid [nz][ny][nx] = [2][2][3]: `inside` at each position,
// NaN at the excluded positions 0 and 6, and `overrides` (position -> value) elsewhere.
std::vector<double> node_array(double inside, const std::map<std::size_t, double>& overrides = {}) {
    std::vector<double> values(12, inside);
    values[0] = not_a_number;
    values[6] = not_a_number;
    for (const auto& [position, value] : overrides) {
        values.at(position) = value;
    }
    return values;
}

// The message of a node state outside the axis of composition set 'test_set' (§9).
std::string out_of_table(double value, const AxisCase& axis, std::string_view side, double point) {
    return std::format("{} {} is {} point {} {} of axis {} of composition set 'test_set'; table "
                       "clamps are not allowed",
                       value, axis.units, side, point, axis.units, axis.axis);
}

// A diagnostic of `file` at `location` whose message contains `message`.
template <typename Diagnostics>
bool has_in_file(const Diagnostics& diagnostics, const std::filesystem::path& file,
                 std::string_view location, std::string_view message) {
    return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.file == file.string() && diagnostic.location == location &&
               diagnostic.message.find(message) != std::string::npos;
    });
}

class DocumentTest : public ProblemTest {
protected:
    // Every node state of `axis` is `inside` except the probed positions, set to `values`.
    void set_field(const AxisCase& axis, std::vector<double> values) {
        initial_state().datasets[std::string(axis.field)] =
            float64_array(std::move(values), {2, 2, 3}, std::string(axis.units));
    }

    // All four node-state fields inside the library's axes.
    void fill_inside() {
        for (const AxisCase& axis : axis_cases) {
            set_field(axis, node_array(middle(axis)));
        }
    }
};

// ----- Equivalence and linking --------------------------------------------------------------

// The same problem with all four documents in place and with all four linked: every field equal,
// only the file holding each document differs.
TEST_F(DocumentTest, LinkedDocumentsLoadAsTheInPlaceDocumentsDo) {
    initial_state().datasets["boron"] =
        float64_array(node_array(500.0, {{1, 0.0}, {11, 1000.0}}), {2, 2, 3}, "ppm");
    set_option("kernel", nts::test::string_scalar("anm"));
    set_option("wielandt_shift", float64_scalar(0.1, "1"));
    set_option("outer_iteration_limit", nts::test::integer_scalar(77));
    set_option("adjoint", nts::test::integer_scalar(1));
    allow_table_clamps(1);
    control_signals();
    const Problem in_place = accepted();
    for (const std::string name : {"reactor", "initial_state", "control_signals", "settings"}) {
        link(name, name + ".h5");
    }
    const Problem linked = accepted();

    for (std::size_t d = 0; d < nts::input_document_count; ++d) {
        const std::string name(nts::input_document_name(static_cast<InputDocument>(d)));
        EXPECT_EQ(in_place.input.document_files.at(d), problem_file()) << name;
        EXPECT_EQ(linked.input.document_files.at(d), directory() / (name + ".h5")) << name;
    }
    EXPECT_EQ(linked.input.file, in_place.input.file);

    const nts::ReactorDescription& a = linked.input.reactor;
    const nts::ReactorDescription& b = in_place.input.reactor;
    EXPECT_EQ(a.width_x_cm, b.width_x_cm);
    EXPECT_EQ(a.width_y_cm, b.width_y_cm);
    EXPECT_EQ(a.width_z_cm, b.width_z_cm);
    EXPECT_EQ(a.composition_map, b.composition_map);
    EXPECT_EQ(a.node_positions, b.node_positions);
    EXPECT_EQ(a.position_nodes, b.position_nodes);
    for (std::size_t f = 0; f <= a.bounding_faces.size(); ++f) {
        const nts::BoundaryCondition& fa =
            f < a.bounding_faces.size() ? a.bounding_faces[f] : a.exterior;
        const nts::BoundaryCondition& fb =
            f < b.bounding_faces.size() ? b.bounding_faces[f] : b.exterior;
        EXPECT_EQ(fa.kind, fb.kind) << f;
        EXPECT_EQ(fa.albedo, fb.albedo) << f;
    }

    EXPECT_EQ(linked.input.binding.library_file, in_place.input.binding.library_file);
    ASSERT_EQ(linked.input.binding.compositions.size(), in_place.input.binding.compositions.size());
    for (std::size_t c = 0; c < linked.input.binding.compositions.size(); ++c) {
        EXPECT_EQ(linked.input.binding.compositions[c].library_set,
                  in_place.input.binding.compositions[c].library_set);
        EXPECT_EQ(linked.input.binding.compositions[c].library_composition,
                  in_place.input.binding.compositions[c].library_composition);
        EXPECT_EQ(linked.compositions.at(c).set, in_place.compositions.at(c).set);
        EXPECT_EQ(linked.compositions.at(c).composition, in_place.compositions.at(c).composition);
    }
    ASSERT_EQ(linked.library.composition_sets.size(), in_place.library.composition_sets.size());
    for (std::size_t s = 0; s < linked.library.composition_sets.size(); ++s) {
        EXPECT_EQ(linked.library.composition_sets[s].name,
                  in_place.library.composition_sets[s].name);
    }

    EXPECT_EQ(linked.input.operating_state.thermal_power_w,
              in_place.input.operating_state.thermal_power_w);
    const auto& linked_nodes = linked.input.operating_state.node_states;
    const auto& in_place_nodes = in_place.input.operating_state.node_states;
    ASSERT_EQ(linked_nodes.size(), in_place_nodes.size());
    for (std::size_t n = 0; n < linked_nodes.size(); ++n) {
        EXPECT_EQ(linked_nodes[n].doppler_temperature_k, in_place_nodes[n].doppler_temperature_k);
        EXPECT_EQ(linked_nodes[n].moderator_density_kg_m3,
                  in_place_nodes[n].moderator_density_kg_m3);
        EXPECT_EQ(linked_nodes[n].moderator_temperature_k,
                  in_place_nodes[n].moderator_temperature_k);
        EXPECT_EQ(linked_nodes[n].boron_ppm, in_place_nodes[n].boron_ppm);
    }

    const nts::SolverOptions& oa = linked.input.solver_options;
    const nts::SolverOptions& ob = in_place.input.solver_options;
    EXPECT_EQ(oa.kernel, nts::KernelChoice::anm);
    EXPECT_EQ(oa.kernel, ob.kernel);
    EXPECT_EQ(oa.kernel_switch_threshold, ob.kernel_switch_threshold);
    EXPECT_EQ(oa.wielandt_shift, 0.1);
    EXPECT_EQ(oa.wielandt_shift, ob.wielandt_shift);
    EXPECT_EQ(oa.eigenvalue_tolerance, ob.eigenvalue_tolerance);
    EXPECT_EQ(oa.fission_source_l2_tolerance, ob.fission_source_l2_tolerance);
    EXPECT_EQ(oa.fission_source_linf_tolerance, ob.fission_source_linf_tolerance);
    EXPECT_EQ(oa.inner_reduction, ob.inner_reduction);
    EXPECT_EQ(oa.inner_iteration_limit, ob.inner_iteration_limit);
    EXPECT_EQ(oa.outer_iteration_limit, 77);
    EXPECT_EQ(oa.outer_iteration_limit, ob.outer_iteration_limit);
    EXPECT_EQ(oa.nodal_update_period, ob.nodal_update_period);
    EXPECT_EQ(oa.coupling_relaxation, ob.coupling_relaxation);
    EXPECT_TRUE(oa.adjoint);
    EXPECT_EQ(oa.adjoint, ob.adjoint);
    EXPECT_TRUE(linked.input.allow_table_clamps);
    EXPECT_EQ(linked.input.allow_table_clamps, in_place.input.allow_table_clamps);
}

TEST_F(DocumentTest, LinkedDocumentsResolveAgainstTheProblemInputNotTheWorkingDirectory) {
    reactor().groups["library"].attributes["path"] = std::string{"../library.h5"};
    link("reactor", "docs/reactor.h5");
    std::filesystem::create_directories(directory() / "elsewhere");
    const ScopedWorkingDirectory elsewhere(directory() / "elsewhere");

    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.document_files[reactor_document], directory() / "docs/reactor.h5");
    EXPECT_EQ(loaded.input.binding.library_file.lexically_normal(),
              (directory() / "library.h5").lexically_normal());
}

// A library path is resolved against the directory of the file holding the reactor document.
TEST_F(DocumentTest, LibraryPathOfALinkedReactorResolvesAgainstItsDirectory) {
    reactor().groups["library"].attributes["path"] = std::string{"../library.h5"};
    link("reactor", "docs/reactor.h5");
    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.binding.library_file.lexically_normal(),
              (directory() / "library.h5").lexically_normal());
}

TEST_F(DocumentTest, LibraryPathOfALinkedReactorIsNotRelativeToTheProblemInput) {
    reactor().groups["library"].attributes["path"] = std::string{"library.h5"};
    link("reactor", "docs/reactor.h5");
    const auto diagnostics = rejected();
    EXPECT_TRUE(has_in_file(diagnostics, directory() / "docs" / "library.h5", "/",
                            "the file does not exist or is not a regular file"))
        << all_text(diagnostics);
}

// ----- Document presence, kind, version and linking (§2) ------------------------------------

TEST_F(DocumentTest, MissingReactorIsReported) {
    problem().groups.erase("reactor");
    EXPECT_TRUE(has_diagnostic(rejected(), "/reactor", "document is missing"));
}

TEST_F(DocumentTest, MissingInitialStateIsReported) {
    problem().groups.erase("initial_state");
    EXPECT_TRUE(has_diagnostic(rejected(), "/initial_state", "document is missing"));
}

TEST_F(DocumentTest, DocumentKindMustNameItsMember) {
    reactor().attributes["document"] = std::string{"settings"};
    EXPECT_TRUE(has_diagnostic(rejected(), "/reactor@document",
                               "document kind 'settings' is not 'reactor'"));
}

TEST_F(DocumentTest, DocumentKindOfALinkedDocumentIsCheckedInItsFile) {
    initial_state().attributes["document"] = std::string{"reactor"};
    link("initial_state", "initial_state.h5");
    EXPECT_TRUE(has_in_file(rejected(), directory() / "initial_state.h5", "/@document",
                            "document kind 'reactor' is not 'initial_state'"));
}

TEST_F(DocumentTest, DocumentKindMustBeAString) {
    reactor().attributes["document"] = std::int64_t{3};
    EXPECT_TRUE(has_location(rejected(), "/reactor@document"));
}

TEST_F(DocumentTest, DocumentFormatVersionMustBeTwo) {
    reactor().attributes["format_version"] = std::int64_t{1};
    EXPECT_TRUE(has_diagnostic(rejected(), "/reactor@format_version",
                               "format version 1 is not supported; this engine reads version 2"));
}

TEST_F(DocumentTest, DocumentFormatVersionOfALinkedDocumentIsCheckedInItsFile) {
    initial_state().attributes["format_version"] = std::int64_t{1};
    link("initial_state", "initial_state.h5");
    EXPECT_TRUE(has_in_file(rejected(), directory() / "initial_state.h5", "/@format_version",
                            "format version 1 is not supported; this engine reads version 2"));
}

TEST_F(DocumentTest, DocumentFormatVersionMustBeAnInteger) {
    reactor().attributes["format_version"] = std::string{"2"};
    EXPECT_TRUE(has_location(rejected(), "/reactor@format_version"));
}

TEST_F(DocumentTest, LinkedFileThatIsMissingIsReported) {
    problem().external_links["settings"] = FixtureExternalLink{.file = "absent.h5"};
    const std::string path = (directory() / "absent.h5").string();
    EXPECT_TRUE(has_diagnostic(rejected(), "/settings",
                               std::format("the linked document file '{}' does not exist or is "
                                           "not a regular file",
                                           path)));
}

TEST_F(DocumentTest, LinkedFileThatIsNotHdf5IsReported) {
    std::filesystem::create_directories(directory());
    std::ofstream(directory() / "plain.h5") << "not an HDF5 file\n";
    problem().external_links["settings"] = FixtureExternalLink{.file = "plain.h5"};
    const std::string path = (directory() / "plain.h5").string();
    EXPECT_TRUE(
        has_diagnostic(rejected(), "/settings",
                       std::format("the linked document file '{}' is not an HDF5 file", path)));
}

TEST_F(DocumentTest, LinkMustNameTheRootGroupOfAFile) {
    settings();
    link("settings", "settings.h5");
    problem().external_links["settings"].object = "/settings";
    EXPECT_TRUE(has_diagnostic(rejected(), "/settings",
                               "the external link must name the root group '/' of a document "
                               "file"));
}

TEST_F(DocumentTest, RootDatasetIsNotADocument) {
    problem().groups.erase("initial_state");
    problem().datasets["initial_state"] = float64_scalar(1.0, "K");
    EXPECT_TRUE(has_diagnostic(rejected(), "/initial_state", "not a group or an external link"));
}

// Each of the four documents: wrong kind, unsupported version, a link to a missing file, a link
// to a file that is not HDF5. Optional documents are created first so that each is present.
class EveryDocument : public DocumentTest, public testing::WithParamInterface<InputDocument> {
protected:
    static std::string name() { return std::string(nts::input_document_name(GetParam())); }
};

TEST_P(EveryDocument, KindMustBeItsMemberName) {
    document(name()).attributes["document"] = std::string{"other"};
    EXPECT_TRUE(has_diagnostic(rejected(), "/" + name() + "@document",
                               std::format("document kind 'other' is not '{}'", name())));
}

TEST_P(EveryDocument, KindIsRequired) {
    document(name()).attributes.erase("document");
    EXPECT_TRUE(has_location(rejected(), "/" + name() + "@document"));
}

TEST_P(EveryDocument, FormatVersionThreeIsUnsupportedInPlaceAndLinked) {
    document(name()).attributes["format_version"] = std::int64_t{3};
    EXPECT_TRUE(has_diagnostic(rejected(), "/" + name() + "@format_version",
                               "format version 3 is not supported; this engine reads version 2"));
    link(name(), name() + ".h5");
    EXPECT_TRUE(has_in_file(rejected(), directory() / (name() + ".h5"), "/@format_version",
                            "format version 3 is not supported; this engine reads version 2"));
}

TEST_P(EveryDocument, LinkToAMissingFileIsReported) {
    problem().groups.erase(name());
    problem().external_links[name()] = FixtureExternalLink{.file = "absent.h5"};
    EXPECT_TRUE(has_diagnostic(rejected(), "/" + name(),
                               std::format("the linked document file '{}' does not exist or is "
                                           "not a regular file",
                                           (directory() / "absent.h5").string())));
}

TEST_P(EveryDocument, LinkToAFileThatIsNotHdf5IsReported) {
    std::filesystem::create_directories(directory());
    std::ofstream(directory() / "plain.h5") << "not an HDF5 file\n";
    problem().groups.erase(name());
    problem().external_links[name()] = FixtureExternalLink{.file = "plain.h5"};
    EXPECT_TRUE(has_diagnostic(rejected(), "/" + name(),
                               std::format("the linked document file '{}' is not an HDF5 file",
                                           (directory() / "plain.h5").string())));
}

TEST_P(EveryDocument, LinkedDocumentIsReadFromItsFile) {
    document(name());
    link(name(), name() + "_document.h5");
    const Problem loaded = accepted();
    EXPECT_EQ(loaded.input.document_files.at(static_cast<std::size_t>(GetParam())),
              directory() / (name() + "_document.h5"));
}

INSTANTIATE_TEST_SUITE_P(Documents, EveryDocument,
                         testing::Values(InputDocument::reactor, InputDocument::initial_state,
                                         InputDocument::control_signals, InputDocument::settings),
                         [](const testing::TestParamInfo<InputDocument>& param_info) {
                             return std::string(nts::input_document_name(param_info.param));
                         });

// Only the four documents of the problem input may be linked: a link anywhere else is refused,
// at the root, inside the reactor document and inside a linked document file.
TEST_F(DocumentTest, ExternalLinksElsewhereAreRejected) {
    problem().external_links["outside"] = FixtureExternalLink{.file = "library.h5"};
    reactor().external_links["extra"] = FixtureExternalLink{.file = "library.h5"};
    reactor().groups["geometry"].external_links["copy"] = FixtureExternalLink{.file = "library.h5"};
    link("reactor", "reactor.h5");
    const auto diagnostics = rejected();
    constexpr std::string_view message =
        "an external link; only the four documents of the problem input may be linked";
    EXPECT_TRUE(has_diagnostic(diagnostics, "/outside", message)) << all_text(diagnostics);
    EXPECT_TRUE(has_in_file(diagnostics, directory() / "reactor.h5", "/extra", message))
        << all_text(diagnostics);
    EXPECT_TRUE(has_in_file(diagnostics, directory() / "reactor.h5", "/geometry/copy", message))
        << all_text(diagnostics);
}

// ----- Initial state source and control signals (§6, §8) ------------------------------------

TEST_F(DocumentTest, SourceMustBeStated) {
    initial_state().attributes["source"] = std::string{"steady"};
    EXPECT_TRUE(has_diagnostic(rejected(), "/initial_state@source",
                               "unknown source 'steady'; expected stated"));
}

TEST_F(DocumentTest, SourceOfALinkedInitialStateIsCheckedInItsFile) {
    initial_state().attributes["source"] = std::string{"steady"};
    link("initial_state", "initial_state.h5");
    EXPECT_TRUE(has_in_file(rejected(), directory() / "initial_state.h5", "/@source",
                            "unknown source 'steady'; expected stated"));
}

TEST_F(DocumentTest, SourceMustExist) {
    initial_state().attributes.erase("source");
    EXPECT_TRUE(has_location(rejected(), "/initial_state@source"));
}

TEST_F(DocumentTest, NonEmptyControlSignalsAreRejected) {
    control_signals().datasets["signal"] = float64_scalar(1.0, "1");
    EXPECT_TRUE(has_diagnostic(rejected(), "/control_signals/signal",
                               "not a member of the problem input contract"));
}

TEST_F(DocumentTest, EmptyControlSignalsAreAccepted) {
    control_signals();
    EXPECT_EQ(accepted().input.file, problem_file());
}

// ----- Settings: allow_table_clamps (§7) ----------------------------------------------------

class AllowTableClampsValue : public DocumentTest,
                              public testing::WithParamInterface<std::int64_t> {};

TEST_P(AllowTableClampsValue, RejectsAValueOtherThanZeroOrOne) {
    allow_table_clamps(GetParam());
    EXPECT_TRUE(has_diagnostic(rejected(), "/settings/allow_table_clamps",
                               std::format("value {} is not 0 or 1", GetParam())));
}

INSTANTIATE_TEST_SUITE_P(Settings, AllowTableClampsValue, testing::Values<std::int64_t>(-1, 3));

TEST_F(DocumentTest, AllowTableClampsZeroAndOneAreTheOnlyFlags) {
    allow_table_clamps(0);
    EXPECT_FALSE(accepted().input.allow_table_clamps);
    allow_table_clamps(1);
    EXPECT_TRUE(accepted().input.allow_table_clamps);
}

// ----- Library coverage of the node states (§6, check 11) -----------------------------------

class CoverageTest : public DocumentTest, public testing::WithParamInterface<AxisCase> {
protected:
    // The diagnostics of a node state below or above the axis at the probed positions.
    void expect_probed(const auto& diagnostics, const AxisCase& axis, double value,
                       std::string_view side, double point) {
        EXPECT_EQ(diagnostics.size(), probed_positions.size()) << all_text(diagnostics);
        const std::string location = std::format("/initial_state/{}", axis.field);
        const std::string message = out_of_table(value, axis, side, point);
        for (const std::string_view entry : probed_entries) {
            EXPECT_TRUE(has_entry_diagnostic(diagnostics, location, entry, message))
                << all_text(diagnostics);
        }
    }
};

TEST_P(CoverageTest, RejectsNodeStatesBelowAndAboveTheAxisUnlessClampsAreAllowed) {
    const AxisCase& axis = GetParam();
    library() = make_valid_fixture_on_axes(four_axes());
    const double below = axis.first - 50.0;
    const double above = axis.last + 50.0;
    fill_inside();

    set_field(axis, node_array(middle(axis), {{1, below}, {5, below}, {11, below}}));
    expect_probed(rejected(), axis, below, "below the first", axis.first);

    set_field(axis, node_array(middle(axis), {{1, above}, {5, above}, {11, above}}));
    expect_probed(rejected(), axis, above, "above the last", axis.last);

    allow_table_clamps(1);
    EXPECT_EQ(accepted().input.operating_state.node_states.size(), 10U);
}

// The end points themselves lie within the table (§9: first_point <= v <= last_point).
TEST_P(CoverageTest, AcceptsNodeStatesAtTheEndPoints) {
    const AxisCase& axis = GetParam();
    library() = make_valid_fixture_on_axes(four_axes());
    fill_inside();
    set_field(axis, node_array(middle(axis), {{1, axis.first}, {5, axis.last}, {11, axis.first}}));
    EXPECT_EQ(accepted().input.operating_state.node_states.size(), 10U);
}

// The interior node (1, 1, 1) of a 3 x 3 x 3 grid, below and above the axis.
TEST_P(CoverageTest, RejectsAnInteriorNodeStateOutsideTheAxis) {
    const AxisCase& axis = GetParam();
    library() = make_valid_fixture_on_axes(four_axes());
    constexpr std::size_t side = 3;
    for (const std::string name : {"width_x", "width_y", "width_z"}) {
        geometry().datasets[name] = float64_array(std::vector<double>(side, 10.0), {side}, "cm");
    }
    FixtureDataset composition_map;
    composition_map.type = FixtureType::int32;
    composition_map.shape = {side, side, side};
    composition_map.numbers = std::vector<double>(side * side * side, 0.0);
    geometry().datasets["composition_map"] = composition_map;
    for (const AxisCase& other : axis_cases) {
        initial_state().datasets[std::string(other.field)] =
            float64_scalar(middle(other), std::string(other.units));
    }
    constexpr std::size_t interior = 13;
    for (const auto& [value, side_text, point] :
         {std::tuple{axis.first - 50.0, "below the first", axis.first},
          std::tuple{axis.last + 50.0, "above the last", axis.last}}) {
        std::vector<double> values(side * side * side, middle(axis));
        values[interior] = value;
        initial_state().datasets[std::string(axis.field)] =
            float64_array(std::move(values), {side, side, side}, std::string(axis.units));
        const auto diagnostics = rejected();
        EXPECT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
        EXPECT_TRUE(has_entry_diagnostic(diagnostics, std::format("/initial_state/{}", axis.field),
                                         "position (1, 1, 1)",
                                         out_of_table(value, axis, side_text, point)))
            << all_text(diagnostics);
    }
}

INSTANTIATE_TEST_SUITE_P(Axes, CoverageTest, testing::ValuesIn(axis_cases),
                         [](const testing::TestParamInfo<AxisCase>& param_info) {
                             return std::string(param_info.param.axis);
                         });

// A one-point axis holds a single value: the point itself is accepted, anything else is not.
TEST_F(DocumentTest, OnePointAxisAcceptsOnlyItsPoint) {
    auto axes = four_axes();
    axes[1].points = {700.0};
    library() = make_valid_fixture_on_axes(axes);
    fill_inside();
    set_field(axis_cases[1], node_array(700.0));
    EXPECT_EQ(accepted().input.operating_state.node_states.size(), 10U);

    set_field(axis_cases[1], node_array(650.0, {{1, 650.0}}));
    EXPECT_TRUE(has_entry_diagnostic(rejected(), "/initial_state/moderator_density",
                                     "position (1, 0, 0)",
                                     "650 kg m^-3 is below the first point 700 kg m^-3 of axis "
                                     "moderator_density of composition set 'test_set'; table "
                                     "clamps are not allowed"));
}

// An axis the library lacks is not checked: the default library has no moderator axes.
TEST_F(DocumentTest, AbsentAxisIsNotChecked) {
    initial_state().datasets["moderator_density"] = float64_scalar(1.0e9, "kg m^-3");
    initial_state().datasets["moderator_temperature"] = float64_scalar(1.0, "K");
    EXPECT_EQ(accepted().input.operating_state.node_states.size(), 10U);
}

// Twenty-five out-of-table nodes of one field: twenty are listed, then the count of the rest.
TEST_F(DocumentTest, ListsAtMostTwentyOutOfTableNodeStatesOfAFieldThenCountsTheRest) {
    constexpr std::size_t side = 5;
    geometry().datasets["width_x"] = float64_array(std::vector<double>(side, 10.0), {side}, "cm");
    geometry().datasets["width_y"] = float64_array(std::vector<double>(side, 10.0), {side}, "cm");
    geometry().datasets["width_z"] = float64_array({10.0}, {1U}, "cm");
    FixtureDataset composition_map;
    composition_map.type = FixtureType::int32;
    composition_map.shape = {1U, side, side};
    composition_map.numbers = std::vector<double>(side * side, 0.0);
    geometry().datasets["composition_map"] = composition_map;
    initial_state().datasets["doppler_temperature"] =
        float64_array(std::vector<double>(side * side, 1200.0), {1U, side, side}, "K");

    const auto diagnostics = rejected();
    const auto listed = std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.location == "/initial_state/doppler_temperature" &&
               !diagnostic.entry.empty();
    });
    EXPECT_EQ(listed, static_cast<std::ptrdiff_t>(20)) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/initial_state/doppler_temperature",
                               "5 more diagnostics are not listed"))
        << all_text(diagnostics);
}

// The number of diagnostics at `location`.
template <typename Diagnostics>
std::ptrdiff_t count_at(const Diagnostics& diagnostics, std::string_view location) {
    return std::ranges::count_if(
        diagnostics, [&](const auto& diagnostic) { return diagnostic.location == location; });
}

// ----- Unreadable and dangling documents (review B1, B2) ------------------------------------

// A linked settings document that cannot be opened is reported at its member, and the width
// check of the reactor still runs.
TEST_F(DocumentTest, UnreadableLinkedDocumentIsReportedAndOtherChecksStillRun) {
    geometry().datasets["width_x"] = float64_array({10.0, -1.0, 10.0}, {3}, "cm");
    settings();
    link("settings", "settings.h5");
    write_files();
    std::filesystem::permissions(directory() / "settings.h5", std::filesystem::perms::none);
    if (std::ifstream(directory() / "settings.h5").good()) {
        GTEST_SKIP() << "the file stays readable without permissions (running as root)";
    }
    const auto result = nts::load_problem(problem_file());
    std::filesystem::permissions(directory() / "settings.h5", std::filesystem::perms::owner_all);
    ASSERT_FALSE(result.has_value());
    const auto& diagnostics = result.error();
    EXPECT_TRUE(has_diagnostic(diagnostics, "/settings",
                               std::format("the linked document file '{}' cannot be read",
                                           (directory() / "settings.h5").string())))
        << all_text(diagnostics);
    EXPECT_TRUE(has_location(diagnostics, "/reactor/geometry/width_x")) << all_text(diagnostics);
}

// A dangling soft link at a document member is reported at that member; other checks still run.
TEST_F(DocumentTest, DanglingSoftLinkAtADocumentMemberIsReportedAndOtherChecksStillRun) {
    geometry().datasets["width_x"] = float64_array({10.0, -1.0, 10.0}, {3}, "cm");
    const auto diagnostics = rejected_edited([](const H5::Group& root) {
        if (H5Lcreate_soft("/nowhere", root.getId(), "settings", H5P_DEFAULT, H5P_DEFAULT) < 0) {
            throw std::runtime_error("cannot create the soft link");
        }
    });
    EXPECT_TRUE(has_location(diagnostics, "/settings")) << all_text(diagnostics);
    EXPECT_TRUE(has_location(diagnostics, "/reactor/geometry/width_x")) << all_text(diagnostics);
}

// A link to a directory is not a regular file.
TEST_F(DocumentTest, LinkedDirectoryIsNotADocumentFile) {
    std::filesystem::create_directories(directory() / "settings.d");
    problem().external_links["settings"] = FixtureExternalLink{.file = "settings.d"};
    const auto diagnostics = rejected();
    EXPECT_TRUE(has_diagnostic(
        diagnostics, "/settings",
        std::format("the linked document file '{}' does not exist or is not a regular file",
                    (directory() / "settings.d").string())))
        << all_text(diagnostics);
}

// ----- Gap tests (review G1-G5) -------------------------------------------------------------

// An invalid flag counts as 0: the out-of-table node is reported beside the flag diagnostic.
TEST_F(DocumentTest, InvalidClampFlagCountsAsZero) {
    allow_table_clamps(3);
    initial_state().datasets["doppler_temperature"] = float64_scalar(2000.0, "K");
    const auto diagnostics = rejected();
    EXPECT_TRUE(
        has_diagnostic(diagnostics, "/settings/allow_table_clamps", "value 3 is not 0 or 1"))
        << all_text(diagnostics);
    EXPECT_TRUE(has_location(diagnostics, "/initial_state/doppler_temperature"))
        << all_text(diagnostics);
}

// Coverage diagnostics of a linked initial state are in its file; settings diagnostics of a
// linked settings document are in its file; neither is in the problem input.
TEST_F(DocumentTest, DiagnosticsOfLinkedStateAndSettingsAreInTheirFiles) {
    initial_state().datasets["doppler_temperature"] = float64_scalar(2000.0, "K");
    link("initial_state", "initial_state.h5");
    set_option("kernel", string_scalar("bogus"));
    link("settings", "settings.h5");
    const auto diagnostics = rejected();
    EXPECT_TRUE(has_in_file(diagnostics, directory() / "initial_state.h5", "/doppler_temperature",
                            "is above the last point"))
        << all_text(diagnostics);
    EXPECT_TRUE(
        has_in_file(diagnostics, directory() / "settings.h5", "/kernel", "unknown kernel 'bogus'"))
        << all_text(diagnostics);
    EXPECT_TRUE(std::ranges::none_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.file == problem_file().string();
    })) << all_text(diagnostics);
}

// A named dataset that is an external link is reported at its location and not followed.
TEST_F(DocumentTest, ExternalLinkAtANamedDatasetIsNotFollowed) {
    geometry().datasets.erase("width_x");
    geometry().external_links["width_x"] = FixtureExternalLink{.file = "library.h5"};
    const auto diagnostics = rejected();
    EXPECT_EQ(count_at(diagnostics, "/reactor/geometry/width_x"), 1) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(
        diagnostics, "/reactor/geometry/width_x",
        "an external link; only the four documents of the problem input may be linked"))
        << all_text(diagnostics);
}

// A named group that is an external link is reported at its location and not followed.
TEST_F(DocumentTest, ExternalLinkAtANamedGroupIsNotFollowed) {
    reactor().groups.erase("geometry");
    reactor().external_links["geometry"] = FixtureExternalLink{.file = "library.h5"};
    const auto diagnostics = rejected();
    EXPECT_EQ(count_at(diagnostics, "/reactor/geometry"), 1) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(
        diagnostics, "/reactor/geometry",
        "an external link; only the four documents of the problem input may be linked"))
        << all_text(diagnostics);
    EXPECT_TRUE(std::ranges::none_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.location.starts_with("/reactor/geometry/");
    })) << all_text(diagnostics);
}

// Settings diagnostics (item 9) come before library coverage diagnostics (item 11).
TEST_F(DocumentTest, SettingsDiagnosticsPrecedeCoverageDiagnostics) {
    set_option("kernel", string_scalar("bogus"));
    initial_state().datasets["doppler_temperature"] = float64_scalar(2000.0, "K");
    const auto diagnostics = rejected();
    const auto settings_at = std::ranges::find_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.location == "/settings/kernel";
    });
    const auto coverage_at = std::ranges::find_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.location == "/initial_state/doppler_temperature";
    });
    ASSERT_NE(settings_at, diagnostics.end()) << all_text(diagnostics);
    ASSERT_NE(coverage_at, diagnostics.end()) << all_text(diagnostics);
    EXPECT_LT(std::distance(diagnostics.begin(), settings_at),
              std::distance(diagnostics.begin(), coverage_at))
        << all_text(diagnostics);
}

// A composition that does not resolve has no node checked; the nodes of the others still are,
// whether they come after the unresolved one in node order or before it.
TEST_F(DocumentTest, UnresolvedCompositionIsNotCheckedAndTheOthersAre) {
    reactor().groups["compositions"].datasets["library_composition"] =
        string_array({"fuel", "missing"});
    initial_state().datasets["doppler_temperature"] = float64_scalar(2000.0, "K");
    const auto diagnostics = rejected();
    // Nine of the ten nodes have composition 0; the node of composition 1 is not checked.
    EXPECT_EQ(count_at(diagnostics, "/initial_state/doppler_temperature"), 9)
        << all_text(diagnostics);
    EXPECT_TRUE(std::ranges::none_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.entry == "position (2, 1, 1)";
    })) << all_text(diagnostics);
}

// The unresolved composition first in node order: the scan continues past its nodes to the one
// node of the resolved composition, (2, 1, 1).
TEST_F(DocumentTest, NodesAfterAnUnresolvedCompositionAreStillChecked) {
    reactor().groups["compositions"].datasets["library_composition"] =
        string_array({"missing", "reflector"});
    initial_state().datasets["doppler_temperature"] = float64_scalar(2000.0, "K");
    const auto diagnostics = rejected();
    EXPECT_EQ(count_at(diagnostics, "/initial_state/doppler_temperature"), 1)
        << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/initial_state/doppler_temperature",
                                     "position (2, 1, 1)", "above the last point"))
        << all_text(diagnostics);
}

} // namespace
