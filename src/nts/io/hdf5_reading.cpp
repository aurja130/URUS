// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/hdf5_reading.hpp"

#include <H5Cpp.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nts::detail {

namespace {

// A fixed-length string up to its first NUL, without the trailing blanks of a space-padded
// type.
std::string fixed_length_text(std::string_view raw, H5T_str_t padding) {
    raw = raw.substr(0, raw.find('\0'));
    if (padding == H5T_STR_SPACEPAD) {
        raw = raw.substr(0, raw.find_last_not_of(' ') + 1);
    }
    return std::string(raw);
}

// An attribute holding one value of the given class, or a diagnostic.
std::optional<H5::Attribute> scalar_attribute(Reporter& reporter, const H5::H5Object& object,
                                              const std::string& object_path,
                                              const std::string& name, H5T_class_t type_class,
                                              std::string_view type_name) {
    const std::string location = object_path + "@" + name;
    if (!object.attrExists(name)) {
        reporter.add(location, std::format("attribute is missing; expected {}", type_name));
        return std::nullopt;
    }
    H5::Attribute attribute = object.openAttribute(name);
    if (attribute.getTypeClass() != type_class) {
        reporter.add(location, std::format("attribute is not {}", type_name));
        return std::nullopt;
    }
    if (attribute.getSpace().getSimpleExtentNpoints() != 1) {
        reporter.add(location, "attribute does not hold exactly one value");
        return std::nullopt;
    }
    return attribute;
}

} // namespace

std::string join(std::span<const std::size_t> values) {
    std::string text = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        text += (i == 0 ? "" : ", ") + std::to_string(values[i]);
    }
    return text + "]";
}

bool is_child(const H5::Group& group, const std::string& name, H5O_type_t type) {
    return group.nameExists(name) && group.childObjType(name) == type;
}

std::vector<std::size_t> dimensions(const H5::DataSet& dataset) {
    const H5::DataSpace space = dataset.getSpace();
    const int rank = space.getSimpleExtentNdims();
    std::vector<hsize_t> dims(static_cast<std::size_t>(rank));
    if (rank > 0) {
        space.getSimpleExtentDims(dims.data());
    }
    return {dims.begin(), dims.end()};
}

bool is_float64(const H5::DataSet& dataset) {
    return dataset.getTypeClass() == H5T_FLOAT && dataset.getDataType().getSize() == float64_size;
}

std::vector<double> read_doubles(const H5::DataSet& dataset) {
    std::vector<double> data(static_cast<std::size_t>(dataset.getSpace().getSimpleExtentNpoints()));
    if (!data.empty()) {
        dataset.read(data.data(), H5::PredType::NATIVE_DOUBLE);
    }
    return data;
}

std::vector<std::string> read_strings(const H5::DataSet& dataset) {
    const H5::StrType type = dataset.getStrType();
    const H5::DataSpace space = dataset.getSpace();
    const auto count = static_cast<std::size_t>(space.getSimpleExtentNpoints());
    std::vector<std::string> strings;
    strings.reserve(count);
    if (count == 0) {
        return strings;
    }
    if (type.isVariableStr()) {
        std::vector<char*> buffer(count, nullptr);
        dataset.read(static_cast<void*>(buffer.data()), type);
        for (const char* text : buffer) {
            strings.emplace_back(text == nullptr ? "" : text);
        }
        H5Treclaim(type.getId(), space.getId(), H5P_DEFAULT, static_cast<void*>(buffer.data()));
        return strings;
    }
    const std::size_t width = type.getSize();
    const H5T_str_t padding = type.getStrpad();
    std::vector<char> buffer(count * width, '\0');
    dataset.read(static_cast<void*>(buffer.data()), type);
    for (std::size_t i = 0; i < count; ++i) {
        strings.push_back(fixed_length_text(std::string_view(&buffer[i * width], width), padding));
    }
    return strings;
}

std::optional<std::int64_t> read_integer_attribute(Reporter& reporter, const H5::H5Object& object,
                                                   const std::string& object_path,
                                                   const std::string& name) {
    const auto attribute =
        scalar_attribute(reporter, object, object_path, name, H5T_INTEGER, "an integer");
    std::int64_t value = 0;
    if (!attribute || !guarded_read(reporter, object_path + "@" + name,
                                    [&] { attribute->read(H5::PredType::NATIVE_INT64, &value); })) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> read_float_attribute(Reporter& reporter, const H5::H5Object& object,
                                           const std::string& object_path,
                                           const std::string& name) {
    const std::string location = object_path + "@" + name;
    const auto attribute =
        scalar_attribute(reporter, object, object_path, name, H5T_FLOAT, "float64");
    if (!attribute) {
        return std::nullopt;
    }
    if (attribute->getDataType().getSize() != float64_size) {
        reporter.add(location, "attribute is not float64");
        return std::nullopt;
    }
    double value = 0.0;
    if (!guarded_read(reporter, location,
                      [&] { attribute->read(H5::PredType::NATIVE_DOUBLE, &value); })) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> read_string_attribute(Reporter& reporter, const H5::H5Object& object,
                                                 const std::string& object_path,
                                                 const std::string& name) {
    const auto attribute =
        scalar_attribute(reporter, object, object_path, name, H5T_STRING, "a string");
    H5std_string value;
    if (!attribute || !guarded_read(reporter, object_path + "@" + name,
                                    [&] { attribute->read(attribute->getStrType(), value); })) {
        return std::nullopt;
    }
    const H5::StrType type = attribute->getStrType();
    return type.isVariableStr() ? value.substr(0, value.find('\0'))
                                : fixed_length_text(value, type.getStrpad());
}

bool check_units(Reporter& reporter, const H5::H5Object& object, const std::string& object_path,
                 std::string_view expected) {
    const auto units = read_string_attribute(reporter, object, object_path, "units");
    if (!units) {
        return false;
    }
    if (*units != expected) {
        reporter.add(object_path + "@units",
                     std::format("units '{}' are not the accepted '{}'", *units, expected));
        return false;
    }
    return true;
}

std::optional<std::vector<std::int64_t>> read_integers(const H5::DataSet& dataset) {
    const H5::IntType type(dataset);
    const auto count = static_cast<std::size_t>(dataset.getSpace().getSimpleExtentNpoints());
    if (type.getSign() == H5T_SGN_NONE && type.getSize() == sizeof(std::uint64_t)) {
        std::vector<std::uint64_t> raw(count);
        if (!raw.empty()) {
            dataset.read(raw.data(), H5::PredType::NATIVE_UINT64);
        }
        std::vector<std::int64_t> values;
        values.reserve(count);
        for (const std::uint64_t value : raw) {
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            values.push_back(static_cast<std::int64_t>(value));
        }
        return values;
    }
    std::vector<std::int64_t> values(count);
    if (!values.empty()) {
        dataset.read(values.data(), H5::PredType::NATIVE_INT64);
    }
    return values;
}

std::vector<std::string> member_names(const H5::Group& group) {
    const hsize_t count = group.getNumObjs();
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(count));
    for (hsize_t index = 0; index < count; ++index) {
        names.push_back(group.getObjnameByIdx(index));
    }
    return names;
}

} // namespace nts::detail
