// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/diagnostic.hpp"
#include "nts/io/problem.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "library_fixture.hpp"
#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::Diagnostics;
using nts::test::all_text;
using nts::test::expect_one;
using nts::test::FixtureSet;
using nts::test::FixtureType;
using nts::test::has_diagnostic;
using nts::test::has_entry_diagnostic;
using nts::test::integer_scalar;
using nts::test::ProblemTest;
using nts::test::string_array;
using nts::test::string_scalar;

// The map of make_valid_problem: nx = 3, ny = 2, row-major [z][y][x].
constexpr std::size_t map_nx = 3;
constexpr std::size_t map_ny = 2;
constexpr std::size_t fuel = 0;      // library composition 0 of "test_set"
constexpr std::size_t reflector = 1; // library composition 1 of "test_set"
constexpr std::size_t state_0 = 0;   // the state of the first fuel temperature and boron point

std::size_t position_index(std::size_t i, std::size_t j, std::size_t k) {
    return (((k * map_ny) + j) * map_nx) + i;
}

// ----- Check 6: the library binding (§5) ------------------------------------------------------

TEST_F(ProblemTest, AcceptsARelativeLibraryPathAgainstTheInputDirectory) {
    const auto loaded = accepted();
    EXPECT_EQ(loaded.input.binding.library_file, library_file());
}

TEST_F(ProblemTest, RejectsAMissingLibraryGroup) {
    problem().groups["reactor"].groups.erase("library");
    expect_one(rejected(), "/reactor/library", "group is missing");
}

TEST_F(ProblemTest, RejectsALibraryWithoutAPathAttribute) {
    problem().groups["reactor"].groups["library"].attributes.erase("path");
    expect_one(rejected(), "/reactor/library@path", "attribute is missing");
}

TEST_F(ProblemTest, RejectsAnEmptyLibraryPath) {
    problem().groups["reactor"].groups["library"].attributes["path"] = std::string{};
    expect_one(rejected(), "/reactor/library@path", "the library path is empty");
}

// A relative path is resolved against the input's directory: the diagnostic names the file
// that path denotes there.
TEST_F(ProblemTest, RejectsALibraryFileThatDoesNotExist) {
    problem().groups["reactor"].groups["library"].attributes["path"] = std::string("nowhere.h5");
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().file, (problem_file().parent_path() / "nowhere.h5").string());
    EXPECT_EQ(diagnostics.front().location, "/");
    EXPECT_TRUE(diagnostics.front().message.contains("does not exist"));
}

// §9 item 7: with the library unreadable, the types and lengths are still checked.
TEST_F(ProblemTest, ChecksTheCompositionListsWhileTheLibraryIsUnreadable) {
    problem().groups["reactor"].groups["library"].attributes["path"] = std::string("nowhere.h5");
    problem().groups["reactor"].groups["compositions"].datasets["extra"] = integer_scalar(1);
    problem().groups["reactor"].groups["compositions"].datasets["library_composition"] =
        string_array({"fuel", "reflector", "fuel"});
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 3U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/", "does not exist"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/reactor/compositions/extra",
                               "not a member of the problem input contract"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/reactor/compositions/library_composition",
                               "does not match library_set's"));
}

// §9 item 7: resolution is skipped when the library cannot be read; the name is not reported.
TEST_F(ProblemTest, RejectsADefectiveLibraryAndDoesNotCheckCompositions) {
    library().format_version = 2;
    problem().groups["reactor"].groups["compositions"].datasets["library_composition"].strings[0] =
        "water";
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().file, library_file().string());
    EXPECT_EQ(diagnostics.front().location, "/@format_version");
    EXPECT_TRUE(diagnostics.front().message.contains("format version 2 is not supported"));
}

TEST_F(ProblemTest, RejectsAnUnknownMemberOfTheLibraryGroup) {
    problem().groups["reactor"].groups["library"].datasets["extra"] = integer_scalar(1);
    expect_one(rejected(), "/reactor/library/extra", "not a member of the problem input contract");
}

// ----- Check 7: the input compositions (§5) --------------------------------------------------

// `library_set` and `library_composition` are each a string [K] dataset; the same rules hold for
// both.
class InputCompositionDatasetTest : public ProblemTest,
                                    public testing::WithParamInterface<const char*> {};

