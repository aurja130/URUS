// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/nodal_library.hpp"

#include "nts/common/diagnostic.hpp"
#include "nts/common/invariant.hpp"

#include <H5Cpp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace nts {
namespace {

constexpr QuantitySpec make_spec(Quantity quantity, std::string_view name, std::string_view units,
                                 std::size_t components, bool required, EntryRule rule) {
    return {.quantity = quantity,
            .name = name,
            .units = units,
            .components = components,
            .required = required,
            .rule = rule};
}

// Contract §3.3, in Quantity order.
constexpr std::array<QuantitySpec, quantity_count> specs{
    make_spec(Quantity::d1, "D1", "cm", 1, true, EntryRule::positive),
    make_spec(Quantity::d2, "D2", "cm", 1, true, EntryRule::positive),
    make_spec(Quantity::sigma_a1, "Sigma_a1", "cm^-1", 1, true, EntryRule::positive),
    make_spec(Quantity::sigma_a2, "Sigma_a2", "cm^-1", 1, true, EntryRule::positive),
    make_spec(Quantity::sigma_s12, "Sigma_s12", "cm^-1", 1, true, EntryRule::positive),
    make_spec(Quantity::sigma_s21, "Sigma_s21", "cm^-1", 1, true, EntryRule::non_negative),
    make_spec(Quantity::nu_sigma_f1, "nu_Sigma_f1", "cm^-1", 1, true, EntryRule::non_negative),
    make_spec(Quantity::nu_sigma_f2, "nu_Sigma_f2", "cm^-1", 1, true, EntryRule::non_negative),
    make_spec(Quantity::kappa_sigma_f1, "kappa_Sigma_f1", "J cm^-1", 1, true,
              EntryRule::non_negative),
    make_spec(Quantity::kappa_sigma_f2, "kappa_Sigma_f2", "J cm^-1", 1, true,
              EntryRule::non_negative),
    make_spec(Quantity::inv_v1, "inv_v1", "s cm^-1", 1, true, EntryRule::positive),
    make_spec(Quantity::inv_v2, "inv_v2", "s cm^-1", 1, true, EntryRule::positive),
    make_spec(Quantity::beta, "beta", "1", delayed_group_count, true, EntryRule::delayed_fraction),
    make_spec(Quantity::lambda, "lambda", "s^-1", delayed_group_count, true,
              EntryRule::decay_constant),
    make_spec(Quantity::sigma_f1, "Sigma_f1", "cm^-1", 1, false, EntryRule::non_negative),
    make_spec(Quantity::sigma_f2, "Sigma_f2", "cm^-1", 1, false, EntryRule::non_negative),
    make_spec(Quantity::discontinuity_factor, "discontinuity_factor", "1",
              face_count* energy_group_count, false, EntryRule::positive),
};

constexpr std::array<StateAxis, state_axis_count> axis_vocabulary{
    StateAxis::fuel_temperature, StateAxis::moderator_density, StateAxis::moderator_temperature,
    StateAxis::boron};

constexpr std::array<std::string_view, face_count> face_names{"x-", "x+", "y-", "y+", "z-", "z+"};

constexpr std::int64_t supported_format_version = 1;
constexpr std::int64_t supported_energy_groups = 2;
constexpr std::int64_t supported_delayed_groups = 8;
constexpr std::size_t max_entry_diagnostics = 20;
constexpr std::size_t float64_size = 8;

std::string join(std::span<const std::size_t> values) {
    std::string text = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        text += (i == 0 ? "" : ", ") + std::to_string(values[i]);
    }
    return text + "]";
}

// Collects the diagnostics of one file.
class Reporter {
public:
    explicit Reporter(std::string file) : file_(std::move(file)) {}

    void add(std::string location, std::string entry, std::string message) {
        diagnostics_.push_back(Diagnostic{.file = file_,
                                          .location = std::move(location),
                                          .entry = std::move(entry),
                                          .message = std::move(message)});
    }
    void add(std::string location, std::string message) {
        add(std::move(location), std::string{}, std::move(message));
    }
    [[nodiscard]] std::size_t count() const { return diagnostics_.size(); }
    [[nodiscard]] Diagnostics take() { return std::move(diagnostics_); }

private:
    std::string file_;
    Diagnostics diagnostics_;
};

