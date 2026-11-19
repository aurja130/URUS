// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// Checks 4 and 5 of the problem input (docs/contracts/problem_input.md §3 and §4): the geometry
// and the boundary conditions.

#include "nts/common/diagnostic.hpp"
#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::BoundaryCondition;
using nts::BoundaryKind;
using nts::Diagnostic;
using nts::Diagnostics;
using nts::Problem;
using nts::test::all_text;
using nts::test::expect_one;
using nts::test::FixtureDataset;
using nts::test::FixtureGroup;
using nts::test::FixtureType;
using nts::test::float64_array;
using nts::test::float64_scalar;
using nts::test::has_diagnostic;
using nts::test::has_entry_diagnostic;
using nts::test::ProblemTest;

// The bounding faces in the order of nts::Face (contract §4), then the exterior face.
constexpr std::size_t face_count = 6;
constexpr std::array<std::string_view, face_count + 1> faces{
    "x_minus", "x_plus", "y_minus", "y_plus", "z_minus", "z_plus", "exterior"};
// The face of the valid problem whose kind is albedo, with albedo [0.3, 0.6].
constexpr std::string_view albedo_face = "y_plus";
constexpr double fast_albedo = 0.3;
constexpr double thermal_albedo = 0.6;

// α_b of each fixed kind (contract §4).
constexpr double reflective_alpha = 0.0;
constexpr double vacuum_alpha = 0.5;
constexpr double zero_flux_alpha = 1.0e30;

// Position (1, 0, 1) of the valid problem's map: (k * ny + j) * nx + i = (1 * 2 + 0) * 3 + 1.
constexpr std::size_t position_one_zero_one = 7;

