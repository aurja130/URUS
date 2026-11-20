// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/problem.hpp"

#include "nts/common/diagnostic.hpp"
#include "nts/common/invariant.hpp"
#include "nts/io/hdf5_reading.hpp"
#include "nts/io/nodal_library.hpp"

#include <H5Cpp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace nts {

std::string_view face_name(Face face) {
    switch (face) {
    case Face::x_minus:
        return "x_minus";
    case Face::x_plus:
        return "x_plus";
    case Face::y_minus:
        return "y_minus";
    case Face::y_plus:
        return "y_plus";
    case Face::z_minus:
        return "z_minus";
    case Face::z_plus:
        return "z_plus";
    }
    std::unreachable();
}

std::string_view boundary_kind_name(BoundaryKind kind) {
    switch (kind) {
    case BoundaryKind::reflective:
        return "reflective";
    case BoundaryKind::vacuum:
        return "vacuum";
    case BoundaryKind::zero_flux:
        return "zero_flux";
    case BoundaryKind::albedo:
        return "albedo";
    }
    std::unreachable();
}

std::string_view kernel_choice_name(KernelChoice kernel) {
    switch (kernel) {
    case KernelChoice::finite_difference:
        return "finite_difference";
    case KernelChoice::nem:
        return "nem";
    case KernelChoice::anm:
        return "anm";
    case KernelChoice::hybrid:
        return "hybrid";
    }
    std::unreachable();
}

namespace {

using detail::check_units;
using detail::dimensions;
using detail::EntryReporter;
using detail::guarded_read;
using detail::is_child;
using detail::is_float64;
using detail::join;
using detail::member_names;
using detail::read_doubles;
using detail::read_integer_attribute;
using detail::read_integers;
using detail::read_string_attribute;
using detail::read_strings;
using detail::Reporter;

constexpr std::int64_t supported_format_version = 2;
constexpr std::int64_t excluded_position = -1; // a composition_map value: no node there
constexpr std::size_t node_field_count = 4;    // doppler, density, temperature, boron

constexpr std::string_view unit_cm = "cm";
constexpr std::string_view unit_dimensionless = "1";
constexpr std::string_view unit_watt = "W";
constexpr std::string_view unit_kelvin = "K";
constexpr std::string_view unit_kg_per_m3 = "kg m^-3";
constexpr std::string_view unit_ppm = "ppm";

constexpr std::array<std::string_view, input_document_count> document_names{
    "reactor", "initial_state", "control_signals", "settings"};
constexpr std::array<bool, input_document_count> document_required{true, true, false, false};
constexpr std::array<std::string_view, 4> reactor_members{"geometry", "boundary_conditions",
                                                          "library", "compositions"};
constexpr std::array<std::string_view, 4> geometry_members{"width_x", "width_y", "width_z",
                                                           "composition_map"};
constexpr std::array<std::string_view, face_count + 1> boundary_members{
    "x_minus", "x_plus", "y_minus", "y_plus", "z_minus", "z_plus", "exterior"};
constexpr std::array<std::string_view, 1> albedo_members{"albedo"};
constexpr std::array<std::string_view, 2> composition_members{"library_set", "library_composition"};
constexpr std::array<std::string_view, 5> initial_state_members{
    "thermal_power", "doppler_temperature", "moderator_density", "moderator_temperature", "boron"};
constexpr std::array<std::string_view, 13> settings_members{"kernel",
                                                            "kernel_switch_threshold",
                                                            "wielandt_shift",
                                                            "eigenvalue_tolerance",
                                                            "fission_source_l2_tolerance",
                                                            "fission_source_linf_tolerance",
                                                            "inner_reduction",
                                                            "inner_iteration_limit",
                                                            "outer_iteration_limit",
                                                            "nodal_update_period",
                                                            "coupling_relaxation",
                                                            "adjoint",
                                                            "allow_table_clamps"};
// The members each document may hold (§2); the control signals document holds none.
constexpr std::array<std::span<const std::string_view>, input_document_count> document_members{
    reactor_members, initial_state_members, std::span<const std::string_view>{}, settings_members};
constexpr std::int64_t minimum_iteration_count = 1;

constexpr std::size_t index_of(InputDocument document) {
    return static_cast<std::size_t>(document);
}

// The location of member `name` of the group at `path`; the root's path is empty.
std::string child_location(const std::string& path, std::string_view name) {
    return std::format("{}/{}", path, name);
}

// The object an attribute of a document root belongs to: "/" for the root of a document file.
std::string root_object_path(const std::string& root_path) {
    return root_path.empty() ? "/" : root_path;
}

// Reports every member of `group` that `allowed` does not name. An external link is reported
// unless `documents_may_link` holds and it names one of `allowed` (§2: only the four documents of
// the problem input may be linked).
void check_members(Reporter& reporter, const H5::Group& group, const std::string& path,
                   std::span<const std::string_view> allowed, bool documents_may_link = false) {
    std::vector<std::string> names;
    if (!guarded_read(reporter, path.empty() ? "/" : path, [&] { names = member_names(group); })) {
        return;
    }
    for (const std::string& name : names) {
        const std::string location = child_location(path, name);
        const bool allowed_name = std::ranges::find(allowed, name) != allowed.end();
        if (detail::is_external_link(group, name)) {
            if (!documents_may_link || !allowed_name) {
                reporter.add(location, "an external link; only the four documents of the problem "
                                       "input may be linked");
            }
            continue;
        }
        if (!allowed_name) {
            reporter.add(location, "not a member of the problem input contract");
        }
    }
}

// The group `name` of `parent` (at `path`), or a diagnostic when it is missing or not a group. An
// external link is not followed and not reported here: check_members of the parent reports it.
std::optional<H5::Group> open_group(Reporter& reporter, const H5::Group& parent,
                                    const std::string& path, const std::string& name) {
    const std::string location = child_location(path, name);
    std::optional<H5::Group> group;
    guarded_read(reporter, location, [&] {
        if (!parent.nameExists(name)) {
            reporter.add(location, "group is missing");
            return;
        }
        if (detail::is_external_link(parent, name)) {
            return;
        }
        if (!is_child(parent, name, H5O_TYPE_GROUP)) {
            reporter.add(location, "not a group");
            return;
        }
        group.emplace(parent.openGroup(name));
    });
    return group;
}

// The dataset `name` of `group` (at `path`), or a diagnostic when it is missing or not a dataset.
// An external link is not followed and not reported here: check_members of the group reports it.
std::optional<H5::DataSet> open_dataset(Reporter& reporter, const H5::Group& group,
                                        const std::string& path, const std::string& name) {
    const std::string location = child_location(path, name);
    std::optional<H5::DataSet> dataset;
    guarded_read(reporter, location, [&] {
        if (!group.nameExists(name)) {
            reporter.add(location, "dataset is missing");
            return;
        }
        if (detail::is_external_link(group, name)) {
            return;
        }
        if (!is_child(group, name, H5O_TYPE_DATASET)) {
            reporter.add(location, "not a dataset");
            return;
        }
        dataset.emplace(group.openDataSet(name));
    });
    return dataset;
}

// A document of the problem input (§2) as found: its root group, the location of that root in
// the problem input ("/<name>" in place, empty for the root of a document file), and the file
// holding it. `root` is nullopt when the document is absent or is not read further.
struct Document {
    std::optional<H5::Group> root;
    std::string root_path;
    std::filesystem::path file;
};

// The root group of the document file that `link` names (§2). A relative file name resolves
// against the directory of the problem input; an absolute one is used as written. The opened file
// is kept in `linked_file` so that the root stays valid.
Document open_linked_document(Reporter& reporter, const std::filesystem::path& input_file,
                              const std::string& location, const detail::ExternalLinkTarget& link,
                              std::optional<H5::H5File>& linked_file) {
    Document document;
    if (link.object != "/") {
        reporter.add(location, "the external link must name the root group '/' of a document file");
        return document;
    }
    const std::filesystem::path linked_path(link.file);
    const std::filesystem::path resolved =
        linked_path.is_absolute() ? linked_path : input_file.parent_path() / linked_path;
    const std::string resolved_text = resolved.string();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved, error)) {
        reporter.add(location, std::format("the linked document file '{}' does not exist or is "
                                           "not a regular file",
                                           resolved_text));
        return document;
    }
    bool hdf5 = false;
    try {
        hdf5 = H5::H5File::isHdf5(resolved_text);
    } catch (const H5::Exception&) {
        reporter.add(location,
                     std::format("the linked document file '{}' cannot be read", resolved_text));
        return document;
    }
    if (!hdf5) {
        reporter.add(location, std::format("the linked document file '{}' is not an HDF5 file",
                                           resolved_text));
        return document;
    }
    document.file = resolved;
    guarded_read(reporter, location, [&] {
        linked_file.emplace(resolved_text, H5F_ACC_RDONLY);
        document.root.emplace(linked_file->openGroup("/"));
    });
    return document;
}