// Runs `read`; an HDF5 failure becomes a diagnostic at `location`, so the file's other checks
// still run (contract §4). False on failure.
template <typename Read>
bool guarded_read(Reporter& reporter, const std::string& location, Read read) {
    try {
        read();
        return true;
    } catch (const H5::Exception& exception) {
        reporter.add(location, std::format("HDF5 cannot read it: {} failed: {}",
                                           exception.getFuncName(), exception.getDetailMsg()));
        return false;
    }
}

// Reports entry diagnostics of one dataset, at most max_entry_diagnostics of them, then the
// number left out (contract §4).
class EntryReporter {
public:
    EntryReporter(Reporter& reporter, std::string location)
        : reporter_(reporter), location_(std::move(location)) {}

    // `entry_text` builds the entry part; `message` is the text or a callable building it.
    // The callables run only for a listed diagnostic.
    template <typename EntryText, typename Message>
    void add(EntryText entry_text, Message message) {
        if (reported_ < max_entry_diagnostics) {
            if constexpr (std::is_invocable_v<Message>) {
                reporter_.add(location_, entry_text(), message());
            } else {
                reporter_.add(location_, entry_text(), std::string(std::move(message)));
            }
            ++reported_;
        } else {
            ++left_out_;
        }
    }
    void finish() {
        if (left_out_ > 0) {
            reporter_.add(location_, std::format("{} more diagnostics are not listed", left_out_));
        }
    }

private:
    Reporter& reporter_;
    std::string location_;
    std::size_t reported_ = 0;
    std::size_t left_out_ = 0;
};

// ----- HDF5 access -------------------------------------------------------------------------

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

// A fixed-length string up to its first NUL, without the trailing blanks of a space-padded
// type (contract §1).
std::string fixed_length_text(std::string_view raw, H5T_str_t padding) {
    raw = raw.substr(0, raw.find('\0'));
    if (padding == H5T_STR_SPACEPAD) {
        raw = raw.substr(0, raw.find_last_not_of(' ') + 1);
    }
    return std::string(raw);
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

// Checks that `object` carries `units` = `expected`.
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

// ----- Root ---------------------------------------------------------------------------------

// Reads the root attributes (contract §2). True when the version and group counts allow the
// sets to be read.
bool read_root_attributes(Reporter& reporter, const H5::H5File& file, NodalLibrary& library) {
    const std::string root = "/";
    const auto version = read_integer_attribute(reporter, file, root, "format_version");
    if (version && *version != supported_format_version) {
        reporter.add("/@format_version",
                     std::format("format version {} is not supported; this engine reads version {}",
                                 *version, supported_format_version));
    }
    const auto energy_groups = read_integer_attribute(reporter, file, root, "energy_groups");
    if (energy_groups && *energy_groups != supported_energy_groups) {
        reporter.add("/@energy_groups",
                     std::format("{} energy groups; only 2 energy groups and 8 delayed groups "
                                 "are supported",
                                 *energy_groups));
    }
    const auto delayed_groups = read_integer_attribute(reporter, file, root, "delayed_groups");
    if (delayed_groups && *delayed_groups != supported_delayed_groups) {
        reporter.add("/@delayed_groups",
                     std::format("{} delayed groups; only 2 energy groups and 8 delayed groups "
                                 "are supported",
                                 *delayed_groups));
    }
    const auto boundary = read_float_attribute(reporter, file, root, "group_boundary_eV");
    if (boundary && (!std::isfinite(*boundary) || *boundary <= 0.0)) {
        reporter.add("/@group_boundary_eV",
                     std::format("group boundary {} eV is not finite and positive", *boundary));
    }
    for (const char* name : {"generator", "generator_commit"}) {
        const auto text = read_string_attribute(reporter, file, root, name);
        if (text && text->empty()) {
            reporter.add(std::format("/@{}", name), "attribute is empty");
        }
        std::string& field =
            std::string_view(name) == "generator" ? library.generator : library.generator_commit;
        field = text.value_or("");
    }
    library.format_version = version.value_or(0);
    library.group_boundary_ev = boundary.value_or(0.0);
    return version == supported_format_version && energy_groups == supported_energy_groups &&
           delayed_groups == supported_delayed_groups;
}

// ----- Axes ---------------------------------------------------------------------------------

bool axis_point_in_bounds(StateAxis axis, double point) {
    return axis == StateAxis::boron ? point >= 0.0 : point > 0.0;
}

// Points: finite, within bounds, strictly ascending (contract §3.1).
bool check_axis_points(Reporter& reporter, const std::string& location, StateAxis axis,
                       std::span<const double> points) {
    bool ok = true;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!std::isfinite(points[i])) {
            reporter.add(location, std::format("point [{}] = {} is not finite", i, points[i]));
            ok = false;
        } else if (!axis_point_in_bounds(axis, points[i])) {
            reporter.add(location, std::format("point [{}] = {} {} is outside the bound {} 0", i,
                                               points[i], state_axis_units(axis),
                                               axis == StateAxis::boron ? ">=" : ">"));
            ok = false;
        }
    }
    if (!ok) {
        return false;
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (!(points[i - 1] < points[i])) {
            reporter.add(location,
                         std::format("points are not strictly ascending: [{}] = {} then [{}] = {}",
                                     i - 1, points[i - 1], i, points[i]));
            return false;
        }
    }
    return true;
}

