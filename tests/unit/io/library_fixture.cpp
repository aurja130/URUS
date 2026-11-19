// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "library_fixture.hpp"

#include <H5Cpp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace nts::test {
namespace {

struct BaseQuantity {
    std::string name;
    std::string units;
    std::vector<double> fuel;
    std::vector<double> reflector;
};

constexpr std::size_t delayed_groups = 8;
constexpr std::size_t factor_components = 12;
constexpr double relative_std_dev = 0.01;
constexpr double state_step = 0.01;

BaseQuantity base(std::string name, std::string units, std::vector<double> fuel,
                  std::vector<double> reflector) {
    return {.name = std::move(name),
            .units = std::move(units),
            .fuel = std::move(fuel),
            .reflector = std::move(reflector)};
}

// Representative two-group values (LWR-like magnitudes); the reflector does not fission.
std::vector<BaseQuantity> base_values() {
    const std::vector<double> beta{0.00021, 0.00105, 0.00063, 0.00133,
                                   0.00227, 0.00076, 0.00063, 0.00018};
    const std::vector<double> lambda{0.0125, 0.0283, 0.0425, 0.133, 0.292, 0.666, 1.635, 3.555};
    const std::vector<double> factors(factor_components, 1.0);
    return {
        base("D1", "cm", {1.4}, {1.2}),
        base("D2", "cm", {0.4}, {0.25}),
        base("Sigma_a1", "cm^-1", {0.009}, {0.001}),
        base("Sigma_a2", "cm^-1", {0.08}, {0.03}),
        base("Sigma_s12", "cm^-1", {0.017}, {0.03}),
        base("Sigma_s21", "cm^-1", {0.0015}, {0.002}),
        base("nu_Sigma_f1", "cm^-1", {0.0065}, {0.0}),
        base("nu_Sigma_f2", "cm^-1", {0.12}, {0.0}),
        base("kappa_Sigma_f1", "J cm^-1", {8.5e-14}, {0.0}),
        base("kappa_Sigma_f2", "J cm^-1", {1.6e-12}, {0.0}),
        base("inv_v1", "s cm^-1", {5.0e-8}, {5.0e-8}),
        base("inv_v2", "s cm^-1", {2.5e-6}, {2.5e-6}),
        base("beta", "1", beta, std::vector<double>(delayed_groups, 0.0)),
        base("lambda", "s^-1", lambda, lambda),
        base("Sigma_f1", "cm^-1", {0.0026}, {0.0}),
        base("Sigma_f2", "cm^-1", {0.05}, {0.0}),
        base("discontinuity_factor", "1", factors, factors),
    };
}

std::vector<std::size_t> leading_shape(const FixtureSet& set) {
    std::vector<std::size_t> shape{set.names.size()};
    for (const FixtureAxis& axis : set.axes) {
        shape.push_back(axis.points.size());
    }
    return shape;
}

std::vector<hsize_t> to_hsize(const std::vector<std::size_t>& shape) {
    return {shape.begin(), shape.end()};
}

void write_string_attribute(const H5::H5Object& object, const std::string& name,
                            const std::string& text) {
    const H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    object.createAttribute(name, type, H5::DataSpace(H5S_SCALAR)).write(type, text);
}

void write_doubles(const H5::Group& group, const std::string& name,
                   const std::vector<std::size_t>& shape, const std::vector<double>& data) {
    const auto dims = to_hsize(shape);
    const H5::DataSpace space(static_cast<int>(dims.size()), dims.data());
    group.createDataSet(name, H5::PredType::IEEE_F64LE, space)
        .write(data.data(), H5::PredType::NATIVE_DOUBLE);
}

void write_names(const H5::Group& group, const FixtureSet& set) {
    const std::array<hsize_t, 1> dims{set.names.size()};
    const H5::DataSpace space(1, dims.data());
    if (!set.fixed_length_names) {
        const H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
        std::vector<const char*> pointers;
        std::ranges::transform(set.names, std::back_inserter(pointers),
                               [](const std::string& name) { return name.c_str(); });
        group.createDataSet("names", type, space)
            .write(static_cast<const void*>(pointers.data()), type);
        return;
    }
    std::size_t width = 1;
    for (const std::string& name : set.names) {
        width = std::max(width, name.size());
    }
    const H5::StrType type(H5::PredType::C_S1, width);
    type.setStrpad(set.space_padded_names ? H5T_STR_SPACEPAD : H5T_STR_NULLPAD);
    std::vector<char> buffer(set.names.size() * width, set.space_padded_names ? ' ' : '\0');
    for (std::size_t i = 0; i < set.names.size(); ++i) {
        std::ranges::copy(set.names[i], buffer.begin() + static_cast<std::ptrdiff_t>(i * width));
    }
    group.createDataSet("names", type, space).write(buffer.data(), type);
}

void write_set(const H5::Group& sets, const FixtureSet& set) {
    const H5::Group group = sets.createGroup(set.name);
    if (!set.axes.empty()) {
        const H5::Group axes = group.createGroup("axes");
        for (const FixtureAxis& axis : set.axes) {
            write_doubles(axes, axis.name, {axis.points.size()}, axis.points);
            write_string_attribute(axes.openDataSet(axis.name), "units", axis.units);
        }
    }
    write_names(group, set);
    for (const auto& [name, quantity] : set.quantities) {
        const H5::Group quantity_group = group.createGroup(name);
        write_string_attribute(quantity_group, "units", quantity.units);
        write_doubles(quantity_group, "value", quantity.shape, quantity.value);
        write_doubles(quantity_group, "std_dev", quantity.shape, quantity.std_dev);
    }
    const auto dims = to_hsize(set.valid_shape);
    const H5::DataSpace space(static_cast<int>(dims.size()), dims.data());
    group.createDataSet("valid", H5::PredType::STD_U8LE, space)
        .write(set.valid.data(), H5::PredType::NATIVE_UINT8);
    for (const std::string& extra : set.extra_groups) {
        static_cast<void>(group.createGroup(extra));
    }
}

} // namespace