// The composition map of the valid problem: [2][2][3] row-major, -1 at positions 0 and 6.
std::vector<std::int64_t> valid_map() {
    return {-1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
}

std::string width_location(const std::string& width) {
    return "/geometry/" + width;
}

std::string face_location(std::size_t face) {
    return "/boundary_conditions/" + std::string(faces[face]);
}

// The condition of bounding face `index`, or of the exterior face for index face_count.
const BoundaryCondition& condition_of(const Problem& result, std::size_t index) {
    return index < face_count ? result.input.reactor.bounding_faces[index]
                              : result.input.reactor.exterior;
}

class ProblemGeometryTest : public ProblemTest {
protected:
    FixtureDataset& geometry(std::string_view name) {
        return problem().groups["geometry"].datasets[std::string(name)];
    }
    FixtureDataset& composition_map() { return geometry("composition_map"); }
    FixtureGroup& face(std::string_view name) {
        return problem().groups["boundary_conditions"].groups[std::string(name)];
    }
    void erase_face(std::string_view name) {
        problem().groups["boundary_conditions"].groups.erase(std::string(name));
    }
    FixtureDataset& albedo() { return face(albedo_face).datasets["albedo"]; }

    // Makes face `name` an albedo face with a valid albedo; returns the albedo dataset.
    FixtureDataset& albedo_dataset_of(std::string_view name) {
        FixtureGroup& this_face = face(name);
        this_face.attributes["kind"] = std::string{"albedo"};
        FixtureDataset& dataset = this_face.datasets["albedo"];
        dataset = float64_array({fast_albedo, thermal_albedo}, {2U}, "1");
        return dataset;
    }
};

// ----- Geometry: the three widths (§3) -------------------------------------------------------

class WidthTest : public ProblemGeometryTest, public testing::WithParamInterface<std::string> {};

TEST_P(WidthTest, RejectsAMissingWidth) {
    problem().groups["geometry"].datasets.erase(GetParam());
    expect_one(rejected(), width_location(GetParam()), "dataset is missing");
}

TEST_P(WidthTest, RejectsAWidthThatIsNotFloat64) {
    geometry(GetParam()).type = FixtureType::float32;
    expect_one(rejected(), width_location(GetParam()), "not a float64 dataset");
}

TEST_P(WidthTest, RejectsAWidthOfRankTwo) {
    FixtureDataset& width = geometry(GetParam());
    width.shape = {width.numbers.size(), 1U};
    expect_one(rejected(), width_location(GetParam()), "is not [n] with n >= 1");
}

TEST_P(WidthTest, RejectsAnEmptyWidth) {
    FixtureDataset& width = geometry(GetParam());
    width.numbers.clear();
    width.shape = {0U};
    expect_one(rejected(), width_location(GetParam()), "is not [n] with n >= 1");
}

TEST_P(WidthTest, RejectsAWidthWithoutUnits) {
    geometry(GetParam()).attributes.erase("units");
    expect_one(rejected(), width_location(GetParam()) + "@units", "attribute is missing");
}

TEST_P(WidthTest, RejectsAWidthInUnitsOtherThanCm) {
    geometry(GetParam()).attributes["units"] = std::string{"m"};
    expect_one(rejected(), width_location(GetParam()) + "@units",
               "units 'm' are not the accepted 'cm'");
}

// The entry is the width's index; index 1 is the second element of every width.
TEST_P(WidthTest, RejectsAWidthThatIsNotFiniteAndPositive) {
    constexpr std::size_t index = 1;
    const std::string entry = "index " + std::to_string(index);
    for (const double value : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
        SCOPED_TRACE(value);
        geometry(GetParam()).numbers[index] = value;
        const Diagnostics diagnostics = rejected();
        expect_one(diagnostics, width_location(GetParam()), "is not finite and positive");
        EXPECT_TRUE(
            has_entry_diagnostic(diagnostics, width_location(GetParam()), entry, "is not finite"))
            << all_text(diagnostics);
    }
}

// An unusable width skips the map's shape check, so only the width is reported.
TEST_P(WidthTest, ReportsOnlyTheWidthWhenTheMapShapeCannotBeChecked) {
    geometry(GetParam()).numbers[0] = -1.0;
    composition_map().shape = {2U, 3U, 2U};
    expect_one(rejected(), width_location(GetParam()), "is not finite and positive");
}

TEST_F(ProblemGeometryTest, RejectsAWidthStoredAsAGroup) {
    problem().groups["geometry"].datasets.erase("width_x");
    problem().groups["geometry"].groups["width_x"] = FixtureGroup{};
    expect_one(rejected(), width_location("width_x"), "not a dataset");
}

INSTANTIATE_TEST_SUITE_P(Widths, WidthTest, testing::Values("width_x", "width_y", "width_z"));

// Widths are not required to be uniform (§3); the values read back unchanged.
TEST_F(ProblemGeometryTest, AcceptsNonUniformWidths) {
    geometry("width_x").numbers = {7.5, 12.25, 30.0};
    geometry("width_y").numbers = {4.0, 9.5};
    geometry("width_z").numbers = {2.5, 6.0};
    const Problem result = accepted();
    EXPECT_EQ(result.input.reactor.width_x_cm, (std::vector<double>{7.5, 12.25, 30.0}));
    EXPECT_EQ(result.input.reactor.width_y_cm, (std::vector<double>{4.0, 9.5}));
    EXPECT_EQ(result.input.reactor.width_z_cm, (std::vector<double>{2.5, 6.0}));
}

// ----- Geometry: the composition map (§3) -----------------------------------------------------

TEST_F(ProblemGeometryTest, RejectsAMissingCompositionMap) {
    problem().groups["geometry"].datasets.erase("composition_map");
    expect_one(rejected(), "/geometry/composition_map", "dataset is missing");
}

TEST_F(ProblemGeometryTest, RejectsACompositionMapThatIsNotAnInteger) {
    composition_map().type = FixtureType::float64;
    expect_one(rejected(), "/geometry/composition_map", "not an integer dataset");
}

// §1: integers are read as signed 64-bit and their range checked. uint64 holds no -1, so the
// excluded positions of this map are 0.
TEST_F(ProblemGeometryTest, RejectsAMapValueBeyondTheSignedRange) {
    constexpr double beyond_signed_range = 9223372036854775808.0; // 2^63
    composition_map().type = FixtureType::uint64;
    composition_map().numbers = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                 0.0, 0.0, 0.0, 0.0, 0.0, beyond_signed_range};
    expect_one(rejected(), "/geometry/composition_map", "a value exceeds the signed 64-bit range");
}

TEST_F(ProblemGeometryTest, AcceptsAnInt64CompositionMap) {
    composition_map().type = FixtureType::int64;
    const Problem result = accepted();
    EXPECT_EQ(result.input.reactor.composition_map, valid_map());
    EXPECT_EQ(result.input.reactor.node_count(), 10U);
}