std::optional<AxisPoints> read_axis(Reporter& reporter, const H5::Group& axes,
                                    const std::string& axes_path, StateAxis axis) {
    const std::string name{state_axis_name(axis)};
    const std::string location = axes_path + "/" + name;
    if (axes.childObjType(name) != H5O_TYPE_DATASET) {
        reporter.add(location, "axis is not a dataset");
        return std::nullopt;
    }
    const H5::DataSet dataset = axes.openDataSet(name);
    const bool units_ok = check_units(reporter, dataset, location, state_axis_units(axis));
    const auto dims = dimensions(dataset);
    if (dims.size() != 1 || !is_float64(dataset)) {
        reporter.add(location,
                     std::format("axis must be a 1-D float64 dataset; found shape {}", join(dims)));
        return std::nullopt;
    }
    if (dims[0] == 0) {
        reporter.add(location, "axis has no points; it needs at least one");
        return std::nullopt;
    }
    AxisPoints result{.axis = axis, .points = {}};
    if (!guarded_read(reporter, location, [&] { result.points = read_doubles(dataset); })) {
        return std::nullopt;
    }
    if (!check_axis_points(reporter, location, axis, result.points) || !units_ok) {
        return std::nullopt;
    }
    return result;
}

// Reads the axes of a set in vocabulary order. False on any problem.
bool read_axes(Reporter& reporter, const H5::Group& set_group, const std::string& set_path,
               CompositionSet& set) {
    if (!set_group.nameExists("axes")) {
        return true; // a set without axes has one branch state
    }
    const std::string axes_path = set_path + "/axes";
    if (set_group.childObjType("axes") != H5O_TYPE_GROUP) {
        reporter.add(axes_path, "'axes' is not a group");
        return false;
    }
    const H5::Group axes = set_group.openGroup("axes");
    bool ok = true;
    for (hsize_t i = 0; i < axes.getNumObjs(); ++i) {
        const std::string member = axes.getObjnameByIdx(i);
        const bool known = std::ranges::any_of(
            axis_vocabulary, [&](StateAxis axis) { return state_axis_name(axis) == member; });
        if (!known) {
            reporter.add(std::format("{}/{}", axes_path, member),
                         std::format("unknown state axis '{}'; the vocabulary is "
                                     "fuel_temperature, moderator_density, "
                                     "moderator_temperature, boron",
                                     member));
            ok = false;
        }
    }
    for (const StateAxis axis : axis_vocabulary) {
        if (!axes.nameExists(std::string(state_axis_name(axis)))) {
            continue;
        }
        auto points = read_axis(reporter, axes, axes_path, axis);
        if (!points) {
            ok = false;
            continue;
        }
        set.state_count *= points->points.size();
        set.axes.push_back(std::move(*points));
    }
    return ok;
}