// The root of document `kind` in the problem input's root (§2): a group in place, or the root of
// the document file that its external link names. An absent required document is reported; an
// absent optional document is not.
Document find_document(Reporter& reporter, const H5::Group& root,
                       const std::filesystem::path& input_file, InputDocument kind,
                       std::optional<H5::H5File>& linked_file) {
    const std::string member(input_document_name(kind));
    const std::string location = child_location("", member);
    Document document;
    bool exists = false;
    std::optional<detail::ExternalLinkTarget> link;
    if (!guarded_read(reporter, location, [&] {
            exists = root.nameExists(member);
            link = detail::external_link_target(root, member);
        })) {
        return document;
    }
    if (!exists) {
        if (document_required[index_of(kind)]) {
            reporter.add(location, "document is missing");
        }
        return document;
    }
    if (link) {
        return open_linked_document(reporter, input_file, location, *link, linked_file);
    }
    bool group = false;
    if (!guarded_read(reporter, location,
                      [&] { group = is_child(root, member, H5O_TYPE_GROUP); })) {
        return document;
    }
    if (!group) {
        reporter.add(location, "not a group or an external link");
        return document;
    }
    guarded_read(reporter, location, [&] { document.root.emplace(root.openGroup(member)); });
    document.root_path = location;
    document.file = input_file;
    return document;
}

// The `document` and `format_version` attributes of a document root (§2); true when both hold.
bool check_document_attributes(Reporter& reporter, const H5::Group& group,
                               const std::string& root_path, InputDocument kind) {
    const std::string object = root_object_path(root_path);
    const std::string_view name = input_document_name(kind);
    const auto kind_text = read_string_attribute(reporter, group, object, "document");
    const bool kind_ok = kind_text && *kind_text == name;
    if (kind_text && !kind_ok) {
        reporter.add(object + "@document",
                     std::format("document kind '{}' is not '{}'", *kind_text, name));
    }
    const auto version = read_integer_attribute(reporter, group, object, "format_version");
    const bool version_ok = version && *version == supported_format_version;
    if (version && !version_ok) {
        reporter.add(object + "@format_version",
                     std::format("format version {} is not supported; this engine reads version {}",
                                 *version, supported_format_version));
    }
    return kind_ok && version_ok;
}

// Check 3, one document (§2): its root, its attributes, and its members. A document whose
// attributes fail is not read further, so its root is nullopt on return.
Document open_document(Reporter& reporter, const H5::Group& root,
                       const std::filesystem::path& input_file, InputDocument kind,
                       std::array<std::optional<H5::H5File>, input_document_count>& linked_files) {
    Document document =
        find_document(reporter, root, input_file, kind, linked_files[index_of(kind)]);
    if (!document.root) {
        return document;
    }
    Reporter attributes(document.file.string());
    if (check_document_attributes(attributes, *document.root, document.root_path, kind)) {
        check_members(attributes, *document.root, document.root_path,
                      document_members[index_of(kind)]);
    } else {
        document.root.reset();
    }
    reporter.append(attributes.take());
    return document;
}

// The groups of the reactor document that sections 3 to 5 and 7 read (§3–§5). Each is opened once,
// here, so a missing or not-a-group member is reported once; nullopt when diagnosed here, or when
// the reactor document is not read.
struct ReactorGroups {
    std::optional<H5::Group> geometry;
    std::optional<H5::Group> boundary_conditions;
    std::optional<H5::Group> library;
    std::optional<H5::Group> compositions;
};

// Check 3: the reactor document's required groups.
ReactorGroups open_reactor_groups(Reporter& reporter, const Document& reactor) {
    if (!reactor.root) {
        return {};
    }
    return ReactorGroups{
        .geometry = open_group(reporter, *reactor.root, reactor.root_path, "geometry"),
        .boundary_conditions =
            open_group(reporter, *reactor.root, reactor.root_path, "boundary_conditions"),
        .library = open_group(reporter, *reactor.root, reactor.root_path, "library"),
        .compositions = open_group(reporter, *reactor.root, reactor.root_path, "compositions"),
    };
}

// K, the number of input compositions: the length of library_set when that is a rank-1 dataset of
// at least one element. Its diagnostics are the business of check 7, so none are reported here; an
// unknown K leaves the map's upper bound unchecked rather than reporting every node against a bound
// of zero.
std::optional<std::size_t> input_composition_count(const std::optional<H5::Group>& compositions) {
    if (!compositions) {
        return std::nullopt;
    }
    try {
        if (detail::is_external_link(*compositions, "library_set") ||
            !is_child(*compositions, "library_set", H5O_TYPE_DATASET)) {
            return std::nullopt;
        }
        const H5::DataSet set = compositions->openDataSet("library_set");
        const std::vector<std::size_t> shape = dimensions(set);
        if (shape.size() != 1 || shape[0] == 0) {
            return std::nullopt;
        }
        return shape[0];
    } catch (const H5::Exception&) {
        return std::nullopt;
    }
}

