// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// A small nodal library held in memory and written to HDF5, for the reader tests. Every field
// can be changed or removed to build a defective file.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nts::test {

struct FixtureAxis {
    std::string name;
    std::string units;
    std::vector<double> points;
};

// Layout of value and std_dev: ((composition * states + state) * components + component).
struct FixtureQuantity {
    std::string units;
    std::vector<std::size_t> shape;
    std::size_t components = 1;
    std::vector<double> value;
    std::vector<double> std_dev;
};

struct FixtureSet {
    std::string name;
    std::vector<FixtureAxis> axes; // written in this order
    std::vector<std::string> names;
    bool fixed_length_names = false;
    bool space_padded_names = false; // with fixed_length_names: blank- instead of NUL-padded
    std::map<std::string, FixtureQuantity> quantities; // by group name
    std::vector<std::uint8_t> valid;
    std::vector<std::size_t> valid_shape;
    std::vector<std::string> extra_groups; // members a later format version might add

    [[nodiscard]] std::size_t state_count() const;
};

struct FixtureLibrary {
    std::optional<std::int64_t> format_version = 1;
    std::optional<std::int64_t> energy_groups = 2;
    std::optional<std::int64_t> delayed_groups = 8;
    std::optional<double> group_boundary_ev = 0.625;
    std::optional<std::string> generator = "library_fixture";
    std::optional<std::string> generator_commit = "0123456789abcdef";
    std::vector<FixtureSet> sets;
    std::vector<std::string> extra_root_groups;
};

// One set "test_set": compositions "fuel" (fissile) and "reflector" (no fission, beta = 0),
// then "filler_<c>" copies of "fuel" up to `compositions`; axes fuel_temperature [600, 900] K
// and boron `boron_points` ppm; every quantity of the contract, valid everywhere.
[[nodiscard]] FixtureLibrary make_valid_fixture(std::size_t compositions = 2,
                                                std::vector<double> boron_points = {0.0, 1000.0});

// The same set on the given axes, listed in the contract's vocabulary order.
[[nodiscard]] FixtureLibrary make_valid_fixture_on_axes(std::vector<FixtureAxis> axes,
                                                        std::size_t compositions = 2);

// The value (or, with `std_dev`, the standard deviation) of one entry component.
[[nodiscard]] double& entry_value(FixtureSet& set, const std::string& quantity,
                                  std::size_t composition, std::size_t state,
                                  std::size_t component = 0, bool std_dev = false);

void write_fixture(const FixtureLibrary& library, const std::filesystem::path& file);

// A fresh path in the temporary directory, unique per test name.
[[nodiscard]] std::filesystem::path fixture_path(std::string_view test_name);

} // namespace nts::test