TEST_P(InputCompositionDatasetTest, RejectsAMissingDataset) {
    const std::string location = std::string("/reactor/compositions/") + GetParam();
    problem().groups["reactor"].groups["compositions"].datasets.erase(GetParam());
    expect_one(rejected(), location, "dataset is missing");
}

TEST_P(InputCompositionDatasetTest, RejectsADatasetThatIsNotAString) {
    const std::string location = std::string("/reactor/compositions/") + GetParam();
    nts::test::FixtureDataset numbers;
    numbers.type = FixtureType::float64;
    numbers.shape = {2};
    numbers.numbers = {0.0, 0.0};
    problem().groups["reactor"].groups["compositions"].datasets[GetParam()] = numbers;
    expect_one(rejected(), location, "not a string dataset");
}

// The contract's rank 0 scalar is not [K]: a string scalar is refused.
TEST_P(InputCompositionDatasetTest, RejectsAScalarInPlaceOfAList) {
    const std::string location = std::string("/reactor/compositions/") + GetParam();
    problem().groups["reactor"].groups["compositions"].datasets[GetParam()] =
        string_scalar("test_set");
    expect_one(rejected(), location, "is not [K]");
}

INSTANTIATE_TEST_SUITE_P(Datasets, InputCompositionDatasetTest,
                         testing::Values("library_set", "library_composition"));

// K >= 1 (§5). An empty list is the only failure reported: K is then unknown, so the map's
// upper bound and the length of library_composition are not checked against it (§9).
TEST_F(ProblemTest, RejectsAnEmptyListOfInputCompositions) {
    problem().groups["reactor"].groups["compositions"].datasets["library_set"] = string_array({});
    expect_one(rejected(), "/reactor/compositions/library_set", "no input composition is listed");
}

TEST_F(ProblemTest, RejectsALibraryCompositionListOfAnotherLengthThanK) {
    problem().groups["reactor"].groups["compositions"].datasets["library_composition"] =
        string_array({"fuel", "reflector", "fuel"});
    expect_one(rejected(), "/reactor/compositions/library_composition",
               "does not match library_set's");
}

// Fixed-length strings, NUL-padded here, read the same as variable-length ones (§1).
TEST_F(ProblemTest, AcceptsFixedLengthInputCompositionNames) {
    problem().groups["reactor"].groups["compositions"].datasets["library_set"].type =
        FixtureType::fixed_string;
    problem().groups["reactor"].groups["compositions"].datasets["library_composition"].type =
        FixtureType::fixed_string;
    const auto loaded = accepted();
    ASSERT_EQ(loaded.compositions.size(), 2U);
    EXPECT_EQ(loaded.compositions[reflector].composition, reflector);
}

TEST_F(ProblemTest, RejectsAnInputCompositionWithAnUnknownSet) {
    problem().groups["reactor"].groups["compositions"].datasets["library_set"].strings[reflector] =
        "nope";
    const auto diagnostics = rejected();
    expect_one(diagnostics, "/reactor/compositions", "no composition set");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/compositions",
                                     "input composition 1 ('nope', 'reflector')",
                                     "no composition set"));
}

TEST_F(ProblemTest, RejectsAnInputCompositionWithAnUnknownComposition) {
    problem()
        .groups["reactor"]
        .groups["compositions"]
        .datasets["library_composition"]
        .strings[fuel] = "water";
    const auto diagnostics = rejected();
    expect_one(diagnostics, "/reactor/compositions", "has no composition");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/compositions",
                                     "input composition 0 ('test_set', 'water')",
                                     "has no composition"));
}

// Every unresolved input composition is reported, not the first one only.
TEST_F(ProblemTest, ReportsEveryUnresolvedInputComposition) {
    problem().groups["reactor"].groups["compositions"].datasets["library_set"].strings[reflector] =
        "nope";
    problem()
        .groups["reactor"]
        .groups["compositions"]
        .datasets["library_composition"]
        .strings[fuel] = "water";
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/compositions",
                                     "input composition 0 ('test_set', 'water')",
                                     "has no composition"));
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/compositions",
                                     "input composition 1 ('nope', 'reflector')",
                                     "no composition set"));
}