// One width dataset `name` of the geometry group at `path`: float64 [n] with n >= 1, in cm,
// every width finite and positive. `widths` is filled only when usable; true when it is.
bool read_width(Reporter& reporter, const H5::Group& geometry, const std::string& path,
                const std::string& name, std::vector<double>& widths) {
    const std::string location = child_location(path, name);
    const auto dataset = open_dataset(reporter, geometry, path, name);
    if (!dataset) {
        return false;
    }
    if (!is_float64(*dataset)) {
        reporter.add(location, "not a float64 dataset");
        return false;
    }
    const std::vector<std::size_t> shape = dimensions(*dataset);
    const bool shape_ok = shape.size() == 1 && shape[0] > 0;
    if (!shape_ok) {
        reporter.add(location, std::format("shape {} is not [n] with n >= 1", join(shape)));
    }
    const bool units_ok = check_units(reporter, *dataset, location, unit_cm);
    if (!shape_ok) {
        return false;
    }
    std::vector<double> values;
    if (!guarded_read(reporter, location, [&] { values = read_doubles(*dataset); })) {
        return false;
    }
    bool usable = units_ok;
    EntryReporter entries(reporter, location);
    for (std::size_t n = 0; n < values.size(); ++n) {
        const double width = values[n];
        if (!std::isfinite(width) || width <= 0.0) {
            usable = false;
            entries.add(
                [n] { return std::format("index {}", n); },
                [width] { return std::format("width {} cm is not finite and positive", width); });
        }
    }
    entries.finish();
    if (usable) {
        widths = std::move(values);
    }
    return usable;
}

// True when `value` is -1, or an input composition index below K. Without K only the lower
// bound -1 is checked.
bool is_map_value_valid(std::int64_t value, std::optional<std::size_t> composition_count) {
    if (value < excluded_position) {
        return false;
    }
    return !composition_count || std::cmp_less(value, *composition_count);
}

std::string map_value_message(std::int64_t value, std::optional<std::size_t> composition_count) {
    if (!composition_count || value < excluded_position) {
        return std::format("value {} is below -1", value);
    }
    return std::format("value {} is not -1 or an input composition index below {}", value,
                       *composition_count);
}

// The values of the map in the dimensions of `reactor`, row-major; each -1 or an input
// composition index. Reports each invalid position (at most the listed limit), true when all
// are valid.
bool check_map_values(Reporter& reporter, const std::string& location,
                      const std::vector<std::int64_t>& values,
                      std::optional<std::size_t> composition_count,
                      const ReactorDescription& reactor) {
    bool valid = true;
    EntryReporter entries(reporter, location);
    for (std::size_t k = 0; k < reactor.nz(); ++k) {
        for (std::size_t j = 0; j < reactor.ny(); ++j) {
            for (std::size_t i = 0; i < reactor.nx(); ++i) {
                const std::size_t position = (((k * reactor.ny()) + j) * reactor.nx()) + i;
                const std::int64_t value = values[position];
                if (!is_map_value_valid(value, composition_count)) {
                    valid = false;
                    entries.add([i, j, k] { return std::format("position ({}, {}, {})", i, j, k); },
                                [value, composition_count] {
                                    return map_value_message(value, composition_count);
                                });
                }
            }
        }
    }
    entries.finish();
    return valid;
}

// Fills the nodes of the reactor from its composition map.
void fill_nodes(ReactorDescription& reactor) {
    reactor.position_nodes.assign(reactor.position_count(), excluded_position);
    for (std::size_t position = 0; position < reactor.composition_map.size(); ++position) {
        if (reactor.composition_map[position] != excluded_position) {
            reactor.position_nodes[position] =
                static_cast<std::int64_t>(reactor.node_positions.size());
            reactor.node_positions.push_back(position);
        }
    }
}

// The composition map (row-major [nz][ny][nx]) of the geometry group at `path`. Its values are
// checked only when the widths are usable, since a position's index needs the dimensions. The
// reactor's positions and nodes are filled only when the map is usable; true when it is.
bool read_composition_map(Reporter& reporter, const H5::Group& geometry, const std::string& path,
                          std::optional<std::size_t> composition_count, bool widths_usable,
                          ReactorDescription& reactor) {
    const std::string location = child_location(path, "composition_map");
    const auto dataset = open_dataset(reporter, geometry, path, "composition_map");
    if (!dataset) {
        return false;
    }
    if (dataset->getTypeClass() != H5T_INTEGER) {
        reporter.add(location, "not an integer dataset");
        return false;
    }
    if (!widths_usable) {
        return false;
    }
    const std::size_t nx = reactor.nx();
    const std::size_t ny = reactor.ny();
    const std::size_t nz = reactor.nz();
    const std::vector<std::size_t> shape = dimensions(*dataset);
    const std::vector<std::size_t> expected{nz, ny, nx};
    if (shape != expected) {
        reporter.add(location, std::format("shape {} does not match [nz][ny][nx] = {}", join(shape),
                                           join(expected)));
        return false;
    }
    std::optional<std::vector<std::int64_t>> values;
    if (!guarded_read(reporter, location, [&] { values = read_integers(*dataset); })) {
        return false;
    }
    if (!values) {
        reporter.add(location, "a value exceeds the signed 64-bit range");
        return false;
    }
    const bool valid = check_map_values(reporter, location, *values, composition_count, reactor);
    const bool any_node =
        std::ranges::any_of(*values, [](std::int64_t value) { return value != excluded_position; });
    if (!any_node) {
        reporter.add(location, "no position is a node");
    }
    if (!valid || !any_node) {
        return false;
    }
    reactor.composition_map = std::move(*values);
    fill_nodes(reactor);
    return true;
}

// Which parts of the geometry are usable: the widths make the dimensions known, the map (given the
// widths) makes the nodes known. A check is skipped only when the part it needs is unusable.
struct GeometryUsability {
    bool widths = false;
    bool map = false;
};

// Check 4: the geometry group at `path`. `composition_count` is K (see input_composition_count).
GeometryUsability read_geometry(Reporter& reporter, const std::optional<H5::Group>& geometry,
                                const std::string& path,
                                std::optional<std::size_t> composition_count,
                                ReactorDescription& reactor) {
    if (!geometry) {
        return {};
    }
    check_members(reporter, *geometry, path, geometry_members);
    const bool width_x_ok = read_width(reporter, *geometry, path, "width_x", reactor.width_x_cm);
    const bool width_y_ok = read_width(reporter, *geometry, path, "width_y", reactor.width_y_cm);
    const bool width_z_ok = read_width(reporter, *geometry, path, "width_z", reactor.width_z_cm);
    const bool widths_usable = width_x_ok && width_y_ok && width_z_ok;
    const bool map_usable =
        read_composition_map(reporter, *geometry, path, composition_count, widths_usable, reactor);
    return {.widths = widths_usable, .map = map_usable};
}

std::optional<BoundaryKind> parse_boundary_kind(std::string_view text) {
    for (const BoundaryKind kind : std::array{BoundaryKind::reflective, BoundaryKind::vacuum,
                                              BoundaryKind::zero_flux, BoundaryKind::albedo}) {
        if (boundary_kind_name(kind) == text) {
            return kind;
        }
    }
    return std::nullopt;
}

