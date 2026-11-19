// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/common/diagnostic.hpp"
#include "nts/io/nodal_library.hpp"

#include <gtest/gtest.h>

#include <H5Cpp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "library_fixture.hpp"

namespace {

using nts::Diagnostics;
using nts::NodalLibrary;
using nts::Quantity;
using nts::test::entry_value;
using nts::test::FixtureAxis;
using nts::test::FixtureLibrary;
using nts::test::make_valid_fixture;

std::string set_path() {
    return "/composition_sets/test_set";
}

// The probe entry: state 1 of a composition in the default fixture (two compositions, axes
// fuel_temperature [600, 900] K and boron [0, 1000] ppm), i.e. fuel_temperature[0] and
// boron[1]. Entry 1·4 + 1 = 5 of the reflector is neither first nor last, a composition-state
// swap would name state 2, and a first-axis-fastest state order would name fuel_temperature[1].
constexpr std::size_t fuel = 0;
constexpr std::size_t reflector = 1;
constexpr std::size_t probe_state = 1;
std::string probe_text(std::size_t composition) {
    return std::format("composition {} '{}', fuel_temperature[0] = 600 K, boron[1] = 1000 ppm",
                       composition, composition == fuel ? "fuel" : "reflector");
}

// ----- Contract §3.3, restated so that no test takes a rule from the code under test --------

struct ContractQuantity {
    std::string_view name;
    std::string_view units;
    std::size_t components;
    bool required;
    nts::EntryRule rule;
};

constexpr ContractQuantity contract_row(std::string_view name, std::string_view units,
                                        std::size_t components, bool required,
                                        nts::EntryRule rule) {
    return {
        .name = name, .units = units, .components = components, .required = required, .rule = rule};
}

constexpr auto positive = nts::EntryRule::positive;
constexpr auto non_negative = nts::EntryRule::non_negative;

// In Quantity order.
constexpr std::array<ContractQuantity, nts::quantity_count> contract_quantities{
    contract_row("D1", "cm", 1, true, positive),
    contract_row("D2", "cm", 1, true, positive),
    contract_row("Sigma_a1", "cm^-1", 1, true, positive),
    contract_row("Sigma_a2", "cm^-1", 1, true, positive),
    contract_row("Sigma_s12", "cm^-1", 1, true, positive),
    contract_row("Sigma_s21", "cm^-1", 1, true, non_negative),
    contract_row("nu_Sigma_f1", "cm^-1", 1, true, non_negative),
    contract_row("nu_Sigma_f2", "cm^-1", 1, true, non_negative),
    contract_row("kappa_Sigma_f1", "J cm^-1", 1, true, non_negative),
    contract_row("kappa_Sigma_f2", "J cm^-1", 1, true, non_negative),
    contract_row("inv_v1", "s cm^-1", 1, true, positive),
    contract_row("inv_v2", "s cm^-1", 1, true, positive),
    contract_row("beta", "1", 8, true, nts::EntryRule::delayed_fraction),
    contract_row("lambda", "s^-1", 8, true, nts::EntryRule::decay_constant),
    contract_row("Sigma_f1", "cm^-1", 1, false, non_negative),
    contract_row("Sigma_f2", "cm^-1", 1, false, non_negative),
    contract_row("discontinuity_factor", "1", 12, false, positive),
};

// Contract §5: "delayed group <k>", or "face <face>, group <g>" over the [6, 2] C-order
// extent (faces x-, x+, y-, y+, z-, z+; energy group fastest).
std::string contract_component_text(std::string_view quantity, std::size_t component) {
    if (quantity == "beta" || quantity == "lambda") {
        return std::format("delayed group {}", component);
    }
    if (quantity == "discontinuity_factor") {
        constexpr std::array<std::string_view, 6> faces{"x-", "x+", "y-", "y+", "z-", "z+"};
        return std::format("face {}, group {}", faces.at(component / 2), (component % 2) + 1);
    }
    return {};
}

// ----- Test fixture and helpers -------------------------------------------------------------

class NodalLibraryTest : public testing::Test {
protected:
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    std::filesystem::path write(const FixtureLibrary& library) {
        nts::test::write_fixture(library, path_);
        return path_;
    }

    std::expected<NodalLibrary, Diagnostics> read(const FixtureLibrary& library) {
        return nts::read_nodal_library(write(library));
    }

    // Writes the library, lets `edit` change the file through the HDF5 API (for defects the
    // fixture cannot describe), then reads it.
    template <typename Edit>
    std::expected<NodalLibrary, Diagnostics> read_edited(const FixtureLibrary& library, Edit edit) {
        write(library);
        {
            const H5::H5File file(path_.string(), H5F_ACC_RDWR);
            edit(file);
        }
        return nts::read_nodal_library(path_);
    }

    // Reads a defective fixture; the read must fail.
    Diagnostics rejected(const FixtureLibrary& library) { return failure(read(library)); }

    template <typename Edit> Diagnostics rejected_edited(const FixtureLibrary& library, Edit edit) {
        return failure(read_edited(library, edit));
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    static Diagnostics failure(const std::expected<NodalLibrary, Diagnostics>& result) {
        EXPECT_FALSE(result.has_value()) << "a defective library was accepted";
        return result.has_value() ? Diagnostics{} : result.error();
    }

    std::filesystem::path path_ =
        nts::test::fixture_path(testing::UnitTest::GetInstance()->current_test_info()->name());
};

std::string all_text(const Diagnostics& diagnostics) {
    std::string text;
    for (const auto& diagnostic : diagnostics) {
        text += nts::to_string(diagnostic) + "\n";
    }
    return text;
}

bool has_location(const Diagnostics& diagnostics, std::string_view location) {
    return std::ranges::any_of(diagnostics, [&](const auto& d) { return d.location == location; });
}

// A diagnostic at `location` whose message contains `message`.
bool has_diagnostic(const Diagnostics& diagnostics, std::string_view location,
                    std::string_view message) {
    return std::ranges::any_of(diagnostics, [&](const auto& d) {
        return d.location == location && d.message.find(message) != std::string::npos;
    });
}

// Exactly one diagnostic, at `location`, whose message contains `message`.
void expect_one(const Diagnostics& diagnostics, std::string_view location,
                std::string_view message) {
    EXPECT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, location, message))
        << "expected '" << message << "' at " << location << "\n"
        << all_text(diagnostics);
}

nts::test::FixtureSet& only_set(FixtureLibrary& library) {
    return library.sets.front();
}

H5::Group test_set_group(const H5::H5File& file) {
    return file.openGroup(set_path());
}