// ----- Names --------------------------------------------------------------------------------

bool read_names(Reporter& reporter, const H5::Group& set_group, const std::string& set_path,
                CompositionSet& set) {
    const std::string location = set_path + "/names";
    if (!is_child(set_group, "names", H5O_TYPE_DATASET)) {
        reporter.add(location, "the composition names dataset is missing");
        return false;
    }
    const H5::DataSet dataset = set_group.openDataSet("names");
    const auto dims = dimensions(dataset);
    if (dataset.getTypeClass() != H5T_STRING || dims.size() != 1) {
        reporter.add(location, "composition names must be a 1-D string dataset");
        return false;
    }
    if (dims[0] == 0) {
        reporter.add(location, "the set holds no composition");
        return false;
    }
    if (!guarded_read(reporter, location, [&] { set.composition_names = read_strings(dataset); })) {
        return false;
    }
    bool ok = true;
    std::map<std::string_view, std::size_t> first_index;
    for (std::size_t c = 0; c < set.composition_names.size(); ++c) {
        const std::string& name = set.composition_names[c];
        if (name.empty()) {
            reporter.add(location, std::format("composition {} has an empty name", c));
            ok = false;
            continue;
        }
        const auto [it, inserted] = first_index.emplace(name, c);
        if (!inserted) {
            reporter.add(location, std::format("composition name '{}' is used by compositions "
                                               "{} and {}",
                                               name, it->second, c));
            ok = false;
        }
    }
    return ok;
}

// ----- Quantities and validity flags --------------------------------------------------------

std::vector<std::size_t> extra_extents(Quantity quantity) {
    switch (quantity) {
    case Quantity::beta:
    case Quantity::lambda:
        return {delayed_group_count};
    case Quantity::discontinuity_factor:
        return {face_count, energy_group_count};
    default:
        return {};
    }
}

// [N, axis lengths...], or nullopt when the names or axes could not be read.
using Shape = std::optional<std::vector<std::size_t>>;

std::optional<H5::DataSet> open_float_table(Reporter& reporter, const H5::Group& group,
                                            const std::string& group_path, const std::string& name,
                                            const Shape& expected) {
    const std::string location = group_path + "/" + name;
    if (!is_child(group, name, H5O_TYPE_DATASET)) {
        reporter.add(location, "dataset is missing");
        return std::nullopt;
    }
    H5::DataSet dataset = group.openDataSet(name);
    if (!is_float64(dataset)) {
        reporter.add(location, "dataset is not float64");
        return std::nullopt;
    }
    const auto dims = dimensions(dataset);
    if (expected && dims != *expected) {
        reporter.add(location,
                     std::format("shape {} is not the expected {}", join(dims), join(*expected)));
        return std::nullopt;
    }
    return dataset;
}

// Reads one quantity group. True when it is present and well formed, or absent and optional.
bool read_quantity(Reporter& reporter, const H5::Group& set_group, const std::string& set_path,
                   const QuantitySpec& spec, const Shape& leading, CompositionSet& set) {
    const std::string name{spec.name};
    const std::string location = set_path + "/" + name;
    if (!set_group.nameExists(name)) {
        if (spec.required) {
            reporter.add(location, std::format("required quantity {} is missing", name));
        }
        return !spec.required;
    }
    if (set_group.childObjType(name) != H5O_TYPE_GROUP) {
        reporter.add(location, "quantity is not a group");
        return false;
    }
    const H5::Group group = set_group.openGroup(name);
    const bool units_ok = check_units(reporter, group, location, spec.units);
    Shape expected;
    if (leading) {
        expected = *leading;
        std::ranges::copy(extra_extents(spec.quantity), std::back_inserter(*expected));
    }
    const auto value = open_float_table(reporter, group, location, "value", expected);
    const auto std_dev = open_float_table(reporter, group, location, "std_dev", expected);
    if (!units_ok || !value || !std_dev || !leading) {
        return false;
    }
    QuantityTable table;
    const bool value_read =
        guarded_read(reporter, location + "/value", [&] { table.value = read_doubles(*value); });
    const bool std_dev_read = guarded_read(reporter, location + "/std_dev",
                                           [&] { table.std_dev = read_doubles(*std_dev); });
    if (!value_read || !std_dev_read) {
        return false;
    }
    set.quantities.at(static_cast<std::size_t>(spec.quantity)) = std::move(table);
    return true;
}