// α_b of a kind with a fixed albedo (contract §4, TM Eq. (4.4)).
double fixed_albedo(BoundaryKind kind) {
    switch (kind) {
    case BoundaryKind::reflective:
        return reflective_albedo;
    case BoundaryKind::vacuum:
        return vacuum_albedo;
    case BoundaryKind::zero_flux:
        return zero_flux_albedo;
    case BoundaryKind::albedo:
        break;
    }
    std::unreachable();
}

// The `albedo` dataset of a face group at `path`: float64 [2], in units "1", each finite and
// non-negative. `albedo` is filled only when usable.
void read_albedo(Reporter& reporter, const H5::Group& group, const std::string& path,
                 std::array<double, energy_group_count>& albedo) {
    const std::string location = child_location(path, "albedo");
    const auto dataset = open_dataset(reporter, group, path, "albedo");
    if (!dataset) {
        return;
    }
    if (!is_float64(*dataset)) {
        reporter.add(location, "not a float64 dataset");
        return;
    }
    const std::vector<std::size_t> shape = dimensions(*dataset);
    const std::vector<std::size_t> expected{energy_group_count};
    const bool shape_ok = shape == expected;
    if (!shape_ok) {
        reporter.add(location,
                     std::format("shape {} does not match [{}]", join(shape), energy_group_count));
    }
    const bool units_ok = check_units(reporter, *dataset, location, unit_dimensionless);
    if (!shape_ok) {
        return;
    }
    std::vector<double> values;
    if (!guarded_read(reporter, location, [&] { values = read_doubles(*dataset); })) {
        return;
    }
    bool usable = units_ok;
    EntryReporter entries(reporter, location);
    for (std::size_t n = 0; n < values.size(); ++n) {
        const double value = values[n];
        if (!std::isfinite(value) || value < 0.0) {
            usable = false;
            entries.add(
                [n] { return std::format("index {} group {}", n, n + 1); },
                [value] { return std::format("albedo {} is not finite and non-negative", value); });
        }
    }
    entries.finish();
    if (usable) {
        std::ranges::copy(values, albedo.begin());
    }
}

// One face group `name` of the boundary conditions group at `parent`: its `kind`, and the `albedo`
// of kind albedo. A group whose kind is missing or unknown still has its members checked, against
// the albedo kind's members, so a misspelt member is reported too.
void read_face(Reporter& reporter, const H5::Group& boundary, const std::string& parent,
               const std::string& name, BoundaryCondition& condition) {
    const auto group = open_group(reporter, boundary, parent, name);
    if (!group) {
        return;
    }
    const std::string path = child_location(parent, name);
    const auto kind_text = read_string_attribute(reporter, *group, path, "kind");
    const std::optional<BoundaryKind> kind =
        kind_text ? parse_boundary_kind(*kind_text) : std::nullopt;
    if (!kind) {
        if (kind_text) {
            reporter.add(path + "@kind",
                         std::format("unknown kind '{}'; expected reflective, vacuum, zero_flux or "
                                     "albedo",
                                     *kind_text));
        }
        check_members(reporter, *group, path, albedo_members);
        return;
    }
    condition.kind = *kind;
    if (*kind == BoundaryKind::albedo) {
        check_members(reporter, *group, path, albedo_members);
        read_albedo(reporter, *group, path, condition.albedo);
    } else {
        check_members(reporter, *group, path, std::span<const std::string_view>{});
        condition.albedo.fill(fixed_albedo(*kind));
    }
}

// Check 5: the boundary conditions group at `path`. The exterior face is read when required, or
// when present.
void read_boundary_conditions(Reporter& reporter, const std::optional<H5::Group>& boundary,
                              const std::string& path, bool exterior_required,
                              ReactorDescription& reactor) {
    if (!boundary) {
        return;
    }
    check_members(reporter, *boundary, path, boundary_members);
    for (std::size_t f = 0; f < face_count; ++f) {
        read_face(reporter, *boundary, path, std::string(face_name(static_cast<Face>(f))),
                  reactor.bounding_faces[f]);
    }
    if (exterior_required || boundary->nameExists("exterior")) {
        read_face(reporter, *boundary, path, "exterior", reactor.exterior);
    }
}

// Check 6: the library group at `path`, and the nodal library it names. A relative library path
// resolves against the directory of the reactor document's file, `reactor_file`.
// `binding.library_file` is the resolved path; `library` is filled when the library is read and
// passes its checks.
void read_library(Reporter& reporter, const std::optional<H5::Group>& group,
                  const std::string& path, const std::filesystem::path& reactor_file,
                  LibraryBinding& binding, std::optional<NodalLibrary>& library) {
    if (!group) {
        return;
    }
    check_members(reporter, *group, path, std::span<const std::string_view>{});
    const auto text = read_string_attribute(reporter, *group, path, "path");
    if (!text) {
        return;
    }
    if (text->empty()) {
        reporter.add(path + "@path", "the library path is empty");
        return;
    }
    const std::filesystem::path library_path(*text);
    binding.library_file =
        library_path.is_absolute() ? library_path : reactor_file.parent_path() / library_path;
    std::expected<NodalLibrary, Diagnostics> loaded = read_nodal_library(binding.library_file);
    if (!loaded) {
        // The library's diagnostics already name the library file.
        reporter.append(std::move(loaded.error()));
        return;
    }
    library = std::move(*loaded);
}

// The index of the first element of `range` whose projection equals `name`.
template <typename Range, typename Projection>
std::optional<std::size_t> find_index(const Range& range, const std::string& name,
                                      Projection projection) {
    const auto found = std::ranges::find(range, name, projection);
    if (found == std::ranges::end(range)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::ranges::distance(std::ranges::begin(range), found));
}

// A string dataset `name` of `group` (at `path`) of rank 1; when `length` is given, its length
// must be that. `names` is filled when usable; true when it is.
bool read_names(Reporter& reporter, const H5::Group& group, const std::string& path,
                const std::string& name, std::optional<std::size_t> length,
                std::vector<std::string>& names) {
    const std::string location = child_location(path, name);
    const auto dataset = open_dataset(reporter, group, path, name);
    if (!dataset) {
        return false;
    }
    if (dataset->getTypeClass() != H5T_STRING) {
        reporter.add(location, "not a string dataset");
        return false;
    }
    const std::vector<std::size_t> shape = dimensions(*dataset);
    if (shape.size() != 1) {
        reporter.add(location, std::format("shape {} is not [K]", join(shape)));
        return false;
    }
    if (length && shape[0] != *length) {
        reporter.add(location, std::format("shape {} does not match library_set's [{}]",
                                           join(shape), *length));
        return false;
    }
    return guarded_read(reporter, location, [&] { names = read_strings(*dataset); });
}