// uint8 cannot hold -1, so the map has no excluded position.
TEST_F(ProblemGeometryTest, AcceptsAUint8CompositionMapWithoutExcludedPositions) {
    composition_map().type = FixtureType::uint8;
    composition_map().numbers.assign(12, 0.0);
    composition_map().numbers.back() = 1.0;
    const Problem result = accepted();
    EXPECT_EQ(result.input.reactor.composition_map,
              (std::vector<std::int64_t>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
    EXPECT_EQ(result.input.reactor.node_count(), 12U);
}

TEST_F(ProblemGeometryTest, RejectsACompositionMapOfTheTransposedShape) {
    composition_map().shape = {2U, 3U, 2U};
    expect_one(rejected(), "/geometry/composition_map", "does not match [nz][ny][nx]");
}

// Pins the x-fastest [z][y][x] ordering: position (1, 0, 1) is the 8th of the array.
TEST_F(ProblemGeometryTest, RejectsAMapValueBelowMinusOneAtItsPosition) {
    composition_map().numbers[position_one_zero_one] = -2.0;
    const Diagnostics diagnostics = rejected();
    expect_one(diagnostics, "/geometry/composition_map", "value -2 is below -1");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/composition_map", "position (1, 0, 1)",
                                     "value -2 is below -1"))
        << all_text(diagnostics);
}

// K = 2 input compositions: the index 2 names none.
TEST_F(ProblemGeometryTest, RejectsAMapValueAboveTheLastCompositionAtItsPosition) {
    composition_map().numbers[position_one_zero_one] = 2.0;
    const Diagnostics diagnostics = rejected();
    expect_one(diagnostics, "/geometry/composition_map",
               "value 2 is not -1 or an input composition index below 2");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/composition_map", "position (1, 0, 1)",
                                     "value 2 is not -1"))
        << all_text(diagnostics);
}

TEST_F(ProblemGeometryTest, RejectsAMapWithoutNodes) {
    std::ranges::fill(composition_map().numbers, -1.0);
    expect_one(rejected(), "/geometry/composition_map", "no position is a node");
}

// A 5 x 5 x 1 box whose 25 positions hold 7, no composition index of K = 2: 20 entries are
// listed, then one diagnostic gives the 5 not listed (§8).
TEST_F(ProblemGeometryTest, ListsAtMostTwentyInvalidMapValuesOfADataset) {
    constexpr std::size_t side = 5;
    constexpr double invalid_value = 7.0;
    constexpr std::size_t listed_limit = 20;
    geometry("width_x") = float64_array(std::vector<double>(side, 10.0), {side}, "cm");
    geometry("width_y") = float64_array(std::vector<double>(side, 10.0), {side}, "cm");
    geometry("width_z") = float64_array({10.0}, {1U}, "cm");
    composition_map() = FixtureDataset{
        .type = FixtureType::int32,
        .shape = {1U, side, side},
        .numbers = std::vector<double>(side * side, invalid_value),
        .strings = {},
        .attributes = {},
    };
    const Diagnostics diagnostics = rejected();
    const auto listed = std::ranges::count_if(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.location == "/geometry/composition_map" && !diagnostic.entry.empty();
    });
    EXPECT_EQ(listed, static_cast<std::ptrdiff_t>(listed_limit)) << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/geometry/composition_map", "position (0, 0, 0)",
                                     "is not -1 or an input composition index"))
        << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/geometry/composition_map",
                               "5 more diagnostics are not listed"))
        << all_text(diagnostics);
}

// Node numbering (§3): x fastest, excluded positions skipped; position_nodes maps back.
TEST_F(ProblemGeometryTest, NumbersTheNodesInPositionOrderSkippingExcludedPositions) {
    const Problem result = accepted();
    EXPECT_EQ(result.input.reactor.node_positions,
              (std::vector<std::size_t>{1, 2, 3, 4, 5, 7, 8, 9, 10, 11}));
    EXPECT_EQ(result.input.reactor.position_nodes,
              (std::vector<std::int64_t>{-1, 0, 1, 2, 3, 4, -1, 5, 6, 7, 8, 9}));
}

TEST_F(ProblemGeometryTest, RejectsAnUnknownGeometryMember) {
    problem().groups["geometry"].datasets["extra"] = float64_scalar(1.0, "cm");
    expect_one(rejected(), "/geometry/extra", "not a member of the problem input contract");
}

TEST_F(ProblemGeometryTest, RejectsTheGeometryStoredAsADataset) {
    problem().groups.erase("geometry");
    problem().datasets["geometry"] = float64_scalar(1.0, "cm");
    expect_one(rejected(), "/geometry", "not a group");
}