// Reads the flags and reports any other than 0 and 1; returns whether the flags were read.
bool read_valid(Reporter& reporter, const H5::Group& set_group, const std::string& set_path,
                const Shape& leading, CompositionSet& set) {
    const std::string location = set_path + "/valid";
    if (!is_child(set_group, "valid", H5O_TYPE_DATASET)) {
        reporter.add(location, "the validity flags are missing");
        return false;
    }
    const H5::DataSet dataset = set_group.openDataSet("valid");
    if (dataset.getTypeClass() != H5T_INTEGER || dataset.getIntType().getSize() != 1 ||
        dataset.getIntType().getSign() != H5T_SGN_NONE) {
        reporter.add(location, "validity flags are not uint8");
        return false;
    }
    const auto dims = dimensions(dataset);
    if (leading && dims != *leading) {
        reporter.add(location,
                     std::format("shape {} is not the expected {}", join(dims), join(*leading)));
        return false;
    }
    if (!leading) {
        return false;
    }
    set.valid.resize(static_cast<std::size_t>(dataset.getSpace().getSimpleExtentNpoints()));
    if (!guarded_read(reporter, location,
                      [&] { dataset.read(set.valid.data(), H5::PredType::NATIVE_UINT8); })) {
        set.valid.clear();
        return false;
    }
    EntryReporter entries(reporter, location);
    for (std::size_t i = 0; i < set.valid.size(); ++i) {
        if (set.valid[i] > 1) {
            entries.add(
                [&] { return describe_entry(set, i / set.state_count, i % set.state_count); },
                std::format("validity flag {} is neither 0 nor 1", set.valid[i]));
        }
    }
    entries.finish();
    return true;
}

// ----- Entry rules --------------------------------------------------------------------------

std::span<const double> entry_span(const std::vector<double>& data, const QuantitySpec& spec,
                                   std::size_t entry) {
    return std::span<const double>(data).subspan(entry * spec.components, spec.components);
}

// The broken rule of one component's value, or nullopt. `beta` is the entry's delayed
// fractions (empty when absent); only the decay-constant rule reads it.
std::optional<std::string> component_rule_message(const QuantitySpec& spec,
                                                  std::span<const double> values,
                                                  std::span<const double> beta,
                                                  std::size_t component) {
    const double x = values[component];
    if (!std::isfinite(x)) {
        return std::format("value {} is not finite", x);
    }
    if (spec.rule == EntryRule::positive) {
        return x > 0.0 ? std::nullopt
                       : std::optional(std::format("value {} breaks the rule > 0", x));
    }
    if (x < 0.0) {
        return std::format("value {} breaks the rule >= 0", x);
    }
    if (spec.rule == EntryRule::decay_constant && x == 0.0 && !beta.empty() &&
        std::isfinite(beta[component]) && beta[component] > 0.0) {
        return std::format("lambda 0 where beta = {} (rule: lambda > 0 where beta > 0)",
                           beta[component]);
    }
    return std::nullopt;
}

// The broken sum rule of the delayed fractions, or nullopt (also when a component is not
// finite or negative: that component breaks its own rule). The sum stops once it reaches 1,
// so it cannot overflow (any partial sum < 1 plus a finite component is at most DBL_MAX).
std::optional<std::string> delayed_sum_message(std::span<const double> beta) {
    if (!std::ranges::all_of(beta, [](double b) { return std::isfinite(b) && b >= 0.0; })) {
        return std::nullopt;
    }
    double sum = 0.0;
    for (const double b : beta) {
        sum += b;
        if (sum >= 1.0) {
            return std::format("delayed fractions sum to {} or more (rule: sum < 1)", sum);
        }
    }
    return std::nullopt;
}

std::optional<std::string> std_dev_message(double sigma) {
    if (!std::isfinite(sigma)) {
        return std::format("std_dev {} is not finite", sigma);
    }
    if (sigma < 0.0) {
        return std::format("std_dev {} is negative", sigma);
    }
    return std::nullopt;
}