// The compositions group at `path`: `library_set` and `library_composition`, each a string [K]
// with K >= 1. The binding's input compositions are filled only when both are usable; true when
// they are.
bool read_input_compositions(Reporter& reporter, const std::optional<H5::Group>& group,
                             const std::string& path, LibraryBinding& binding) {
    if (!group) {
        return false;
    }
    check_members(reporter, *group, path, composition_members);
    std::vector<std::string> sets;
    bool sets_ok = read_names(reporter, *group, path, "library_set", std::nullopt, sets);
    if (sets_ok && sets.empty()) {
        reporter.add(child_location(path, "library_set"), "no input composition is listed");
        sets_ok = false;
    }
    std::optional<std::size_t> expected_length;
    if (sets_ok) {
        expected_length = sets.size();
    }
    std::vector<std::string> compositions;
    const bool compositions_ok =
        read_names(reporter, *group, path, "library_composition", expected_length, compositions);
    if (!sets_ok || !compositions_ok) {
        return false;
    }
    for (std::size_t c = 0; c < sets.size(); ++c) {
        binding.compositions.push_back(InputComposition{
            .library_set = std::move(sets[c]), .library_composition = std::move(compositions[c])});
    }
    return true;
}

// Each input composition's library composition (§5). Each one is resolved on its own: an entry is
// nullopt when it does not resolve, and a diagnostic says why. Entries are reported at the
// compositions group at `path`.
std::vector<std::optional<BoundComposition>>
resolve_compositions(Reporter& reporter, const NodalLibrary& library, const std::string& path,
                     const std::vector<InputComposition>& inputs) {
    std::vector<std::optional<BoundComposition>> bound;
    EntryReporter entries(reporter, path);
    for (std::size_t c = 0; c < inputs.size(); ++c) {
        const InputComposition& input = inputs[c];
        const auto entry = [c, &input] {
            return std::format("input composition {} ('{}', '{}')", c, input.library_set,
                               input.library_composition);
        };
        const std::optional<std::size_t> set =
            find_index(library.composition_sets, input.library_set, &CompositionSet::name);
        std::optional<std::size_t> composition;
        if (set) {
            composition = find_index(library.composition_sets[*set].composition_names,
                                     input.library_composition, std::identity{});
        }
        std::optional<BoundComposition> resolved;
        if (!set) {
            entries.add(entry,
                        std::format("the library has no composition set '{}'", input.library_set));
        } else if (!composition) {
            entries.add(entry, std::format("composition set '{}' has no composition '{}'",
                                           input.library_set, input.library_composition));
        } else {
            resolved = BoundComposition{.set = *set, .composition = *composition};
        }
        bound.push_back(resolved);
    }
    entries.finish();
    return bound;
}

// Contract §5: each used input composition that resolved is valid (§4). The compositions some
// node is made of are grouped by library set, each set's list unique and ascending. An input
// composition that did not resolve is not checked.
void check_used_compositions(Reporter& reporter, const NodalLibrary& library,
                             const ReactorDescription& reactor,
                             const std::vector<std::optional<BoundComposition>>& bound) {
    std::vector<std::vector<std::size_t>> used(library.composition_sets.size());
    for (const std::size_t position : reactor.node_positions) {
        const auto input = static_cast<std::size_t>(reactor.composition_map[position]);
        if (const std::optional<BoundComposition>& entry = bound[input]; entry) {
            used[entry->set].push_back(entry->composition);
        }
    }
    for (std::size_t set = 0; set < used.size(); ++set) {
        std::vector<std::size_t>& compositions = used[set];
        if (compositions.empty()) {
            continue;
        }
        std::ranges::sort(compositions);
        compositions.erase(std::ranges::unique(compositions).begin(), compositions.end());
        reporter.append(
            check_compositions_usable(library, library.composition_sets[set], compositions));
    }
}

// Check 7: the compositions group at `path`. The input compositions resolve when the library is
// read and their names are usable. Every input composition is resolved and reported, however many
// fail; the used ones are checked when the map is usable (the nodes are known). Each entry is
// nullopt when it does not resolve, or when the names or the library are unusable (no entries).
std::vector<std::optional<BoundComposition>>
read_compositions(Reporter& reporter, const std::optional<H5::Group>& group,
                  const std::string& path, const std::optional<NodalLibrary>& library,
                  const GeometryUsability& geometry, const ReactorDescription& reactor,
                  LibraryBinding& binding) {
    const bool names_ok = read_input_compositions(reporter, group, path, binding);
    if (!names_ok || !library) {
        return {};
    }
    std::vector<std::optional<BoundComposition>> bound =
        resolve_compositions(reporter, *library, path, binding.compositions);
    if (geometry.map) {
        check_used_compositions(reporter, *library, reactor, bound);
    }
    return bound;
}

// The one element of `values`, the value of a scalar dataset; a diagnostic and nullopt otherwise.
template <typename T>
std::optional<T> single_element(Reporter& reporter, const std::string& location,
                                const std::vector<T>& values) {
    if (values.size() != 1) {
        reporter.add(location, "not a scalar");
        return std::nullopt;
    }
    return values.front();
}

// The scalar dataset `name` of `group` (at `path`), or a diagnostic when it is missing, not a
// dataset, or not of the shape of a scalar.
std::optional<H5::DataSet> open_scalar(Reporter& reporter, const H5::Group& group,
                                       const std::string& path, const std::string& name) {
    auto dataset = open_dataset(reporter, group, path, name);
    if (!dataset) {
        return std::nullopt;
    }
    const std::vector<std::size_t> shape = dimensions(*dataset);
    if (!shape.empty()) {
        reporter.add(child_location(path, name),
                     std::format("shape {} is not a scalar", join(shape)));
        return std::nullopt;
    }
    return dataset;
}

// The value of the float64 scalar `name` of `group` (at `path`), in `units` when given; nullopt
// when it is unreadable (diagnosed). A wrong unit is reported, but does not hide the value. The
// value is not checked against a rule.
std::optional<double> read_float_scalar(Reporter& reporter, const H5::Group& group,
                                        const std::string& path, const std::string& name,
                                        std::optional<std::string_view> units) {
    const std::string location = child_location(path, name);
    const auto dataset = open_scalar(reporter, group, path, name);
    if (!dataset) {
        return std::nullopt;
    }
    if (!is_float64(*dataset)) {
        reporter.add(location, "not a float64 dataset");
        return std::nullopt;
    }
    if (units) {
        // The mismatch is reported here; the value is still read and returned.
        static_cast<void>(check_units(reporter, *dataset, location, *units));
    }
    std::vector<double> values;
    if (!guarded_read(reporter, location, [&] { values = read_doubles(*dataset); })) {
        return std::nullopt;
    }
    return single_element(reporter, location, values);
}

// The value of the integer scalar `name` of `group` (at `path`); nullopt when it is unusable
// (diagnosed).
std::optional<std::int64_t> read_integer_scalar(Reporter& reporter, const H5::Group& group,
                                                const std::string& path, const std::string& name) {
    const std::string location = child_location(path, name);
    const auto dataset = open_scalar(reporter, group, path, name);
    if (!dataset) {
        return std::nullopt;
    }
    if (dataset->getTypeClass() != H5T_INTEGER) {
        reporter.add(location, "not an integer dataset");
        return std::nullopt;
    }
    std::optional<std::vector<std::int64_t>> values;
    if (!guarded_read(reporter, location, [&] { values = read_integers(*dataset); })) {
        return std::nullopt;
    }
    if (!values) {
        reporter.add(location, "a value exceeds the signed 64-bit range");
        return std::nullopt;
    }
    return single_element(reporter, location, *values);
}

