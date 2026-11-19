// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "problem_fixture.hpp"

#include <H5Cpp.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace nts::test {

namespace {

// ----- Writing ----------------------------------------------------------------------------------

H5::DataSpace dataspace(const std::vector<std::size_t>& shape) {
    if (shape.empty()) {
        return H5::DataSpace{H5S_SCALAR};
    }
    std::vector<hsize_t> dims;
    dims.reserve(shape.size());
    for (const std::size_t extent : shape) {
        dims.push_back(static_cast<hsize_t>(extent));
    }
    return H5::DataSpace{static_cast<int>(dims.size()), dims.data()};
}

std::size_t element_count(const std::vector<std::size_t>& shape) {
    std::size_t count = 1;
    for (const std::size_t extent : shape) {
        count *= extent;
    }
    return count;
}

// A dataset must hold exactly the values its shape needs; anything else is a construction error.
void check_count(std::size_t given, const std::vector<std::size_t>& shape,
                 const std::string& name) {
    const std::size_t expected = element_count(shape);
    if (given != expected) {
        throw std::invalid_argument(std::format(
            "problem fixture: '{}' holds {} values for {} elements", name, given, expected));
    }
}

H5::StrType variable_string_type() {
    const H5::StrType type(H5::PredType::C_S1, H5T_VARIABLE);
    type.setCset(H5T_CSET_UTF8);
    return type;
}

H5::StrType fixed_string_type(std::size_t width) {
    const H5::StrType type(H5::PredType::C_S1, width);
    type.setCset(H5T_CSET_UTF8);
    type.setStrpad(H5T_STR_NULLPAD);
    return type;
}

void write_attribute(const H5::H5Object& object, const std::string& name,
                     const FixtureAttribute& value) {
    const H5::DataSpace scalar(H5S_SCALAR);
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        object.createAttribute(name, H5::PredType::STD_I64LE, scalar)
            .write(H5::PredType::NATIVE_INT64, integer);
    } else if (const auto* number = std::get_if<double>(&value)) {
        object.createAttribute(name, H5::PredType::IEEE_F64LE, scalar)
            .write(H5::PredType::NATIVE_DOUBLE, number);
    } else {
        const H5::StrType type = variable_string_type();
        object.createAttribute(name, type, scalar).write(type, std::get<std::string>(value));
    }
}

void write_attributes(const H5::H5Object& object,
                      const std::map<std::string, FixtureAttribute>& attributes) {
    for (const auto& [name, value] : attributes) {
        write_attribute(object, name, value);
    }
}

// `value` as a T. An integer type takes only whole values in range; a floating-point type takes
// any value that does not overflow (NaN and the infinities are stored as they are).
template <typename T> T representable(double value) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isfinite(value) &&
            std::fabs(value) > static_cast<double>(std::numeric_limits<T>::max())) {
            throw std::out_of_range("problem fixture: value overflows the dataset type");
        }
    } else {
        // One past the largest T; exact as a double for every integer type used here.
        const double above = static_cast<double>(std::numeric_limits<T>::max()) + 1.0;
        if (!std::isfinite(value) || value != std::floor(value) ||
            value < static_cast<double>(std::numeric_limits<T>::min()) || value >= above) {
            throw std::out_of_range("problem fixture: value is not an integer of the type");
        }
    }
    if constexpr (std::is_same_v<T, std::uint64_t>) {
        // Clang converts a double to uint64 through the signed conversion, which raises the
        // trapped invalid-operation exception at 2^63 and above; convert the part below 2^63.
        constexpr double half = 9223372036854775808.0; // 2^63
        if (value >= half) {
            return static_cast<T>(static_cast<std::int64_t>(value - half)) + (T{1} << 63U);
        }
    }
    return static_cast<T>(value);
}