std::span<const double> entry_beta(const CompositionSet& set, std::size_t entry) {
    const QuantityTable* beta = set.find(Quantity::beta);
    if (beta == nullptr) {
        return {};
    }
    return entry_span(beta->value, quantity_spec(Quantity::beta), entry);
}

std::string entry_text(const CompositionSet& set, const QuantitySpec& spec, std::size_t entry,
                       std::size_t component) {
    std::string text = describe_entry(set, entry / set.state_count, entry % set.state_count);
    const std::string component_text = describe_component(spec.quantity, component);
    if (!component_text.empty()) {
        text += ", " + component_text;
    }
    return text;
}

void check_entry_values(const CompositionSet& set, const QuantitySpec& spec,
                        const QuantityTable& table, std::size_t entry, EntryReporter& values) {
    const auto value = entry_span(table.value, spec, entry);
    const auto beta = entry_beta(set, entry);
    for (std::size_t k = 0; k < spec.components; ++k) {
        if (auto message = component_rule_message(spec, value, beta, k)) {
            values.add([&] { return entry_text(set, spec, entry, k); },
                       *message + " in an entry marked valid");
        }
    }
    if (spec.rule == EntryRule::delayed_fraction) {
        if (auto message = delayed_sum_message(value)) {
            values.add(
                [&] {
                    return describe_entry(set, entry / set.state_count, entry % set.state_count);
                },
                *message + " in an entry marked valid");
        }
    }
}

void check_entry_std_devs(const CompositionSet& set, const QuantitySpec& spec,
                          const QuantityTable& table, std::size_t entry, EntryReporter& std_devs) {
    const auto sigma = entry_span(table.std_dev, spec, entry);
    for (std::size_t k = 0; k < spec.components; ++k) {
        if (auto message = std_dev_message(sigma[k])) {
            std_devs.add([&] { return entry_text(set, spec, entry, k); },
                         *message + " in an entry marked valid");
        }
    }
}

// Contract §4 item 5: every entry marked valid satisfies §3.4.
void check_valid_entries(Reporter& reporter, const std::string& set_path,
                         const CompositionSet& set) {
    for (const QuantitySpec& spec : specs) {
        const QuantityTable* table = set.find(spec.quantity);
        if (table == nullptr) {
            continue;
        }
        const std::string location = set_path + "/" + std::string(spec.name);
        EntryReporter values(reporter, location + "/value");
        EntryReporter std_devs(reporter, location + "/std_dev");
        for (std::size_t entry = 0; entry < set.valid.size(); ++entry) {
            if (set.valid[entry] == 1) {
                check_entry_values(set, spec, *table, entry, values);
                check_entry_std_devs(set, spec, *table, entry, std_devs);
            }
        }
        values.finish();
        std_devs.finish();
    }
}

// ----- Sets ---------------------------------------------------------------------------------

std::optional<CompositionSet> read_set(Reporter& reporter, const H5::Group& sets,
                                       const std::string& name) {
    const std::string set_path = "/composition_sets/" + name;
    const H5::Group set_group = sets.openGroup(name);
    CompositionSet set;
    set.name = name;
    const std::size_t problems_before = reporter.count();
    const bool axes_ok = read_axes(reporter, set_group, set_path, set);
    const bool names_ok = read_names(reporter, set_group, set_path, set);
    Shape leading;
    if (axes_ok && names_ok) {
        leading = std::vector<std::size_t>{set.composition_count()};
        for (const AxisPoints& axis : set.axes) {
            leading->push_back(axis.points.size());
        }
    }
    for (const QuantitySpec& spec : specs) {
        static_cast<void>(read_quantity(reporter, set_group, set_path, spec, leading, set));
    }
    const bool flags_read = read_valid(reporter, set_group, set_path, leading, set);
    // Item 5 runs whenever the flags were read, flags other than 0 and 1 included, so a
    // structural problem elsewhere in the set does not hide the entry-rule diagnostics; it
    // reads only the tables that were read whole and the entries whose flag is 1.
    if (flags_read) {
        check_valid_entries(reporter, set_path, set);
    }
    if (reporter.count() != problems_before) {
        return std::nullopt;
    }
    return set;
}