// Three input compositions; input composition 2 names the library's fuel too, and the map uses
// it at position (1, 0, 0). Both input compositions 0 and 2 bind library composition 0.
TEST_F(ProblemTest, AcceptsSeveralInputCompositionsNamingOneLibraryComposition) {
    constexpr std::size_t third = 2;
    problem().groups["reactor"].groups["compositions"].datasets["library_set"] =
        string_array({"test_set", "test_set", "test_set"});
    problem().groups["reactor"].groups["compositions"].datasets["library_composition"] =
        string_array({"fuel", "reflector", "fuel"});
    problem()
        .groups["reactor"]
        .groups["geometry"]
        .datasets["composition_map"]
        .numbers[position_index(1, 0, 0)] = static_cast<double>(third);
    const auto loaded = accepted();
    ASSERT_EQ(loaded.compositions.size(), 3U);
    EXPECT_EQ(loaded.compositions[third].set, 0U);
    EXPECT_EQ(loaded.compositions[third].composition, fuel);
}

// The reflector is used at position (2, 1, 1) and its entry in state 0 is invalid.
TEST_F(ProblemTest, RejectsAUsedCompositionWithAnInvalidEntry) {
    FixtureSet& set = library().sets.front();
    set.valid[(reflector * set.state_count()) + state_0] = 0;
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().file, library_file().string());
    EXPECT_EQ(diagnostics.front().location, "/composition_sets/test_set/valid");
    EXPECT_TRUE(diagnostics.front().message.contains("entry is invalid (valid = 0)"));
    EXPECT_TRUE(diagnostics.front().entry.contains("reflector"));
}

// An invalid entry of a composition no node is made of is not checked.
TEST_F(ProblemTest, AcceptsAnInvalidEntryOfAnUnusedComposition) {
    FixtureSet& set = library().sets.front();
    set.valid[(reflector * set.state_count()) + state_0] = 0;
    problem()
        .groups["reactor"]
        .groups["geometry"]
        .datasets["composition_map"]
        .numbers[position_index(2, 1, 1)] = static_cast<double>(fuel);
    accepted();
}

// §5, §9 item 7: the used reflector is checked though the fuel does not resolve.
TEST_F(ProblemTest, ChecksAUsedResolvedCompositionWhileAnotherIsUnresolved) {
    FixtureSet& set = library().sets.front();
    set.valid[(reflector * set.state_count()) + state_0] = 0;
    problem()
        .groups["reactor"]
        .groups["compositions"]
        .datasets["library_composition"]
        .strings[fuel] = "water";
    const auto diagnostics = rejected();
    ASSERT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/compositions",
                                     "input composition 0 ('test_set', 'water')",
                                     "composition set 'test_set' has no composition 'water'"));
    EXPECT_EQ(diagnostics.back().file, library_file().string());
    EXPECT_EQ(diagnostics.back().location, "/composition_sets/test_set/valid");
    EXPECT_TRUE(diagnostics.back().entry.contains("reflector"));
    EXPECT_TRUE(diagnostics.back().message.contains("entry is invalid (valid = 0)"));
}

TEST_F(ProblemTest, RejectsAnUnknownMemberOfTheCompositionsGroup) {
    problem().groups["reactor"].groups["compositions"].datasets["extra"] = integer_scalar(1);
    expect_one(rejected(), "/reactor/compositions/extra",
               "not a member of the problem input contract");
}

// The map's index is checked against K, the length of library_set.
TEST_F(ProblemTest, RejectsAMapValueAtOrAboveK) {
    constexpr double first_out_of_range = 2.0; // K of the default problem
    problem()
        .groups["reactor"]
        .groups["geometry"]
        .datasets["composition_map"]
        .numbers[position_index(1, 0, 0)] = first_out_of_range;
    const auto diagnostics = rejected();
    expect_one(diagnostics, "/reactor/geometry/composition_map",
               "is not -1 or an input composition index below 2");
    EXPECT_TRUE(has_entry_diagnostic(diagnostics, "/reactor/geometry/composition_map",
                                     "position (1, 0, 0)",
                                     "is not -1 or an input composition index below 2"));
}

} // namespace