// A rule on a float64 value (§6, §7). `holds` is false for a non-finite value: the ordered
// comparisons run only after isfinite, since a NaN compare may trap.
struct FloatRule {
    std::string_view text; // completes "value ... is not <text>"
    bool (*holds)(double value);
};

bool is_finite_non_negative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool is_finite_positive(double value) {
    return std::isfinite(value) && value > 0.0;
}

bool is_open_unit(double value) {
    return std::isfinite(value) && value > 0.0 && value < 1.0;
}

bool is_half_open_unit(double value) {
    return std::isfinite(value) && value > 0.0 && value <= 1.0;
}

constexpr FloatRule non_negative_rule{.text = "finite and non-negative",
                                      .holds = is_finite_non_negative};
constexpr FloatRule positive_rule{.text = "finite and positive", .holds = is_finite_positive};
constexpr FloatRule open_unit_rule{.text = "in (0, 1)", .holds = is_open_unit};
constexpr FloatRule half_open_unit_rule{.text = "in (0, 1]", .holds = is_half_open_unit};

// Check 8, thermal_power (§6): a float64 scalar in W, finite and positive.
std::optional<double> read_thermal_power(Reporter& reporter, const H5::Group& group,
                                         const std::string& path) {
    const std::optional<double> power =
        read_float_scalar(reporter, group, path, "thermal_power", unit_watt);
    if (power && !positive_rule.holds(*power)) {
        reporter.add(child_location(path, "thermal_power"),
                     std::format("{} {} is not {}", *power, unit_watt, positive_rule.text));
        return std::nullopt;
    }
    return power;
}

// A node-state field of the initial state (§6): its units, its rule, and the state axis of its
// composition sets (§6, check 11).
struct NodeField {
    std::string_view name;
    std::string_view units;
    FloatRule rule;
    StateAxis axis;
};

constexpr NodeField doppler_field{.name = "doppler_temperature",
                                  .units = unit_kelvin,
                                  .rule = positive_rule,
                                  .axis = StateAxis::fuel_temperature};
constexpr NodeField density_field{.name = "moderator_density",
                                  .units = unit_kg_per_m3,
                                  .rule = positive_rule,
                                  .axis = StateAxis::moderator_density};
constexpr NodeField moderator_temperature_field{.name = "moderator_temperature",
                                                .units = unit_kelvin,
                                                .rule = positive_rule,
                                                .axis = StateAxis::moderator_temperature};
constexpr NodeField boron_field{
    .name = "boron", .units = unit_ppm, .rule = non_negative_rule, .axis = StateAxis::boron};

// The node-state fields, in the order of their values in NodeValues.
constexpr std::array<NodeField, node_field_count> node_fields{
    doppler_field, density_field, moderator_temperature_field, boron_field};

// The values of the node-state fields, in node order; each is filled only when its field is usable.
using NodeValues = std::array<std::optional<std::vector<double>>, node_field_count>;

std::string field_message(const NodeField& field, double value) {
    return std::format("{} {} is not {}", value, field.units, field.rule.text);
}

// "position (i, j, k)" of a row-major position (contract §3).
std::string position_entry(const ReactorDescription& reactor, std::size_t position) {
    const std::size_t i = position % reactor.nx();
    const std::size_t j = (position / reactor.nx()) % reactor.ny();
    const std::size_t k = position / (reactor.nx() * reactor.ny());
    return std::format("position ({}, {}, {})", i, j, k);
}

// The values at the node positions of an array field, in node order. Each invalid one is
// reported and clears `usable`; values at excluded positions are never read.
std::vector<double> node_values_of_array(Reporter& reporter, const std::string& location,
                                         const NodeField& field, const std::vector<double>& values,
                                         const ReactorDescription& reactor, bool& usable) {
    std::vector<double> per_node;
    EntryReporter entries(reporter, location);
    for (const std::size_t position : reactor.node_positions) {
        const double value = values[position];
        if (!field.rule.holds(value)) {
            usable = false;
            entries.add([&reactor, position] { return position_entry(reactor, position); },
                        [&field, value] { return field_message(field, value); });
        }
        per_node.push_back(value);
    }
    entries.finish();
    return per_node;
}

// A node-state field (§6): a float64 scalar, or an array [nz][ny][nx]. The type and the units are
// checked always; an array's shape when the widths are usable (the dimensions are known); its
// values at the node positions when the map is usable too (the nodes are known). The value of
// each node, in node order, when the field and the map are usable and every value is valid;
// nullopt otherwise.
std::optional<std::vector<double>> read_node_field(Reporter& reporter, const H5::Group& group,
                                                   const std::string& path, const NodeField& field,
                                                   const GeometryUsability& geometry,
                                                   const ReactorDescription& reactor) {
    const std::string name(field.name);
    const std::string location = child_location(path, name);
    const auto dataset = open_dataset(reporter, group, path, name);
    if (!dataset) {
        return std::nullopt;
    }
    if (!is_float64(*dataset)) {
        reporter.add(location, "not a float64 dataset");
        return std::nullopt;
    }
    const std::vector<std::size_t> shape = dimensions(*dataset);
    const bool scalar = shape.empty();
    const bool shape_checked = !scalar && geometry.widths;
    bool shape_ok = true;
    if (shape_checked) {
        const std::vector<std::size_t> expected{reactor.nz(), reactor.ny(), reactor.nx()};
        shape_ok = shape == expected;
        if (!shape_ok) {
            reporter.add(location, std::format("shape {} is not a scalar or [nz][ny][nx] = {}",
                                               join(shape), join(expected)));
        }
    }
    const bool units_ok = check_units(reporter, *dataset, location, field.units);
    // The values are read only when they are checked: a scalar's always, an array's only when
    // its shape is known to match and the map is usable.
    if (!scalar && !(geometry.map && shape_ok)) {
        return std::nullopt;
    }
    std::vector<double> values;
    if (!guarded_read(reporter, location, [&] { values = read_doubles(*dataset); })) {
        return std::nullopt;
    }
    bool usable = units_ok && shape_ok;
    std::vector<double> per_node;
    if (scalar) {
        const std::optional<double> value = single_element(reporter, location, values);
        if (!value) {
            return std::nullopt;
        }
        if (!field.rule.holds(*value)) {
            reporter.add(location, field_message(field, *value));
            usable = false;
        }
        per_node.assign(reactor.node_count(), *value);
    } else {
        per_node = node_values_of_array(reporter, location, field, values, reactor, usable);
    }
    if (!usable || !geometry.map) {
        return std::nullopt;
    }
    return per_node;
}