// ----- Boundary conditions: the bounding and exterior faces (§4) -----------------------------

class FaceTest : public ProblemGeometryTest, public testing::WithParamInterface<std::size_t> {};

TEST_P(FaceTest, RejectsAMissingFaceGroup) {
    erase_face(faces[GetParam()]);
    expect_one(rejected(), face_location(GetParam()), "group is missing");
}

TEST_P(FaceTest, RejectsAFaceWithoutKind) {
    face(faces[GetParam()]).attributes.erase("kind");
    expect_one(rejected(), face_location(GetParam()) + "@kind", "attribute is missing");
}

TEST_P(FaceTest, RejectsAnUnknownKind) {
    face(faces[GetParam()]).attributes["kind"] = std::string{"mirror"};
    expect_one(rejected(), face_location(GetParam()) + "@kind", "unknown kind 'mirror'");
}

TEST_P(FaceTest, RejectsAnAlbedoKindWithoutAlbedo) {
    FixtureGroup& this_face = face(faces[GetParam()]);
    this_face.attributes["kind"] = std::string{"albedo"};
    this_face.datasets.erase("albedo");
    expect_one(rejected(), face_location(GetParam()) + "/albedo", "dataset is missing");
}

TEST_P(FaceTest, AcceptsAnAlbedoKindWithAnAlbedoPerGroup) {
    FixtureGroup& this_face = face(faces[GetParam()]);
    this_face.attributes["kind"] = std::string{"albedo"};
    this_face.datasets["albedo"] = float64_array({fast_albedo, thermal_albedo}, {2U}, "1");
    const Problem result = accepted();
    const auto& condition = condition_of(result, GetParam());
    EXPECT_EQ(condition.kind, BoundaryKind::albedo);
    EXPECT_DOUBLE_EQ(condition.albedo[0], fast_albedo);
    EXPECT_DOUBLE_EQ(condition.albedo[1], thermal_albedo);
}

TEST_P(FaceTest, RejectsAnAlbedoDatasetOfAFixedKind) {
    FixtureGroup& this_face = face(faces[GetParam()]);
    this_face.attributes["kind"] = std::string{"vacuum"};
    this_face.datasets["albedo"] = float64_array({fast_albedo, thermal_albedo}, {2U}, "1");
    expect_one(rejected(), face_location(GetParam()) + "/albedo",
               "not a member of the problem input contract");
}

TEST_P(FaceTest, RejectsAnAlbedoOfTheWrongShape) {
    albedo_dataset_of(faces[GetParam()]) =
        float64_array({fast_albedo, thermal_albedo, thermal_albedo}, {3U}, "1");
    expect_one(rejected(), face_location(GetParam()) + "/albedo", "does not match");
}

TEST_P(FaceTest, RejectsAnAlbedoInUnitsOtherThanOne) {
    albedo_dataset_of(faces[GetParam()]).attributes["units"] = std::string{"cm"};
    expect_one(rejected(), face_location(GetParam()) + "/albedo@units",
               "units 'cm' are not the accepted '1'");
}

// The entry of the thermal group is index 1, group 2.
TEST_P(FaceTest, RejectsANegativeAlbedoOfTheThermalGroup) {
    constexpr double negative_albedo = -0.1;
    const std::string location = face_location(GetParam()) + "/albedo";
    albedo_dataset_of(faces[GetParam()]).numbers[1] = negative_albedo;
    const Diagnostics diagnostics = rejected();
    expect_one(diagnostics, location, "is not finite and non-negative");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, location, "index 1 group 2",
                                     "is not finite and non-negative"))
        << all_text(diagnostics);
}

// The entry of the fast group is index 0, group 1; NaN and infinity are both not finite.
TEST_P(FaceTest, RejectsANonFiniteAlbedoOfTheFastGroup) {
    const std::string location = face_location(GetParam()) + "/albedo";
    for (const double value :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        SCOPED_TRACE(value);
        albedo_dataset_of(faces[GetParam()]).numbers[0] = value;
        const Diagnostics diagnostics = rejected();
        expect_one(diagnostics, location, "is not finite and non-negative");
        EXPECT_TRUE(has_entry_diagnostic(diagnostics, location, "index 0 group 1",
                                         "is not finite and non-negative"))
            << all_text(diagnostics);
    }
}