std::size_t FixtureSet::state_count() const {
    std::size_t count = 1;
    for (const FixtureAxis& axis : axes) {
        count *= axis.points.size();
    }
    return count;
}

FixtureLibrary make_valid_fixture(std::size_t compositions, std::vector<double> boron_points) {
    return make_valid_fixture_on_axes(
        {FixtureAxis{.name = "fuel_temperature", .units = "K", .points = {600.0, 900.0}},
         FixtureAxis{.name = "boron", .units = "ppm", .points = std::move(boron_points)}},
        compositions);
}

FixtureLibrary make_valid_fixture_on_axes(std::vector<FixtureAxis> axes, std::size_t compositions) {
    FixtureSet set;
    set.name = "test_set";
    set.axes = std::move(axes);
    set.names = {"fuel", "reflector"};
    for (std::size_t c = set.names.size(); c < compositions; ++c) {
        set.names.push_back(std::format("filler_{}", c));
    }
    const std::size_t states = set.state_count();
    const auto leading = leading_shape(set);
    for (const BaseQuantity& values : base_values()) {
        const std::string& name = values.name;
        FixtureQuantity quantity;
        quantity.units = values.units;
        quantity.components = values.fuel.size();
        quantity.shape = leading;
        if (name == "beta" || name == "lambda") {
            quantity.shape.push_back(delayed_groups);
        } else if (name == "discontinuity_factor") {
            quantity.shape.insert(quantity.shape.end(), {6, 2});
        }
        for (std::size_t c = 0; c < set.names.size(); ++c) {
            const auto& composition_values = c == 1 ? values.reflector : values.fuel;
            for (std::size_t s = 0; s < states; ++s) {
                const double scale = 1.0 + (state_step * static_cast<double>(s));
                for (const double v : composition_values) {
                    quantity.value.push_back(v * scale);
                    quantity.std_dev.push_back(v * scale * relative_std_dev);
                }
            }
        }
        set.quantities.emplace(name, std::move(quantity));
    }
    set.valid.assign(set.names.size() * states, 1);
    set.valid_shape = leading;
    FixtureLibrary library;
    library.sets.push_back(std::move(set));
    return library;
}

double& entry_value(FixtureSet& set, const std::string& quantity, std::size_t composition,
                    std::size_t state, std::size_t component, bool std_dev) {
    FixtureQuantity& table = set.quantities.at(quantity);
    const std::size_t index =
        (((composition * set.state_count()) + state) * table.components) + component;
    return std_dev ? table.std_dev.at(index) : table.value.at(index);
}

void write_fixture(const FixtureLibrary& library, const std::filesystem::path& file) {
    const H5::H5File h5(file.string(), H5F_ACC_TRUNC);
    const H5::DataSpace scalar(H5S_SCALAR);
    const std::vector<std::pair<const char*, std::optional<std::int64_t>>> integers{
        {"format_version", library.format_version},
        {"energy_groups", library.energy_groups},
        {"delayed_groups", library.delayed_groups}};
    for (const auto& [name, value] : integers) {
        if (value) {
            h5.createAttribute(name, H5::PredType::STD_I64LE, scalar)
                .write(H5::PredType::NATIVE_INT64, &*value);
        }
    }
    if (library.group_boundary_ev) {
        h5.createAttribute("group_boundary_eV", H5::PredType::IEEE_F64LE, scalar)
            .write(H5::PredType::NATIVE_DOUBLE, &*library.group_boundary_ev);
    }
    if (library.generator) {
        write_string_attribute(h5, "generator", *library.generator);
    }
    if (library.generator_commit) {
        write_string_attribute(h5, "generator_commit", *library.generator_commit);
    }
    const H5::Group sets = h5.createGroup("composition_sets");
    for (const FixtureSet& set : library.sets) {
        write_set(sets, set);
    }
    for (const std::string& extra : library.extra_root_groups) {
        static_cast<void>(h5.createGroup(extra));
    }
}

std::filesystem::path fixture_path(std::string_view test_name) {
    std::string file_name = std::format("nts_library_fixture_{}_{}.h5", test_name, getpid());
    std::ranges::replace(file_name, '/', '_'); // parameterized test names hold '/'
    return std::filesystem::temp_directory_path() / file_name;
}

} // namespace nts::test