// Check 8 (§6): the initial state document. Its `source` and its thermal power are read here, and
// the node-state fields are returned in node order, each nullopt when it is unusable. The node
// states are filled when all four fields are usable.
NodeValues read_initial_state(Reporter& reporter, const Document& state,
                              const GeometryUsability& geometry, const ReactorDescription& reactor,
                              OperatingState& operating) {
    if (!state.root) {
        return {};
    }
    const H5::Group& group = *state.root;
    const std::string object = root_object_path(state.root_path);
    const auto source = read_string_attribute(reporter, group, object, "source");
    if (source && *source != "stated") {
        reporter.add(object + "@source",
                     std::format("unknown source '{}'; expected stated", *source));
    }
    const std::optional<double> power = read_thermal_power(reporter, group, state.root_path);
    if (power) {
        operating.thermal_power_w = *power;
    }
    NodeValues values;
    for (std::size_t f = 0; f < node_field_count; ++f) {
        values[f] =
            read_node_field(reporter, group, state.root_path, node_fields[f], geometry, reactor);
    }
    std::array<const std::vector<double>*, node_field_count> filled{};
    for (std::size_t f = 0; f < node_field_count; ++f) {
        const std::optional<std::vector<double>>& field_values = values[f];
        if (!field_values) {
            return values;
        }
        filled[f] = &*field_values;
    }
    for (std::size_t n = 0; n < filled[0]->size(); ++n) {
        operating.node_states.push_back(NodeState{
            .doppler_temperature_k = (*filled[0])[n],
            .moderator_density_kg_m3 = (*filled[1])[n],
            .moderator_temperature_k = (*filled[2])[n],
            .boron_ppm = (*filled[3])[n],
        });
    }
    return values;
}

// Check 11 (§6): the node-state field `field`, at the node positions, lies within the axis of its
// composition set. A node whose input composition does not resolve, or whose set has no such axis,
// is not checked. Reported at `location`, at most the listed limit of entries.
void check_field_coverage(Reporter& reporter, const std::string& location, const NodeField& field,
                          const std::vector<double>& values, const NodalLibrary& library,
                          const std::vector<std::optional<BoundComposition>>& bound,
                          const ReactorDescription& reactor) {
    EntryReporter entries(reporter, location);
    for (std::size_t node = 0; node < values.size(); ++node) {
        const std::size_t position = reactor.node_positions[node];
        const auto input = static_cast<std::size_t>(reactor.composition_map[position]);
        const std::optional<BoundComposition>& composition = bound[input];
        if (!composition) {
            continue;
        }
        const CompositionSet& set = library.composition_sets[composition->set];
        const auto axis = std::ranges::find(set.axes, field.axis, &AxisPoints::axis);
        if (axis == set.axes.end() || axis->points.empty()) {
            continue;
        }
        const double value = values[node];
        const bool below = std::isless(value, axis->points.front());
        const bool above = std::isgreater(value, axis->points.back());
        if (!below && !above) {
            continue;
        }
        const double limit = below ? axis->points.front() : axis->points.back();
        const std::string_view relation = below ? "below the first point" : "above the last point";
        entries.add([&reactor, position] { return position_entry(reactor, position); },
                    [&field, &set, value, limit, relation] {
                        return std::format(
                            "{} {} is {} {} {} of axis {} of composition set '{}'; table "
                            "clamps are not allowed",
                            value, field.units, relation, limit, field.units,
                            state_axis_name(field.axis), set.name);
                    });
    }
    entries.finish();
}

// Check 11 (§6): unless table clamps are allowed, each node-state field that was filled lies
// within the axes of the composition sets of the nodes it is at. Nothing is checked when the
// library, the compositions or the node states are unusable.
void check_library_coverage(Reporter& reporter, const Document& state, const NodeValues& values,
                            bool allow_table_clamps, const std::optional<NodalLibrary>& library,
                            const std::vector<std::optional<BoundComposition>>& bound,
                            const ReactorDescription& reactor) {
    if (allow_table_clamps || !library || bound.empty()) {
        return;
    }
    for (std::size_t f = 0; f < node_field_count; ++f) {
        const std::optional<std::vector<double>>& field_values = values[f];
        if (field_values) {
            check_field_coverage(reporter, child_location(state.root_path, node_fields[f].name),
                                 node_fields[f], *field_values, *library, bound, reactor);
        }
    }
}

// A float64 option of the settings document (§7): its rule and its member of SolverOptions.
struct FloatOption {
    std::string_view name;
    FloatRule rule;
    double SolverOptions::* member;
};

// An integer option of the settings document (§7): its member of SolverOptions; at least 1.
struct IntegerOption {
    std::string_view name;
    std::int64_t SolverOptions::* member;
};

constexpr std::array float_options{
    FloatOption{.name = "kernel_switch_threshold",
                .rule = non_negative_rule,
                .member = &SolverOptions::kernel_switch_threshold},
    FloatOption{
        .name = "wielandt_shift", .rule = positive_rule, .member = &SolverOptions::wielandt_shift},
    FloatOption{.name = "eigenvalue_tolerance",
                .rule = positive_rule,
                .member = &SolverOptions::eigenvalue_tolerance},
    FloatOption{.name = "fission_source_l2_tolerance",
                .rule = positive_rule,
                .member = &SolverOptions::fission_source_l2_tolerance},
    FloatOption{.name = "fission_source_linf_tolerance",
                .rule = positive_rule,
                .member = &SolverOptions::fission_source_linf_tolerance},
    FloatOption{.name = "inner_reduction",
                .rule = open_unit_rule,
                .member = &SolverOptions::inner_reduction},
    FloatOption{.name = "coupling_relaxation",
                .rule = half_open_unit_rule,
                .member = &SolverOptions::coupling_relaxation},
};

constexpr std::array integer_options{
    IntegerOption{.name = "inner_iteration_limit", .member = &SolverOptions::inner_iteration_limit},
    IntegerOption{.name = "outer_iteration_limit", .member = &SolverOptions::outer_iteration_limit},
    IntegerOption{.name = "nodal_update_period", .member = &SolverOptions::nodal_update_period},
};

std::optional<KernelChoice> parse_kernel(std::string_view text) {
    for (const KernelChoice kernel : std::array{KernelChoice::finite_difference, KernelChoice::nem,
                                                KernelChoice::anm, KernelChoice::hybrid}) {
        if (kernel_choice_name(kernel) == text) {
            return kernel;
        }
    }
    return std::nullopt;
}

// Check 9, kernel (§7): a string naming one of the four kernels.
void read_kernel(Reporter& reporter, const H5::Group& group, const std::string& path,
                 SolverOptions& options) {
    if (!group.nameExists("kernel")) {
        return;
    }
    const std::string location = child_location(path, "kernel");
    const auto dataset = open_scalar(reporter, group, path, "kernel");
    if (!dataset) {
        return;
    }
    if (dataset->getTypeClass() != H5T_STRING) {
        reporter.add(location, "not a string dataset");
        return;
    }
    std::vector<std::string> texts;
    if (!guarded_read(reporter, location, [&] { texts = read_strings(*dataset); })) {
        return;
    }
    const std::optional<std::string> text = single_element(reporter, location, texts);
    if (!text) {
        return;
    }
    const std::optional<KernelChoice> kernel = parse_kernel(*text);
    if (!kernel) {
        reporter.add(location, std::format("unknown kernel '{}'; expected finite_difference, nem, "
                                           "anm or hybrid",
                                           *text));
        return;
    }
    options.kernel = *kernel;
}