template <typename T>
H5::DataSet write_numbers(const H5::Group& parent, const std::string& name,
                          const H5::DataType& file_type, const FixtureDataset& dataset,
                          const H5::DataType& memory_type) {
    check_count(dataset.numbers.size(), dataset.shape, name);
    std::vector<T> values;
    values.reserve(dataset.numbers.size());
    for (const double number : dataset.numbers) {
        values.push_back(representable<T>(number));
    }
    const H5::DataSet h5 = parent.createDataSet(name, file_type, dataspace(dataset.shape));
    if (!values.empty()) {
        h5.write(values.data(), memory_type);
    }
    return h5;
}

H5::DataSet write_strings(const H5::Group& parent, const std::string& name,
                          const FixtureDataset& dataset) {
    check_count(dataset.strings.size(), dataset.shape, name);
    const H5::DataSpace space = dataspace(dataset.shape);
    if (dataset.type == FixtureType::string) {
        const H5::StrType type = variable_string_type();
        std::vector<const char*> pointers;
        pointers.reserve(dataset.strings.size());
        for (const std::string& text : dataset.strings) {
            pointers.push_back(text.c_str());
        }
        const H5::DataSet h5 = parent.createDataSet(name, type, space);
        if (!pointers.empty()) {
            h5.write(static_cast<const void*>(pointers.data()), type);
        }
        return h5;
    }
    // Fixed-length: as wide as the longest string, each value NUL-padded.
    std::size_t width = 1;
    for (const std::string& text : dataset.strings) {
        width = std::max(width, text.size());
    }
    const H5::StrType type = fixed_string_type(width);
    std::vector<char> buffer(dataset.strings.size() * width, '\0');
    for (std::size_t i = 0; i < dataset.strings.size(); ++i) {
        std::ranges::copy(dataset.strings[i],
                          buffer.begin() + static_cast<std::ptrdiff_t>(i * width));
    }
    const H5::DataSet h5 = parent.createDataSet(name, type, space);
    if (!buffer.empty()) {
        h5.write(buffer.data(), type);
    }
    return h5;
}

H5::DataSet create_dataset(const H5::Group& parent, const std::string& name,
                           const FixtureDataset& dataset) {
    switch (dataset.type) {
    case FixtureType::float64:
        return write_numbers<double>(parent, name, H5::PredType::IEEE_F64LE, dataset,
                                     H5::PredType::NATIVE_DOUBLE);
    case FixtureType::float32:
        return write_numbers<float>(parent, name, H5::PredType::IEEE_F32LE, dataset,
                                    H5::PredType::NATIVE_FLOAT);
    case FixtureType::int32:
        return write_numbers<std::int32_t>(parent, name, H5::PredType::STD_I32LE, dataset,
                                           H5::PredType::NATIVE_INT32);
    case FixtureType::int64:
        return write_numbers<std::int64_t>(parent, name, H5::PredType::STD_I64LE, dataset,
                                           H5::PredType::NATIVE_INT64);
    case FixtureType::uint64:
        return write_numbers<std::uint64_t>(parent, name, H5::PredType::STD_U64LE, dataset,
                                            H5::PredType::NATIVE_UINT64);
    case FixtureType::uint8:
        return write_numbers<std::uint8_t>(parent, name, H5::PredType::STD_U8LE, dataset,
                                           H5::PredType::NATIVE_UINT8);
    case FixtureType::string:
    case FixtureType::fixed_string:
        return write_strings(parent, name, dataset);
    }
    throw std::logic_error("problem fixture: unknown dataset type");
}

void write_dataset(const H5::Group& parent, const std::string& name,
                   const FixtureDataset& dataset) {
    write_attributes(create_dataset(parent, name, dataset), dataset.attributes);
}

// Writes the tree without recursion: a stack holds each group still to write, with its handle.
void write_tree(const H5::Group& root_group, const FixtureGroup& root) {
    struct Pending {
        H5::Group group;
        const FixtureGroup* fixture;
    };
    std::vector<Pending> pending;
    pending.push_back({.group = root_group, .fixture = &root});
    while (!pending.empty()) {
        const Pending current = std::move(pending.back());
        pending.pop_back();
        write_attributes(current.group, current.fixture->attributes);
        for (const auto& [name, child] : current.fixture->groups) {
            pending.push_back({.group = current.group.createGroup(name), .fixture = &child});
        }
        for (const auto& [name, dataset] : current.fixture->datasets) {
            write_dataset(current.group, name, dataset);
        }
    }
}