void write_units(const H5::H5Object& object, const std::string& units) {
    const H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    object.createAttribute("units", type, H5::DataSpace(H5S_SCALAR)).write(type, units);
}

// Replaces the member `name` of `parent` by a dataset of the given type and dimensions, filled
// with ones (strings are left unwritten).
H5::DataSet replace_with_dataset(const H5::Group& parent, const std::string& name,
                                 const H5::DataType& type, const std::vector<hsize_t>& dims) {
    if (parent.nameExists(name)) {
        parent.unlink(name);
    }
    const H5::DataSpace space(static_cast<int>(dims.size()), dims.data());
    const H5::DataSet dataset = parent.createDataSet(name, type, space);
    std::size_t count = 1;
    for (const hsize_t d : dims) {
        count *= static_cast<std::size_t>(d);
    }
    if (count > 0 && type.getClass() != H5T_STRING) {
        const std::vector<double> ones(count, 1.0);
        dataset.write(ones.data(), H5::PredType::NATIVE_DOUBLE);
    }
    return dataset;
}

// Replaces the attribute `name` of `object` by one of the given type holding `values`.
void replace_attribute(const H5::H5Object& object, const std::string& name,
                       const H5::DataType& type, const std::vector<double>& values) {
    if (object.attrExists(name)) {
        object.removeAttr(name);
    }
    const std::array<hsize_t, 1> dims{values.size()};
    const H5::DataSpace space =
        values.size() == 1 ? H5::DataSpace(H5S_SCALAR) : H5::DataSpace(1, dims.data());
    object.createAttribute(name, type, space).write(H5::PredType::NATIVE_DOUBLE, values.data());
}

// ----- A valid library ----------------------------------------------------------------------

TEST_F(NodalLibraryTest, ReadsEveryFieldOfAValidLibrary) {
    const FixtureLibrary fixture = make_valid_fixture();
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    EXPECT_EQ(library->format_version, 1);
    EXPECT_EQ(library->group_boundary_ev, 0.625);
    EXPECT_EQ(library->generator, "library_fixture");
    EXPECT_EQ(library->generator_commit, "0123456789abcdef");
    ASSERT_EQ(library->composition_sets.size(), 1U);
    const nts::CompositionSet& set = library->composition_sets.front();
    EXPECT_EQ(set.name, "test_set");
    EXPECT_EQ(set.composition_names, (std::vector<std::string>{"fuel", "reflector"}));
    ASSERT_EQ(set.axes.size(), 2U);
    EXPECT_EQ(set.axes[0].axis, nts::StateAxis::fuel_temperature);
    EXPECT_EQ(set.axes[0].points, (std::vector<double>{600.0, 900.0}));
    EXPECT_EQ(set.axes[1].axis, nts::StateAxis::boron);
    EXPECT_EQ(set.state_count, 4U);
    EXPECT_EQ(set.valid, std::vector<std::uint8_t>(8, 1));
    for (const auto& spec : nts::quantity_specs()) {
        const nts::QuantityTable* table = set.find(spec.quantity);
        ASSERT_NE(table, nullptr) << spec.name;
        const auto& expected = fixture.sets.front().quantities.at(std::string(spec.name));
        EXPECT_EQ(table->value, expected.value) << spec.name;
        EXPECT_EQ(table->std_dev, expected.std_dev) << spec.name;
    }
}

// Axes of lengths 2, 3, 4 and 1: a reader that ordered the axes otherwise than the
// vocabulary, or decoded a state with the first axis fastest, would expect another shape or
// name another point.
TEST_F(NodalLibraryTest, ReadsAllFourAxesInVocabularyOrder) {
    const FixtureLibrary fixture = nts::test::make_valid_fixture_on_axes({
        FixtureAxis{.name = "fuel_temperature", .units = "K", .points = {600.0, 900.0}},
        FixtureAxis{
            .name = "moderator_density", .units = "kg m^-3", .points = {600.0, 700.0, 750.0}},
        FixtureAxis{
            .name = "moderator_temperature", .units = "K", .points = {550.0, 560.0, 570.0, 580.0}},
        FixtureAxis{.name = "boron", .units = "ppm", .points = {975.0}},
    });
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto& set = library->composition_sets.front();
    ASSERT_EQ(set.axes.size(), 4U);
    EXPECT_EQ(set.axes[0].axis, nts::StateAxis::fuel_temperature);
    EXPECT_EQ(set.axes[1].axis, nts::StateAxis::moderator_density);
    EXPECT_EQ(set.axes[1].points, (std::vector<double>{600.0, 700.0, 750.0}));
    EXPECT_EQ(set.axes[2].axis, nts::StateAxis::moderator_temperature);
    EXPECT_EQ(set.axes[2].points, (std::vector<double>{550.0, 560.0, 570.0, 580.0}));
    EXPECT_EQ(set.axes[3].axis, nts::StateAxis::boron);
    EXPECT_EQ(set.state_count, 24U);
    // State 18 = ((1·3 + 1)·4 + 2)·1 + 0.
    EXPECT_EQ(nts::describe_entry(set, reflector, 18),
              "composition 1 'reflector', fuel_temperature[1] = 900 K, "
              "moderator_density[1] = 700 kg m^-3, moderator_temperature[2] = 570 K, "
              "boron[0] = 975 ppm");
    EXPECT_EQ(nts::state_axis_units(nts::StateAxis::moderator_density), "kg m^-3");
    EXPECT_EQ(nts::state_axis_units(nts::StateAxis::moderator_temperature), "K");
}

TEST_F(NodalLibraryTest, ReadsFixedLengthNames) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).fixed_length_names = true;
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    EXPECT_EQ(library->composition_sets.front().composition_names,
              (std::vector<std::string>{"fuel", "reflector"}));
}

TEST_F(NodalLibraryTest, ReadsSpacePaddedFixedLengthStrings) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).fixed_length_names = true;
    only_set(fixture).space_padded_names = true;
    const auto library = read_edited(fixture, [](const H5::H5File& file) {
        const H5::Group d1 = test_set_group(file).openGroup("D1");
        d1.removeAttr("units");
        const H5::StrType type(H5::PredType::C_S1, 8);
        type.setStrpad(H5T_STR_SPACEPAD);
        const std::string padded = "cm      ";
        d1.createAttribute("units", type, H5::DataSpace(H5S_SCALAR)).write(type, padded.data());
    });
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    EXPECT_EQ(library->composition_sets.front().composition_names,
              (std::vector<std::string>{"fuel", "reflector"}));
}