void read_float_options(Reporter& reporter, const H5::Group& group, const std::string& path,
                        SolverOptions& options) {
    for (const FloatOption& option : float_options) {
        const std::string name(option.name);
        if (!group.nameExists(name)) {
            continue;
        }
        const std::optional<double> value =
            read_float_scalar(reporter, group, path, name, std::nullopt);
        if (!value) {
            continue;
        }
        if (!option.rule.holds(*value)) {
            reporter.add(child_location(path, name),
                         std::format("value {} is not {}", *value, option.rule.text));
            continue;
        }
        options.*(option.member) = *value;
    }
}

void read_integer_options(Reporter& reporter, const H5::Group& group, const std::string& path,
                          SolverOptions& options) {
    for (const IntegerOption& option : integer_options) {
        const std::string name(option.name);
        if (!group.nameExists(name)) {
            continue;
        }
        const std::optional<std::int64_t> value = read_integer_scalar(reporter, group, path, name);
        if (!value) {
            continue;
        }
        if (*value < minimum_iteration_count) {
            reporter.add(child_location(path, name), std::format("value {} is not at least {}",
                                                                 *value, minimum_iteration_count));
            continue;
        }
        options.*(option.member) = *value;
    }
}

// A 0 or 1 integer scalar `name` of the group at `path` (§7) into `flag`. Any other value is
// reported and leaves `flag` unchanged.
void read_flag(Reporter& reporter, const H5::Group& group, const std::string& path,
               const std::string& name, bool& flag) {
    if (!group.nameExists(name)) {
        return;
    }
    const std::optional<std::int64_t> value = read_integer_scalar(reporter, group, path, name);
    if (!value) {
        return;
    }
    if (*value != 0 && *value != 1) {
        reporter.add(child_location(path, name), std::format("value {} is not 0 or 1", *value));
        return;
    }
    flag = *value == 1;
}

// Check 9 (§7): the settings document. An absent member takes its default. The document's
// members are checked at check 3.
void read_settings(Reporter& reporter, const Document& settings, SolverOptions& options,
                   bool& allow_table_clamps) {
    if (!settings.root) {
        return;
    }
    const H5::Group& group = *settings.root;
    read_kernel(reporter, group, settings.root_path, options);
    read_float_options(reporter, group, settings.root_path, options);
    read_integer_options(reporter, group, settings.root_path, options);
    read_flag(reporter, group, settings.root_path, "adjoint", options.adjoint);
    read_flag(reporter, group, settings.root_path, "allow_table_clamps", allow_table_clamps);
}

} // namespace

std::string_view input_document_name(InputDocument document) {
    return document_names[index_of(document)];
}

std::expected<Problem, Diagnostics> load_problem(const std::filesystem::path& file) {
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
        const H5::Group root = h5.openGroup("/");
        ProblemInput input;
        input.file = file;

        const auto version = read_integer_attribute(reporter, h5, "/", "format_version");
        if (version && *version != supported_format_version) {
            reporter.add("/@format_version",
                         std::format("format version {} is not supported; this engine reads "
                                     "version {}",
                                     *version, supported_format_version));
        }
        if (!version || *version != supported_format_version) {
            return std::unexpected(reporter.take());
        }

        // Check 3: the root members, then the four documents (§2).
        check_members(reporter, root, "", document_names, true);
        std::array<std::optional<H5::H5File>, input_document_count> linked_files;
        const Document reactor =
            open_document(reporter, root, file, InputDocument::reactor, linked_files);
        const Document initial_state =
            open_document(reporter, root, file, InputDocument::initial_state, linked_files);
        const Document control_signals =
            open_document(reporter, root, file, InputDocument::control_signals, linked_files);
        const Document settings =
            open_document(reporter, root, file, InputDocument::settings, linked_files);

        // Checks 3 to 7: the reactor document, reported in its own file.
        Reporter reactor_reporter(reactor.file.string());
        const ReactorGroups groups = open_reactor_groups(reactor_reporter, reactor);
        const std::optional<std::size_t> composition_count =
            input_composition_count(groups.compositions);
        const GeometryUsability geometry = read_geometry(
            reactor_reporter, groups.geometry, child_location(reactor.root_path, "geometry"),
            composition_count, input.reactor);
        const bool exterior_required =
            geometry.map && input.reactor.node_count() < input.reactor.position_count();
        read_boundary_conditions(reactor_reporter, groups.boundary_conditions,
                                 child_location(reactor.root_path, "boundary_conditions"),
                                 exterior_required, input.reactor);
        std::optional<NodalLibrary> library;
        read_library(reactor_reporter, groups.library, child_location(reactor.root_path, "library"),
                     reactor.file, input.binding, library);
        const std::vector<std::optional<BoundComposition>> bound =
            read_compositions(reactor_reporter, groups.compositions,
                              child_location(reactor.root_path, "compositions"), library, geometry,
                              input.reactor, input.binding);
        reporter.append(reactor_reporter.take());

        // Check 8: the initial state document.
        Reporter state_reporter(initial_state.file.string());
        const NodeValues node_values = read_initial_state(state_reporter, initial_state, geometry,
                                                          input.reactor, input.operating_state);
        reporter.append(state_reporter.take());

        // Check 9: the settings document.
        Reporter settings_reporter(settings.file.string());
        read_settings(settings_reporter, settings, input.solver_options, input.allow_table_clamps);
        reporter.append(settings_reporter.take());

        // Check 10: the control signals document has no members, checked at check 3.

        // Check 11: the node states against the library's axes.
        Reporter coverage_reporter(initial_state.file.string());
        check_library_coverage(coverage_reporter, initial_state, node_values,
                               input.allow_table_clamps, library, bound, input.reactor);
        reporter.append(coverage_reporter.take());

        if (reporter.count() > 0) {
            return std::unexpected(reporter.take());
        }
        input.document_files = {reactor.file, initial_state.file, control_signals.file,
                                settings.file};
        NTS_ASSERT(library && !bound.empty(),
                   "a problem without diagnostics lacks its library or compositions");
        std::vector<BoundComposition> compositions;
        compositions.reserve(bound.size());
        for (const std::optional<BoundComposition>& entry : bound) {
            NTS_ASSERT(entry.has_value(),
                       "a problem without diagnostics has an unbound composition");
            compositions.push_back(*entry);
        }
        return Problem{.input = std::move(input),
                       .library = std::move(*library),
                       .compositions = std::move(compositions)};
    } catch (const H5::Exception& exception) {
        reporter.add("/", std::format("HDF5 error in {}: {}", exception.getFuncName(),
                                      exception.getDetailMsg()));
        return std::unexpected(reporter.take());
    }
}

} // namespace nts