// A boundary-condition group of the given kind (contract §4).
FixtureGroup kind_group(const std::string& kind) {
    FixtureGroup group;
    group.attributes["kind"] = kind;
    return group;
}

} // namespace

FixtureDataset float64_scalar(double value, const std::string& units) {
    return float64_array({value}, {}, units);
}

FixtureDataset float64_array(std::vector<double> values, std::vector<std::size_t> shape,
                             const std::string& units) {
    FixtureDataset dataset;
    dataset.type = FixtureType::float64;
    dataset.shape = std::move(shape);
    dataset.numbers = std::move(values);
    dataset.attributes["units"] = units;
    return dataset;
}

FixtureDataset integer_scalar(std::int64_t value) {
    FixtureDataset dataset;
    dataset.type = FixtureType::int64;
    dataset.numbers = {static_cast<double>(value)}; // exact for |value| <= 2^53
    return dataset;
}

FixtureDataset string_scalar(const std::string& value) {
    FixtureDataset dataset;
    dataset.type = FixtureType::string;
    dataset.strings = {value};
    return dataset;
}

FixtureDataset string_array(std::vector<std::string> values) {
    FixtureDataset dataset;
    dataset.type = FixtureType::string;
    dataset.shape = {values.size()};
    dataset.strings = std::move(values);
    return dataset;
}

FixtureGroup make_valid_problem(const std::string& library_path) {
    FixtureGroup root;
    root.attributes["format_version"] = std::int64_t{1};

    FixtureGroup& geometry = root.groups["geometry"];
    geometry.datasets["width_x"] = float64_array({10.0, 21.5, 10.0}, {3}, "cm");
    geometry.datasets["width_y"] = float64_array({15.0, 15.0}, {2}, "cm");
    geometry.datasets["width_z"] = float64_array({20.0, 30.0}, {2}, "cm");
    FixtureDataset composition_map;
    composition_map.type = FixtureType::int32;
    composition_map.shape = {2, 2, 3};
    composition_map.numbers = {-1.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    geometry.datasets["composition_map"] = composition_map;

    FixtureGroup& boundaries = root.groups["boundary_conditions"];
    boundaries.groups["x_minus"] = kind_group("vacuum");
    boundaries.groups["x_plus"] = kind_group("reflective");
    boundaries.groups["y_minus"] = kind_group("zero_flux");
    FixtureGroup albedo = kind_group("albedo");
    albedo.datasets["albedo"] = float64_array({0.3, 0.6}, {2}, "1");
    boundaries.groups["y_plus"] = std::move(albedo);
    boundaries.groups["z_minus"] = kind_group("vacuum");
    boundaries.groups["z_plus"] = kind_group("reflective");
    boundaries.groups["exterior"] = kind_group("vacuum");

    root.groups["library"].attributes["path"] = library_path;

    FixtureGroup& operating_state = root.groups["operating_state"];
    operating_state.datasets["thermal_power"] = float64_scalar(1.0e6, "W");
    operating_state.datasets["doppler_temperature"] = float64_scalar(700.0, "K");
    operating_state.datasets["moderator_density"] = float64_scalar(700.0, "kg m^-3");
    operating_state.datasets["moderator_temperature"] = float64_scalar(560.0, "K");
    operating_state.datasets["boron"] = float64_scalar(500.0, "ppm");

    FixtureGroup& compositions = root.groups["compositions"];
    compositions.datasets["library_set"] = string_array({"test_set", "test_set"});
    compositions.datasets["library_composition"] = string_array({"fuel", "reflector"});
    return root;
}

void write_problem(const FixtureGroup& root, const std::filesystem::path& file) {
    H5::Exception::dontPrint();
    const H5::H5File h5(file.string(), H5F_ACC_TRUNC);
    // Slicing the H5File into an H5::Group would copy an invalid identifier: open "/" instead.
    write_tree(h5.openGroup("/"), root);
}

} // namespace nts::test