void read_sets(Reporter& reporter, const H5::H5File& file, NodalLibrary& library) {
    if (!is_child(file, "composition_sets", H5O_TYPE_GROUP)) {
        reporter.add("/composition_sets", "the group of composition sets is missing");
        return;
    }
    const H5::Group sets = file.openGroup("composition_sets");
    std::vector<std::string> names;
    for (hsize_t i = 0; i < sets.getNumObjs(); ++i) {
        std::string member = sets.getObjnameByIdx(i);
        if (sets.childObjType(member) == H5O_TYPE_GROUP) {
            names.push_back(std::move(member));
        }
    }
    if (names.empty()) {
        reporter.add("/composition_sets", "holds no composition set");
        return;
    }
    std::ranges::sort(names);
    for (const std::string& name : names) {
        if (auto set = read_set(reporter, sets, name)) {
            library.composition_sets.push_back(std::move(*set));
        }
    }
}

bool breaks_std_dev_rule(const CompositionSet& set, const QuantitySpec& spec, std::size_t entry) {
    const QuantityTable* table = set.find(spec.quantity);
    if (table == nullptr) {
        return false;
    }
    return std::ranges::any_of(entry_span(table->std_dev, spec, entry),
                               [](double sigma) { return std_dev_message(sigma).has_value(); });
}

// Names each quantity whose value breaks its rule in the entry, then each whose standard
// deviation does ("Sigma_a2 std_dev"); "none" for an entry marked invalid that breaks no rule.
std::string invalid_entry_message(const CompositionSet& set, std::size_t composition,
                                  std::size_t state) {
    const std::size_t entry = (composition * set.state_count) + state;
    std::string broken;
    for (const QuantitySpec& spec : specs) {
        if (breaks_entry_rule(set, spec.quantity, composition, state)) {
            broken += (broken.empty() ? "" : ", ") + std::string(spec.name);
        }
    }
    for (const QuantitySpec& spec : specs) {
        if (breaks_std_dev_rule(set, spec, entry)) {
            broken += (broken.empty() ? "" : ", ") + std::string(spec.name) + " std_dev";
        }
    }
    return "entry is invalid (valid = 0) in a composition the problem uses; rules broken: " +
           (broken.empty() ? std::string("none") : broken);
}

} // namespace

std::string_view state_axis_name(StateAxis axis) {
    switch (axis) {
    case StateAxis::fuel_temperature:
        return "fuel_temperature";
    case StateAxis::moderator_density:
        return "moderator_density";
    case StateAxis::moderator_temperature:
        return "moderator_temperature";
    case StateAxis::boron:
        return "boron";
    }
    NTS_ASSERT(false, "unknown state axis");
}

std::string_view state_axis_units(StateAxis axis) {
    switch (axis) {
    case StateAxis::fuel_temperature:
    case StateAxis::moderator_temperature:
        return "K";
    case StateAxis::moderator_density:
        return "kg m^-3";
    case StateAxis::boron:
        return "ppm";
    }
    NTS_ASSERT(false, "unknown state axis");
}

std::span<const QuantitySpec, quantity_count> quantity_specs() {
    return specs;
}

const QuantitySpec& quantity_spec(Quantity quantity) {
    return specs.at(static_cast<std::size_t>(quantity));
}

std::size_t CompositionSet::composition_count() const {
    return composition_names.size();
}

const QuantityTable* CompositionSet::find(Quantity quantity) const {
    const auto& table = quantities.at(static_cast<std::size_t>(quantity));
    return table ? &*table : nullptr;
}