TEST_F(NodalLibraryTest, ReadsASetWithoutAxesAsOneState) {
    FixtureLibrary fixture = make_valid_fixture();
    auto& set = only_set(fixture);
    set.axes.clear();
    for (auto& [name, quantity] : set.quantities) {
        // Keep the first state of each composition.
        const std::size_t width = quantity.components;
        std::vector<double> value;
        std::vector<double> std_dev;
        for (std::size_t c = 0; c < 2; ++c) {
            const auto first = static_cast<std::ptrdiff_t>(c * 4 * width);
            value.insert(value.end(), quantity.value.begin() + first,
                         quantity.value.begin() + first + static_cast<std::ptrdiff_t>(width));
            std_dev.insert(std_dev.end(), quantity.std_dev.begin() + first,
                           quantity.std_dev.begin() + first + static_cast<std::ptrdiff_t>(width));
        }
        quantity.value = value;
        quantity.std_dev = std_dev;
        quantity.shape.erase(quantity.shape.begin() + 1, quantity.shape.begin() + 3);
    }
    set.valid.assign(2, 1);
    set.valid_shape = {2};
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto& read_set = library->composition_sets.front();
    EXPECT_EQ(read_set.state_count, 1U);
    EXPECT_EQ(nts::describe_entry(read_set, 1, 0), "composition 1 'reflector'");
}

TEST_F(NodalLibraryTest, ReadsEverySetInNameOrder) {
    FixtureLibrary fixture = make_valid_fixture();
    nts::test::FixtureSet second = only_set(fixture);
    only_set(fixture).name = "b_set";
    second.name = "a_set";
    fixture.sets.push_back(second);
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    ASSERT_EQ(library->composition_sets.size(), 2U);
    EXPECT_EQ(library->composition_sets[0].name, "a_set");
    EXPECT_EQ(library->composition_sets[1].name, "b_set");

    // A defect in one set names that set.
    entry_value(fixture.sets.front(), "D1", reflector, probe_state) = -1.0;
    const auto diagnostics = rejected(fixture);
    expect_one(diagnostics, "/composition_sets/b_set/D1/value", "breaks the rule > 0");
}

TEST_F(NodalLibraryTest, IgnoresMembersItDoesNotKnow) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.extra_root_groups = {"future_root_group"};
    only_set(fixture).extra_groups = {"future_quantity"};
    const auto library = read(fixture);
    EXPECT_TRUE(library.has_value()) << all_text(library.error());
}

TEST_F(NodalLibraryTest, AcceptsAbsentOptionalQuantities) {
    FixtureLibrary fixture = make_valid_fixture();
    for (const char* name : {"Sigma_f1", "Sigma_f2", "discontinuity_factor"}) {
        only_set(fixture).quantities.erase(name);
    }
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto& set = library->composition_sets.front();
    EXPECT_EQ(set.find(Quantity::discontinuity_factor), nullptr);
    EXPECT_EQ(set.find(Quantity::sigma_f1), nullptr);
    EXPECT_NE(set.find(Quantity::sigma_s21), nullptr);
}

// ----- The file and its root attributes (contract §4 items 1-2) -----------------------------

TEST_F(NodalLibraryTest, RejectsAMissingFile) {
    const auto library = nts::read_nodal_library(path());
    ASSERT_FALSE(library.has_value());
    ASSERT_EQ(library.error().size(), 1U);
    EXPECT_EQ(library.error().front().file, path().string());
    EXPECT_NE(library.error().front().message.find("does not exist"), std::string::npos);
}

TEST_F(NodalLibraryTest, RejectsAFileThatIsNotHdf5) {
    std::ofstream(path()) << "not an HDF5 file\n";
    const auto library = nts::read_nodal_library(path());
    ASSERT_FALSE(library.has_value());
    expect_one(library.error(), "/", "not an HDF5 file");
}

// Items 3 to 5 are skipped when item 2 fails: the missing set is not reported.
TEST_F(NodalLibraryTest, RejectsAnUnsupportedFormatVersion) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.format_version = 2;
    fixture.sets.clear();
    expect_one(rejected(fixture), "/@format_version", "format version 2 is not supported");
}

TEST_F(NodalLibraryTest, RejectsGroupCountsOtherThanTwoAndEight) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.energy_groups = 4;
    fixture.delayed_groups = 6;
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@energy_groups", "4 energy groups"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@delayed_groups", "6 delayed groups"));
}

TEST_F(NodalLibraryTest, RejectsMissingRootAttributes) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.format_version.reset();
    fixture.energy_groups.reset();
    fixture.delayed_groups.reset();
    fixture.group_boundary_ev.reset();
    fixture.generator.reset();
    fixture.generator_commit.reset();
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 6U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@format_version",
                               "attribute is missing; expected an integer"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@energy_groups", "attribute is missing"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@delayed_groups", "attribute is missing"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@group_boundary_eV",
                               "attribute is missing; expected float64"));
    EXPECT_TRUE(
        has_diagnostic(diagnostics, "/@generator", "attribute is missing; expected a string"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@generator_commit", "attribute is missing"));
}

TEST_F(NodalLibraryTest, RejectsEmptyGeneratorText) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.generator = "";
    expect_one(rejected(fixture), "/@generator", "attribute is empty");
}

TEST_F(NodalLibraryTest, RejectsAGroupBoundaryThatIsNotFiniteAndPositive) {
    for (const double boundary :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(), 0.0, -1.0}) {
        SCOPED_TRACE(boundary);
        FixtureLibrary fixture = make_valid_fixture();
        fixture.group_boundary_ev = boundary;
        expect_one(rejected(fixture), "/@group_boundary_eV", "is not finite and positive");
    }
}

