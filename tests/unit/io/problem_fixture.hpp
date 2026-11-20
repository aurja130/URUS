// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// A problem input held in memory as a tree of HDF5 groups, datasets and attributes, and
// written to a file, for the problem input tests. Any member can be changed, retyped,
// reshaped or removed to build a defective file (docs/contracts/problem_input.md).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace nts::test {

enum class FixtureType : std::uint8_t {
    float64,
    float32,
    int32,
    int64,
    uint64,
    uint8,
    string,      // variable-length UTF-8
    fixed_string // fixed-length, NUL-padded, as wide as the longest string
};

// A scalar attribute.
using FixtureAttribute = std::variant<std::int64_t, double, std::string>;

struct FixtureDataset {
    FixtureType type = FixtureType::float64;
    std::vector<std::size_t> shape;   // empty: a scalar dataset (rank 0)
    std::vector<double> numbers;      // numeric types; converted to `type` when written
    std::vector<std::string> strings; // string types
    std::map<std::string, FixtureAttribute> attributes;
};

// An HDF5 external link: the object `object` of the file `file` (as written; a relative name
// resolves against the directory of the file holding the link when the link is read).
struct FixtureExternalLink {
    std::string file;
    std::string object = "/";
};

struct FixtureGroup {
    std::map<std::string, FixtureGroup> groups;
    std::map<std::string, FixtureDataset> datasets;
    std::map<std::string, FixtureExternalLink> external_links;
    std::map<std::string, FixtureAttribute> attributes;
};

// Builders of single datasets; float64 ones carry the `units` attribute.
[[nodiscard]] FixtureDataset float64_scalar(double value, const std::string& units);
[[nodiscard]] FixtureDataset
float64_array(std::vector<double> values, std::vector<std::size_t> shape, const std::string& units);
[[nodiscard]] FixtureDataset integer_scalar(std::int64_t value);
[[nodiscard]] FixtureDataset string_scalar(const std::string& value);
[[nodiscard]] FixtureDataset string_array(std::vector<std::string> values);

// A valid version 2 problem input bound to the library of make_valid_fixture() (set "test_set"),
// with the reactor and the initial state written in place and no settings or control signals:
// - (reactor) geometry nx = 3 (widths 10, 21.5, 10 cm), ny = 2 (15, 15 cm), nz = 2 (20, 30 cm);
// - input compositions 0 = ("test_set", "fuel"), 1 = ("test_set", "reflector");
// - composition_map int32 [2][2][3]: 0 everywhere except position (0, 0, z) = -1 for both
//   z (excluded) and position (2, 1, 1) = 1; so 10 nodes;
// - bounding faces x_minus vacuum, x_plus reflective, y_minus zero_flux, y_plus albedo
//   [0.3, 0.6], z_minus vacuum, z_plus reflective; exterior vacuum;
// - thermal_power 1e6 W; scalar node states doppler_temperature 700 K, moderator_density
//   700 kg m^-3, moderator_temperature 560 K, boron 500 ppm;
// - no settings or control_signals group (the tests add them);
// - /library@path = `library_path` as given.
[[nodiscard]] FixtureGroup make_valid_problem(const std::string& library_path);

// Writes the tree; the root group's attributes become root attributes.
void write_problem(const FixtureGroup& root, const std::filesystem::path& file);

} // namespace nts::test