std::expected<NodalLibrary, Diagnostics> read_nodal_library(const std::filesystem::path& file) {
    Reporter reporter(file.string());
    H5::Exception::dontPrint();
    try {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error)) {
            reporter.add("/", "the file does not exist or is not a regular file");
            return std::unexpected(reporter.take());
        }
        if (!H5::H5File::isHdf5(file.string())) {
            reporter.add("/", "the file is not an HDF5 file");
            return std::unexpected(reporter.take());
        }
        const H5::H5File h5(file.string(), H5F_ACC_RDONLY);
        NodalLibrary library;
        library.file = file;
        if (read_root_attributes(reporter, h5, library)) {
            read_sets(reporter, h5, library);
        }
        if (reporter.count() > 0) {
            return std::unexpected(reporter.take());
        }
        return library;
    } catch (const H5::Exception& exception) {
        reporter.add("/", std::format("HDF5 error in {}: {}", exception.getFuncName(),
                                      exception.getDetailMsg()));
        return std::unexpected(reporter.take());
    }
}

std::string describe_entry(const CompositionSet& set, std::size_t composition, std::size_t state) {
    NTS_ASSERT(composition < set.composition_count() && state < set.state_count,
               "entry outside the composition set");
    std::string text =
        std::format("composition {} '{}'", composition, set.composition_names[composition]);
    std::vector<std::size_t> index(set.axes.size());
    std::size_t rest = state;
    for (std::size_t a = set.axes.size(); a-- > 0;) {
        index[a] = rest % set.axes[a].points.size();
        rest /= set.axes[a].points.size();
    }
    for (std::size_t a = 0; a < set.axes.size(); ++a) {
        const AxisPoints& axis = set.axes[a];
        text += std::format(", {}[{}] = {} {}", state_axis_name(axis.axis), index[a],
                            axis.points[index[a]], state_axis_units(axis.axis));
    }
    return text;
}

std::string describe_component(Quantity quantity, std::size_t component) {
    const QuantitySpec& spec = quantity_spec(quantity);
    NTS_ASSERT(component < spec.components, "component outside the quantity");
    if (quantity == Quantity::beta || quantity == Quantity::lambda) {
        return std::format("delayed group {}", component);
    }
    if (quantity == Quantity::discontinuity_factor) {
        return std::format("face {}, group {}", face_names.at(component / energy_group_count),
                           (component % energy_group_count) + 1);
    }
    return {};
}

bool breaks_entry_rule(const CompositionSet& set, Quantity quantity, std::size_t composition,
                       std::size_t state) {
    NTS_ASSERT(composition < set.composition_count() && state < set.state_count,
               "entry outside the composition set");
    const QuantityTable* table = set.find(quantity);
    if (table == nullptr) {
        return false;
    }
    const QuantitySpec& spec = quantity_spec(quantity);
    const std::size_t entry = (composition * set.state_count) + state;
    const auto value = entry_span(table->value, spec, entry);
    const auto beta = entry_beta(set, entry);
    for (std::size_t k = 0; k < spec.components; ++k) {
        if (component_rule_message(spec, value, beta, k)) {
            return true;
        }
    }
    return spec.rule == EntryRule::delayed_fraction && delayed_sum_message(value).has_value();
}

std::array<std::size_t, quantity_count> count_rule_violations(const CompositionSet& set) {
    std::array<std::size_t, quantity_count> counts{};
    for (const QuantitySpec& spec : specs) {
        for (std::size_t c = 0; c < set.composition_count(); ++c) {
            for (std::size_t s = 0; s < set.state_count; ++s) {
                if (breaks_entry_rule(set, spec.quantity, c, s)) {
                    ++counts.at(static_cast<std::size_t>(spec.quantity));
                }
            }
        }
    }
    return counts;
}

Diagnostics check_compositions_usable(const NodalLibrary& library, const CompositionSet& set,
                                      std::span<const std::size_t> compositions) {
    Reporter reporter(library.file.string());
    const std::string set_path = "/composition_sets/" + set.name;
    EntryReporter entries(reporter, set_path + "/valid");
    for (const std::size_t c : compositions) {
        if (c >= set.composition_count()) {
            reporter.add(set_path + "/names",
                         std::format("composition index {} is outside the set of {} compositions",
                                     c, set.composition_count()));
            continue;
        }
        for (std::size_t s = 0; s < set.state_count; ++s) {
            if (set.valid[(c * set.state_count) + s] == 0) {
                entries.add([&] { return describe_entry(set, c, s); },
                            [&] { return invalid_entry_message(set, c, s); });
            }
        }
    }
    entries.finish();
    return reporter.take();
}

} // namespace nts