TEST_F(NodalLibraryTest, RejectsRootAttributesOfTheWrongTypeOrShape) {
    const auto diagnostics = rejected_edited(make_valid_fixture(), [](const H5::H5File& file) {
        replace_attribute(file, "format_version", H5::PredType::IEEE_F64LE, {1.0});
        replace_attribute(file, "energy_groups", H5::PredType::STD_I64LE, {2.0, 2.0});
        replace_attribute(file, "delayed_groups", H5::PredType::STD_I64LE, {8.0, 8.0, 8.0});
        replace_attribute(file, "group_boundary_eV", H5::PredType::IEEE_F32LE, {0.625});
        replace_attribute(file, "generator", H5::PredType::STD_I64LE, {1.0});
    });
    EXPECT_EQ(diagnostics.size(), 5U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@format_version", "attribute is not an integer"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@energy_groups",
                               "attribute does not hold exactly one value"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@delayed_groups",
                               "attribute does not hold exactly one value"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@group_boundary_eV", "attribute is not float64"));
    EXPECT_TRUE(has_diagnostic(diagnostics, "/@generator", "attribute is not a string"));
}

TEST_F(NodalLibraryTest, RejectsAnIntegerGroupBoundary) {
    const auto diagnostics = rejected_edited(make_valid_fixture(), [](const H5::H5File& file) {
        replace_attribute(file, "group_boundary_eV", H5::PredType::STD_I64LE, {1.0});
    });
    expect_one(diagnostics, "/@group_boundary_eV", "attribute is not float64");
}

// ----- Sets, axes, names, quantities and flags (contract §4 items 3-4) ----------------------

TEST_F(NodalLibraryTest, RejectsAMissingGroupOfCompositionSets) {
    const auto diagnostics = rejected_edited(
        make_valid_fixture(), [](const H5::H5File& file) { file.unlink("composition_sets"); });
    expect_one(diagnostics, "/composition_sets", "the group of composition sets is missing");
}

TEST_F(NodalLibraryTest, RejectsALibraryWithoutSets) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.sets.clear();
    expect_one(rejected(fixture), "/composition_sets", "holds no composition set");
}

TEST_F(NodalLibraryTest, RejectsEachMissingRequiredQuantity) {
    for (const ContractQuantity& row : contract_quantities) {
        if (!row.required) {
            continue;
        }
        SCOPED_TRACE(row.name);
        FixtureLibrary fixture = make_valid_fixture();
        only_set(fixture).quantities.erase(std::string(row.name));
        expect_one(rejected(fixture), set_path() + "/" + std::string(row.name),
                   std::format("required quantity {} is missing", row.name));
    }
}

TEST_F(NodalLibraryTest, RejectsWrongUnits) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).quantities.at("D2").units = "m";
    only_set(fixture).axes[0].units = "C";
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, set_path() + "/D2@units",
                               "units 'm' are not the accepted 'cm'"));
    EXPECT_TRUE(has_diagnostic(diagnostics, set_path() + "/axes/fuel_temperature@units",
                               "units 'C' are not the accepted 'K'"));
}

TEST_F(NodalLibraryTest, RejectsAWrongShape) {
    FixtureLibrary fixture = make_valid_fixture();
    auto& beta = only_set(fixture).quantities.at("beta");
    beta.shape.back() = 6; // six delayed groups
    beta.value.resize(std::size_t{2} * 4 * 6);
    beta.std_dev.resize(std::size_t{2} * 4 * 6);
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, set_path() + "/beta/value",
                               "shape [2, 2, 2, 6] is not the expected [2, 2, 2, 8]"));
    EXPECT_TRUE(has_location(diagnostics, set_path() + "/beta/std_dev"));
}

TEST_F(NodalLibraryTest, RejectsMalformedQuantityGroups) {
    struct Case {
        std::string_view label;
        void (*edit)(const H5::Group& set);
        std::string location;
        std::string_view message;
    };
    const std::vector<Case> cases{
        {.label = "quantity_as_dataset",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "D1", H5::PredType::IEEE_F64LE, {2, 2, 2});
             },
         .location = set_path() + "/D1",
         .message = "quantity is not a group"},
        {.label = "value_missing",
         .edit = [](const H5::Group& set) { set.openGroup("D2").unlink("value"); },
         .location = set_path() + "/D2/value",
         .message = "dataset is missing"},
        {.label = "std_dev_missing",
         .edit = [](const H5::Group& set) { set.openGroup("D2").unlink("std_dev"); },
         .location = set_path() + "/D2/std_dev",
         .message = "dataset is missing"},
        {.label = "value_float32",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set.openGroup("Sigma_a1"), "value", H5::PredType::IEEE_F32LE,
                                      {2, 2, 2});
             },
         .location = set_path() + "/Sigma_a1/value",
         .message = "dataset is not float64"},
        {.label = "std_dev_int64",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set.openGroup("Sigma_a1"), "std_dev", H5::PredType::STD_I64LE,
                                      {2, 2, 2});
             },
         .location = set_path() + "/Sigma_a1/std_dev",
         .message = "dataset is not float64"},
        {.label = "units_missing",
         .edit = [](const H5::Group& set) { set.openGroup("lambda").removeAttr("units"); },
         .location = set_path() + "/lambda@units",
         .message = "attribute is missing; expected a string"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.label);
        const auto diagnostics = rejected_edited(
            make_valid_fixture(), [&](const H5::H5File& file) { c.edit(test_set_group(file)); });
        expect_one(diagnostics, c.location, c.message);
    }
}

TEST_F(NodalLibraryTest, RejectsAnUnknownAxis) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).axes[1].name = "xenon";
    expect_one(rejected(fixture), set_path() + "/axes/xenon", "unknown state axis 'xenon'");
}

TEST_F(NodalLibraryTest, RejectsMalformedAxes) {
    struct Case {
        std::string_view label;
        void (*edit)(const H5::Group& set);
        std::string location;
        std::string_view message;
    };
    const std::string fuel_axis = set_path() + "/axes/fuel_temperature";
    const std::vector<Case> cases{
        {.label = "float32",
         .edit =
             [](const H5::Group& set) {
                 write_units(replace_with_dataset(set.openGroup("axes"), "fuel_temperature",
                                                  H5::PredType::IEEE_F32LE, {2}),
                             "K");
             },
         .location = fuel_axis,
         .message = "axis must be a 1-D float64 dataset; found shape [2]"},
        {.label = "two_dimensional",
         .edit =
             [](const H5::Group& set) {
                 write_units(replace_with_dataset(set.openGroup("axes"), "fuel_temperature",
                                                  H5::PredType::IEEE_F64LE, {2, 1}),
                             "K");
             },
         .location = fuel_axis,
         .message = "axis must be a 1-D float64 dataset; found shape [2, 1]"},
        {.label = "group",
         .edit =
             [](const H5::Group& set) {
                 const H5::Group axes = set.openGroup("axes");
                 axes.unlink("fuel_temperature");
                 static_cast<void>(axes.createGroup("fuel_temperature"));
             },
         .location = fuel_axis,
         .message = "axis is not a dataset"},
        {.label = "units_missing",
         .edit =
             [](const H5::Group& set) {
                 set.openGroup("axes").openDataSet("fuel_temperature").removeAttr("units");
             },
         .location = fuel_axis + "@units",
         .message = "attribute is missing; expected a string"},
        {.label = "axes_not_a_group",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "axes", H5::PredType::IEEE_F64LE, {2});
             },
         .location = set_path() + "/axes",
         .message = "'axes' is not a group"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.label);
        const auto diagnostics = rejected_edited(
            make_valid_fixture(), [&](const H5::H5File& file) { c.edit(test_set_group(file)); });
        expect_one(diagnostics, c.location, c.message);
    }
}