// An albedo face takes no member but `albedo` (§4).
TEST_P(FaceTest, RejectsAnExtraMemberOfAnAlbedoFace) {
    albedo_dataset_of(faces[GetParam()]);
    face(faces[GetParam()]).datasets["extra"] = float64_scalar(1.0, "1");
    expect_one(rejected(), face_location(GetParam()) + "/extra",
               "not a member of the problem input contract");
}

// A face without its kind still has its members checked, against the albedo kind's (§4).
TEST_P(FaceTest, ChecksFaceMembersWhenTheKindIsMissing) {
    FixtureGroup& this_face = face(faces[GetParam()]);
    this_face.attributes.erase("kind");
    this_face.datasets["foo"] = float64_scalar(1.0, "1");
    const Diagnostics diagnostics = rejected();
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(
        has_diagnostic(diagnostics, face_location(GetParam()) + "@kind", "attribute is missing"))
        << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, face_location(GetParam()) + "/foo",
                               "not a member of the problem input contract"))
        << all_text(diagnostics);
}

INSTANTIATE_TEST_SUITE_P(Faces, FaceTest, testing::Range<std::size_t>(0U, faces.size()));

struct FixedKind {
    std::string kind;
    BoundaryKind expected;
    double albedo;
};

class FixedKindTest : public ProblemGeometryTest, public testing::WithParamInterface<FixedKind> {};

// Every face, the exterior included, takes the α_b of its kind in both groups (§4).
TEST_P(FixedKindTest, GivesEveryFaceTheAlbedoOfItsKindInBothGroups) {
    const FixedKind& fixed = GetParam();
    for (const std::string_view name : faces) {
        face(name).attributes["kind"] = fixed.kind;
        face(name).datasets.erase("albedo");
    }
    const Problem result = accepted();
    for (std::size_t f = 0; f < faces.size(); ++f) {
        SCOPED_TRACE(std::string(faces[f]));
        const auto& condition = condition_of(result, f);
        EXPECT_EQ(condition.kind, fixed.expected);
        EXPECT_DOUBLE_EQ(condition.albedo[0], fixed.albedo);
        EXPECT_DOUBLE_EQ(condition.albedo[1], fixed.albedo);
    }
}

INSTANTIATE_TEST_SUITE_P(FixedKinds, FixedKindTest,
                         testing::Values(FixedKind{.kind = "reflective",
                                                   .expected = BoundaryKind::reflective,
                                                   .albedo = reflective_alpha},
                                         FixedKind{.kind = "vacuum",
                                                   .expected = BoundaryKind::vacuum,
                                                   .albedo = vacuum_alpha},
                                         FixedKind{.kind = "zero_flux",
                                                   .expected = BoundaryKind::zero_flux,
                                                   .albedo = zero_flux_alpha}));

TEST_F(ProblemGeometryTest, RejectsAnUnknownFaceGroup) {
    problem().groups["boundary_conditions"].groups["w_minus"].attributes["kind"] =
        std::string{"vacuum"};
    expect_one(rejected(), "/boundary_conditions/w_minus",
               "not a member of the problem input contract");
}

// ----- Boundary conditions: the albedo dataset of the albedo face (§4) ------------------------

TEST_F(ProblemGeometryTest, AcceptsAZeroAlbedoInBothGroups) {
    albedo().numbers = {0.0, 0.0};
    const Problem result = accepted();
    const auto& condition = result.input.reactor.bounding_faces[3];
    EXPECT_DOUBLE_EQ(condition.albedo[0], 0.0);
    EXPECT_DOUBLE_EQ(condition.albedo[1], 0.0);
}

// ----- Boundary conditions: the exterior face (§4) --------------------------------------------

TEST_F(ProblemGeometryTest, RejectsAMissingExteriorWhenAPositionIsExcluded) {
    erase_face("exterior");
    expect_one(rejected(), "/boundary_conditions/exterior", "group is missing");
}

TEST_F(ProblemGeometryTest, AcceptsAMissingExteriorWithoutExcludedPositions) {
    std::ranges::fill(composition_map().numbers, 0.0);
    erase_face("exterior");
    const Problem result = accepted();
    EXPECT_EQ(result.input.reactor.exterior.kind, BoundaryKind::reflective);
}

TEST_F(ProblemGeometryTest, ChecksAnExteriorFacePresentWithoutExcludedPositions) {
    std::ranges::fill(composition_map().numbers, 0.0);
    face("exterior").attributes["kind"] = std::string{"mirror"};
    expect_one(rejected(), "/boundary_conditions/exterior@kind", "unknown kind 'mirror'");
}

} // namespace