TEST_F(NodalLibraryTest, RejectsBadAxisPoints) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    struct Case {
        std::vector<double> points;
        std::string_view message;
    };
    const std::vector<Case> cases{
        {.points = {900.0, 600.0}, .message = "points are not strictly ascending"},
        {.points = {600.0, 600.0}, .message = "points are not strictly ascending"},
        {.points = {600.0, nan}, .message = "point [1] = nan is not finite"},
        {.points = {600.0, inf}, .message = "point [1] = inf is not finite"},
        {.points = {-inf, 600.0}, .message = "point [0] = -inf is not finite"},
        {.points = {0.0, 600.0}, .message = "point [0] = 0 K is outside the bound > 0"},
        {.points = {}, .message = "axis has no points; it needs at least one"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.message);
        FixtureLibrary fixture = make_valid_fixture();
        only_set(fixture).axes[0].points = c.points;
        expect_one(rejected(fixture), set_path() + "/axes/fuel_temperature", c.message);
    }
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).axes[1].points = {-1.0, 1000.0};
    expect_one(rejected(fixture), set_path() + "/axes/boron",
               "point [0] = -1 ppm is outside the bound >= 0");
}

TEST_F(NodalLibraryTest, RejectsBadModeratorAxes) {
    const auto four_axes = [] {
        return std::vector<FixtureAxis>{
            FixtureAxis{.name = "fuel_temperature", .units = "K", .points = {600.0, 900.0}},
            FixtureAxis{.name = "moderator_density", .units = "kg m^-3", .points = {600.0, 700.0}},
            FixtureAxis{.name = "moderator_temperature", .units = "K", .points = {560.0, 580.0}},
            FixtureAxis{.name = "boron", .units = "ppm", .points = {975.0}},
        };
    };
    for (const std::size_t a : {std::size_t{1}, std::size_t{2}}) {
        auto axes = four_axes();
        const std::string location = set_path() + "/axes/" + axes[a].name;
        SCOPED_TRACE(axes[a].name);
        const std::string accepted = axes[a].units;
        axes[a].units = "g cm^-3";
        expect_one(rejected(nts::test::make_valid_fixture_on_axes(axes)), location + "@units",
                   std::format("units 'g cm^-3' are not the accepted '{}'", accepted));
        axes = four_axes();
        axes[a].points.front() = 0.0;
        expect_one(rejected(nts::test::make_valid_fixture_on_axes(axes)), location,
                   "is outside the bound > 0");
    }
}

TEST_F(NodalLibraryTest, RejectsDuplicateAndEmptyCompositionNames) {
    FixtureLibrary fixture = make_valid_fixture(3);
    only_set(fixture).names = {"fuel", "fuel", ""};
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, set_path() + "/names",
                               "composition name 'fuel' is used by compositions 0 and 1"));
    EXPECT_TRUE(
        has_diagnostic(diagnostics, set_path() + "/names", "composition 2 has an empty name"));
}

TEST_F(NodalLibraryTest, RejectsMalformedCompositionNames) {
    struct Case {
        std::string_view label;
        void (*edit)(const H5::Group& set);
        std::string_view message;
    };
    const std::vector<Case> cases{
        {.label = "missing",
         .edit = [](const H5::Group& set) { set.unlink("names"); },
         .message = "the composition names dataset is missing"},
        {.label = "float64",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "names", H5::PredType::IEEE_F64LE, {2});
             },
         .message = "composition names must be a 1-D string dataset"},
        {.label = "two_dimensional",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "names", H5::StrType(H5::PredType::C_S1, H5T_VARIABLE),
                                      {2, 1});
             },
         .message = "composition names must be a 1-D string dataset"},
        {.label = "empty",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "names", H5::StrType(H5::PredType::C_S1, H5T_VARIABLE),
                                      {0});
             },
         .message = "the set holds no composition"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.label);
        const auto diagnostics = rejected_edited(
            make_valid_fixture(), [&](const H5::H5File& file) { c.edit(test_set_group(file)); });
        expect_one(diagnostics, set_path() + "/names", c.message);
    }
}

TEST_F(NodalLibraryTest, RejectsMalformedValidityFlags) {
    struct Case {
        std::string_view label;
        void (*edit)(const H5::Group& set);
        std::string_view message;
    };
    const std::vector<Case> cases{
        {.label = "missing",
         .edit = [](const H5::Group& set) { set.unlink("valid"); },
         .message = "the validity flags are missing"},
        {.label = "int64",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "valid", H5::PredType::STD_I64LE, {2, 2, 2});
             },
         .message = "validity flags are not uint8"},
        {.label = "int8",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "valid", H5::PredType::STD_I8LE, {2, 2, 2});
             },
         .message = "validity flags are not uint8"},
        {.label = "float64",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "valid", H5::PredType::IEEE_F64LE, {2, 2, 2});
             },
         .message = "validity flags are not uint8"},
        {.label = "uint16",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "valid", H5::PredType::STD_U16LE, {2, 2, 2});
             },
         .message = "validity flags are not uint8"},
        {.label = "wrong_shape",
         .edit =
             [](const H5::Group& set) {
                 replace_with_dataset(set, "valid", H5::PredType::STD_U8LE, {2, 4});
             },
         .message = "shape [2, 4] is not the expected [2, 2, 2]"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.label);
        const auto diagnostics = rejected_edited(
            make_valid_fixture(), [&](const H5::H5File& file) { c.edit(test_set_group(file)); });
        expect_one(diagnostics, set_path() + "/valid", c.message);
    }
}

// The flag sits in the fuel (entry 1): a composition-state swap would name the reflector.
TEST_F(NodalLibraryTest, RejectsValidityFlagsOtherThanZeroAndOne) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).valid[(fuel * 4) + probe_state] = 2;
    const auto diagnostics = rejected(fixture);
    expect_one(diagnostics, set_path() + "/valid", "validity flag 2 is neither 0 nor 1");
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics.front().entry, probe_text(fuel));
}

// A flag other than 0 and 1 does not hide an entry-rule violation in an entry marked valid.
TEST_F(NodalLibraryTest, ReportsEntryRulesAlongsideBadValidityFlags) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).valid[(reflector * 4) + probe_state] = 2;
    entry_value(only_set(fixture), "D1", fuel, probe_state) = -1.0;
    const auto diagnostics = rejected(fixture);
    ASSERT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& d) {
        return d.location == set_path() + "/valid" &&
               d.message == "validity flag 2 is neither 0 nor 1" &&
               d.entry == probe_text(reflector);
    })) << all_text(diagnostics);
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& d) {
        return d.location == set_path() + "/D1/value" &&
               d.message.find("breaks the rule > 0") != std::string::npos &&
               d.entry == probe_text(fuel);
    })) << all_text(diagnostics);
}

TEST_F(NodalLibraryTest, ReportsEveryProblemTogether) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).quantities.erase("D1");
    only_set(fixture).quantities.at("lambda").units = "s";
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_location(diagnostics, set_path() + "/D1"));
    EXPECT_TRUE(has_location(diagnostics, set_path() + "/lambda@units"));
}

// A structural problem in one quantity does not hide an entry-rule violation in another.
TEST_F(NodalLibraryTest, ReportsEntryRulesAlongsideStructuralProblems) {
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).quantities.at("kappa_Sigma_f1").units = "eV cm^-1";
    entry_value(only_set(fixture), "D1", reflector, probe_state) = -1.0;
    const auto diagnostics = rejected(fixture);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_location(diagnostics, set_path() + "/kappa_Sigma_f1@units"));
    EXPECT_TRUE(has_diagnostic(diagnostics, set_path() + "/D1/value", "breaks the rule > 0"));
}

// An HDF5 filter that encodes and fails to decode: reading a dataset through it fails inside
// HDF5. IDs 256-511 are reserved for testing.
constexpr H5Z_filter_t failing_filter_id = 300;

std::size_t fail_on_read(unsigned int flags, std::size_t /*cd_nelmts*/,
                         const unsigned int* /*cd_values*/, std::size_t nbytes,
                         std::size_t* /*buf_size*/, void** /*buf*/) {
    return (flags & H5Z_FLAG_REVERSE) != 0U ? 0 : nbytes;
}

TEST_F(NodalLibraryTest, NamesADatasetHdf5CannotReadAndGoesOn) {
    const H5Z_class2_t filter{.version = H5Z_CLASS_T_VERS,
                              .id = failing_filter_id,
                              .encoder_present = 1,
                              .decoder_present = 1,
                              .name = "fails on read",
                              .can_apply = nullptr,
                              .set_local = nullptr,
                              .filter = fail_on_read};
    ASSERT_GE(H5Zregister(&filter), 0);
    FixtureLibrary fixture = make_valid_fixture();
    only_set(fixture).quantities.at("D2").units = "m";
    const auto diagnostics = rejected_edited(fixture, [](const H5::H5File& file) {
        const H5::Group d1 = test_set_group(file).openGroup("D1");
        d1.unlink("value");
        const std::array<hsize_t, 3> dims{2, 2, 2};
        const H5::DSetCreatPropList properties;
        properties.setChunk(3, dims.data());
        properties.setFilter(failing_filter_id, H5Z_FLAG_MANDATORY);
        const std::vector<double> ones(8, 1.0);
        d1.createDataSet("value", H5::PredType::IEEE_F64LE, H5::DataSpace(3, dims.data()),
                         properties)
            .write(ones.data(), H5::PredType::NATIVE_DOUBLE);
    });
    EXPECT_GE(H5Zunregister(failing_filter_id), 0);
    EXPECT_EQ(diagnostics.size(), 2U) << all_text(diagnostics);
    EXPECT_TRUE(has_diagnostic(diagnostics, set_path() + "/D1/value", "HDF5 cannot read it"));
    EXPECT_TRUE(has_location(diagnostics, set_path() + "/D2@units"));
}

// ----- Entry rules (contract §3.4, §4 item 5) -----------------------------------------------

// One breaking value or standard deviation in one component of one quantity, in the probe
// entry of the reflector: the entry is rejected while marked valid and accepted once marked
// invalid. Swept over every component (both groups are separate quantities; all eight delayed
// groups; all twelve face-group factors), over NaN, ±Inf and the sign rule, and over a fixture
// whose boron axis has one point.
struct BreakKind {
    std::string_view label;
    double (*make)(nts::EntryRule contract_rule);
    bool on_std_dev;
    std::string_view rule_text; // the broken rule the message names; empty: the sign rule
};

struct RuleCase {
    std::size_t row; // index into contract_quantities, in Quantity order
    std::size_t component;
    BreakKind kind;
    bool single_boron_point;
};

std::string rule_case_name(const RuleCase& rule_case) {
    return std::format("{}_{}_{}_{}", contract_quantities.at(rule_case.row).name,
                       rule_case.component, rule_case.kind.label,
                       rule_case.single_boron_point ? "one_boron_point" : "two_boron_points");
}

// GoogleTest finds a parameter's printer by this exact name.
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const RuleCase& rule_case, std::ostream* out) {
    *out << rule_case_name(rule_case);
}

class EntryRuleTest : public NodalLibraryTest, public testing::WithParamInterface<RuleCase> {};

// 0 breaks every > 0 rule; a negative value breaks every ≥ 0 rule (β and λ included).
double breaks_sign_rule(nts::EntryRule contract_rule) {
    return contract_rule == nts::EntryRule::positive ? 0.0 : -1.0e-6;
}
double negative(nts::EntryRule /*contract_rule*/) {
    return -1.0e-9;
}
double not_a_number(nts::EntryRule /*contract_rule*/) {
    return std::numeric_limits<double>::quiet_NaN();
}
double infinite(nts::EntryRule /*contract_rule*/) {
    return std::numeric_limits<double>::infinity();
}
double minus_infinite(nts::EntryRule /*contract_rule*/) {
    return -std::numeric_limits<double>::infinity();
}

constexpr std::array<BreakKind, 8> break_kinds{{
    {.label = "sign", .make = breaks_sign_rule, .on_std_dev = false, .rule_text = ""},
    {.label = "nan", .make = not_a_number, .on_std_dev = false, .rule_text = "is not finite"},
    {.label = "inf", .make = infinite, .on_std_dev = false, .rule_text = "is not finite"},
    {.label = "minus_inf",
     .make = minus_infinite,
     .on_std_dev = false,
     .rule_text = "is not finite"},
    {.label = "std_dev_negative", .make = negative, .on_std_dev = true, .rule_text = "is negative"},
    {.label = "std_dev_nan",
     .make = not_a_number,
     .on_std_dev = true,
     .rule_text = "is not finite"},
    {.label = "std_dev_inf", .make = infinite, .on_std_dev = true, .rule_text = "is not finite"},
    {.label = "std_dev_minus_inf",
     .make = minus_infinite,
     .on_std_dev = true,
     .rule_text = "is not finite"},
}};

TEST_P(EntryRuleTest, ValidEntryBreakingTheRuleIsRejectedInvalidOneIsKept) {
    const RuleCase& rule_case = GetParam();
    const ContractQuantity& row = contract_quantities.at(rule_case.row);
    const auto quantity = static_cast<Quantity>(rule_case.row);
    const std::string name(row.name);
    // Three compositions: in the one-boron-point fixture (two states) the probe entry 1·2 + 1
    // then differs from the composition-state swap (3 mod 3, 3 / 3).
    FixtureLibrary fixture =
        rule_case.single_boron_point ? make_valid_fixture(3, {975.0}) : make_valid_fixture(3);
    const std::size_t states = only_set(fixture).state_count();
    entry_value(only_set(fixture), name, reflector, probe_state, rule_case.component,
                rule_case.kind.on_std_dev) = rule_case.kind.make(row.rule);

    const auto diagnostics = rejected(fixture);
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().location,
              set_path() + "/" + name + (rule_case.kind.on_std_dev ? "/std_dev" : "/value"));
    std::string entry = rule_case.single_boron_point
                            ? "composition 1 'reflector', fuel_temperature[1] = 900 K, "
                              "boron[0] = 975 ppm"
                            : probe_text(reflector);
    const std::string component_text = contract_component_text(row.name, rule_case.component);
    if (!component_text.empty()) {
        entry += ", " + component_text;
    }
    EXPECT_EQ(diagnostics.front().entry, entry);
    std::string_view rule_text = rule_case.kind.rule_text;
    if (rule_text.empty()) {
        rule_text =
            row.rule == nts::EntryRule::positive ? "breaks the rule > 0" : "breaks the rule >= 0";
    }
    EXPECT_NE(diagnostics.front().message.find(rule_text), std::string::npos)
        << diagnostics.front().message;
    EXPECT_TRUE(diagnostics.front().message.ends_with("in an entry marked valid"))
        << diagnostics.front().message;

    only_set(fixture).valid[(reflector * states) + probe_state] = 0;
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto& set = library->composition_sets.front();
    auto expected_counts = std::array<std::size_t, nts::quantity_count>{};
    if (!rule_case.kind.on_std_dev) {
        expected_counts.at(rule_case.row) = 1;
    }
    EXPECT_EQ(nts::breaks_entry_rule(set, quantity, reflector, probe_state),
              !rule_case.kind.on_std_dev);
    EXPECT_EQ(nts::count_rule_violations(set), expected_counts);

    const std::array<std::size_t, 1> used{reflector};
    const auto unusable = nts::check_compositions_usable(*library, set, used);
    ASSERT_EQ(unusable.size(), 1U) << all_text(unusable);
    EXPECT_EQ(unusable.front().entry, rule_case.single_boron_point
                                          ? "composition 1 'reflector', fuel_temperature[1] = "
                                            "900 K, boron[0] = 975 ppm"
                                          : probe_text(reflector));
    const std::string broken = rule_case.kind.on_std_dev ? name + " std_dev" : name;
    EXPECT_TRUE(unusable.front().message.ends_with("rules broken: " + broken))
        << unusable.front().message;
}

std::vector<RuleCase> all_rule_cases() {
    std::vector<RuleCase> cases;
    for (const bool single_boron_point : {false, true}) {
        for (std::size_t row = 0; row < contract_quantities.size(); ++row) {
            for (std::size_t component = 0; component < contract_quantities.at(row).components;
                 ++component) {
                for (const BreakKind& kind : break_kinds) {
                    cases.push_back({.row = row,
                                     .component = component,
                                     .kind = kind,
                                     .single_boron_point = single_boron_point});
                }
            }
        }
    }
    return cases;
}

INSTANTIATE_TEST_SUITE_P(EveryComponent, EntryRuleTest, testing::ValuesIn(all_rule_cases()),
                         [](const testing::TestParamInfo<RuleCase>& case_info) {
                             return rule_case_name(case_info.param);
                         });

// λ_k = 0 is allowed exactly where the same group's β_k is 0, in every delayed group.
TEST_F(NodalLibraryTest, ZeroDecayConstantIsAllowedExactlyWhereItsOwnBetaIsZero) {
    for (std::size_t k = 0; k < nts::delayed_group_count; ++k) {
        SCOPED_TRACE(k);
        const std::size_t other = k == 0 ? 1 : 0;

        // β_k = λ_k = 0 in the fuel, every other β > 0: accepted.
        FixtureLibrary accepted = make_valid_fixture();
        entry_value(only_set(accepted), "beta", fuel, probe_state, k) = 0.0;
        entry_value(only_set(accepted), "lambda", fuel, probe_state, k) = 0.0;
        const auto library = read(accepted);
        EXPECT_TRUE(library.has_value()) << all_text(library.error());

        // λ_k = 0 with β_k > 0, another group's β = 0: rejected, naming group k.
        FixtureLibrary fixture = make_valid_fixture();
        entry_value(only_set(fixture), "beta", fuel, probe_state, other) = 0.0;
        entry_value(only_set(fixture), "lambda", fuel, probe_state, k) = 0.0;
        const auto diagnostics = rejected(fixture);
        expect_one(diagnostics, set_path() + "/lambda/value", "lambda 0 where beta = ");
        ASSERT_EQ(diagnostics.size(), 1U);
        EXPECT_EQ(diagnostics.front().entry,
                  std::format("{}, delayed group {}", probe_text(fuel), k));
    }
}

TEST_F(NodalLibraryTest, RejectsDelayedFractionsSummingToOneOrMore) {
    FixtureLibrary fixture = make_valid_fixture();
    for (std::size_t k = 0; k < nts::delayed_group_count; ++k) {
        entry_value(only_set(fixture), "beta", fuel, probe_state, k) = 0.125;
    }
    const auto diagnostics = rejected(fixture);
    expect_one(diagnostics, set_path() + "/beta/value", "delayed fractions sum to 1 or more");
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics.front().entry, probe_text(fuel));
}

// Components near the largest double: their sum would overflow, which traps in debug builds.
TEST_F(NodalLibraryTest, HugeDelayedFractionsBreakTheSumWithoutOverflow) {
    FixtureLibrary fixture = make_valid_fixture();
    entry_value(only_set(fixture), "beta", fuel, probe_state, 0) = 1.0e308;
    entry_value(only_set(fixture), "beta", fuel, probe_state, 1) = 1.0e308;
    const auto diagnostics = rejected(fixture);
    expect_one(diagnostics, set_path() + "/beta/value", "delayed fractions sum to");
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics.front().entry, probe_text(fuel));

    only_set(fixture).valid[(fuel * 4) + probe_state] = 0;
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto& set = library->composition_sets.front();
    EXPECT_TRUE(nts::breaks_entry_rule(set, Quantity::beta, fuel, probe_state));
    const std::array<std::size_t, 1> used{fuel};
    const auto unusable = nts::check_compositions_usable(*library, set, used);
    ASSERT_EQ(unusable.size(), 1U) << all_text(unusable);
    EXPECT_TRUE(unusable.front().message.ends_with("rules broken: beta"));

    FixtureLibrary negative_fixture = make_valid_fixture();
    entry_value(only_set(negative_fixture), "beta", fuel, probe_state, 0) = -1.0e308;
    entry_value(only_set(negative_fixture), "beta", fuel, probe_state, 1) = -1.0e308;
    const auto negative_diagnostics = rejected(negative_fixture);
    EXPECT_EQ(negative_diagnostics.size(), 2U) << all_text(negative_diagnostics);
    EXPECT_TRUE(std::ranges::all_of(negative_diagnostics, [](const auto& d) {
        return d.message.find("breaks the rule >= 0") != std::string::npos;
    })) << all_text(negative_diagnostics);
}

TEST_F(NodalLibraryTest, CapsEntryDiagnosticsPerDataset) {
    constexpr std::size_t compositions = 30;
    FixtureLibrary fixture = make_valid_fixture(compositions);
    for (double& d1 : only_set(fixture).quantities.at("D1").value) {
        d1 = -d1;
    }
    const auto diagnostics = rejected(fixture);
    ASSERT_EQ(diagnostics.size(), 21U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.back().location, set_path() + "/D1/value");
    EXPECT_EQ(diagnostics.back().message, "100 more diagnostics are not listed");
}

TEST_F(NodalLibraryTest, ChecksOnlyTheCompositionsInUse) {
    FixtureLibrary fixture = make_valid_fixture();
    entry_value(only_set(fixture), "D1", reflector, probe_state) = 0.0;
    entry_value(only_set(fixture), "Sigma_a1", reflector, probe_state) = 0.0;
    only_set(fixture).valid[(reflector * 4) + probe_state] = 0;
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto& set = library->composition_sets.front();

    const std::array<std::size_t, 1> fuel_only{fuel};
    EXPECT_TRUE(nts::check_compositions_usable(*library, set, fuel_only).empty());

    const std::array<std::size_t, 2> with_reflector{fuel, reflector};
    const auto diagnostics = nts::check_compositions_usable(*library, set, with_reflector);
    ASSERT_EQ(diagnostics.size(), 1U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.front().location, set_path() + "/valid");
    EXPECT_EQ(diagnostics.front().entry, probe_text(reflector));
    EXPECT_TRUE(diagnostics.front().message.ends_with("rules broken: D1, Sigma_a1"))
        << diagnostics.front().message;

    const std::array<std::size_t, 1> outside{2};
    expect_one(nts::check_compositions_usable(*library, set, outside), set_path() + "/names",
               "composition index 2 is outside the set of 2 compositions");
}

TEST_F(NodalLibraryTest, CapsInUseDiagnosticsPerDataset) {
    constexpr std::size_t compositions = 30;
    FixtureLibrary fixture = make_valid_fixture(compositions);
    std::vector<std::size_t> used;
    for (std::size_t c = 0; c < compositions; ++c) {
        entry_value(only_set(fixture), "D1", c, 0) = 0.0;
        only_set(fixture).valid[c * 4] = 0;
        used.push_back(c);
    }
    const auto library = read(fixture);
    ASSERT_TRUE(library.has_value()) << all_text(library.error());
    const auto diagnostics =
        nts::check_compositions_usable(*library, library->composition_sets.front(), used);
    ASSERT_EQ(diagnostics.size(), 21U) << all_text(diagnostics);
    EXPECT_EQ(diagnostics.back().location, set_path() + "/valid");
    EXPECT_EQ(diagnostics.back().message, "10 more diagnostics are not listed");
}

// ----- Descriptions and the quantity table --------------------------------------------------

TEST(NodalLibraryDescription, NamesTheComponent) {
    EXPECT_EQ(nts::describe_component(Quantity::d1, 0), "");
    EXPECT_EQ(nts::describe_component(Quantity::beta, 0), "delayed group 0");
    EXPECT_EQ(nts::describe_component(Quantity::lambda, 7), "delayed group 7");
    EXPECT_EQ(nts::describe_component(Quantity::discontinuity_factor, 0), "face x-, group 1");
    EXPECT_EQ(nts::describe_component(Quantity::discontinuity_factor, 1), "face x-, group 2");
    EXPECT_EQ(nts::describe_component(Quantity::discontinuity_factor, 2), "face x+, group 1");
    EXPECT_EQ(nts::describe_component(Quantity::discontinuity_factor, 11), "face z+, group 2");
}

TEST(NodalLibraryDescription, SpecsFollowTheContract) {
    for (std::size_t i = 0; i < nts::quantity_count; ++i) {
        const auto& spec = nts::quantity_specs()[i];
        const ContractQuantity& row = contract_quantities.at(i);
        SCOPED_TRACE(row.name);
        EXPECT_EQ(static_cast<std::size_t>(spec.quantity), i);
        EXPECT_EQ(spec.name, row.name);
        EXPECT_EQ(spec.units, row.units);
        EXPECT_EQ(spec.components, row.components);
        EXPECT_EQ(spec.required, row.required);
        EXPECT_EQ(spec.rule, row.rule);
        EXPECT_EQ(&nts::quantity_spec(spec.quantity), &spec);
    }
}

TEST(NodalLibraryDeathTest, BreaksEntryRuleAssertsTheEntryIsInTheSet) {
    nts::CompositionSet set;
    set.composition_names = {"only"};
    set.state_count = 1;
    EXPECT_DEATH(static_cast<void>(nts::breaks_entry_rule(set, Quantity::d1, 1, 0)),
                 "entry outside the composition set");
}

} // namespace
