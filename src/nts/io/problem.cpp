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

constexpr std::int64_t supported_format_version = 1;
constexpr std::int64_t excluded_position = -1; // a composition_map value: no node there

constexpr std::string_view unit_cm = "cm";
constexpr std::string_view unit_dimensionless = "1";
constexpr std::string_view unit_watt = "W";
constexpr std::string_view unit_kelvin = "K";
constexpr std::string_view unit_kg_per_m3 = "kg m^-3";
constexpr std::string_view unit_ppm = "ppm";

constexpr std::array<std::string_view, 6> root_members{"geometry",        "boundary_conditions",
                                                       "library",         "compositions",
                                                       "operating_state", "solver_options"};
constexpr std::array<std::string_view, 4> geometry_members{"width_x", "width_y", "width_z",
                                                           "composition_map"};
constexpr std::array<std::string_view, face_count + 1> boundary_members{
    "x_minus", "x_plus", "y_minus", "y_plus", "z_minus", "z_plus", "exterior"};
constexpr std::array<std::string_view, 1> albedo_members{"albedo"};
constexpr std::array<std::string_view, 2> composition_members{"library_set", "library_composition"};
constexpr std::array<std::string_view, 5> operating_state_members{
    "thermal_power", "doppler_temperature", "moderator_density", "moderator_temperature", "boron"};
constexpr std::array<std::string_view, 12> solver_option_members{"kernel",
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
                                                                 "adjoint"};
constexpr std::int64_t minimum_iteration_count = 1;

// The location of member `name` of the group at `path`; the root's path is empty.
std::string child_location(const std::string& path, std::string_view name) {
    return std::format("{}/{}", path, name);
}

// Reports every link of `group` that `allowed` does not name.
void check_members(Reporter& reporter, const H5::Group& group, const std::string& path,
                   std::span<const std::string_view> allowed) {
    std::vector<std::string> names;
    if (!guarded_read(reporter, path.empty() ? "/" : path, [&] { names = member_names(group); })) {
        return;
    }
    for (const std::string& name : names) {
        if (std::ranges::find(allowed, name) == allowed.end()) {
            reporter.add(child_location(path, name), "not a member of the problem input contract");
        }
    }
}

// The group `name` of `parent` (at `path`), or a diagnostic when it is missing or not a group.
std::optional<H5::Group> open_group(Reporter& reporter, const H5::Group& parent,
                                    const std::string& path, const std::string& name) {
    const std::string location = child_location(path, name);
    std::optional<H5::Group> group;
    guarded_read(reporter, location, [&] {
        if (!parent.nameExists(name)) {
            reporter.add(location, "group is missing");
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

// The groups of the root that sections 3 to 9 read (§2). Each is opened once, here, so a missing
// or not-a-group member is reported once; a member that is nullopt was diagnosed here.
struct RootGroups {
    std::optional<H5::Group> geometry;
    std::optional<H5::Group> boundary_conditions;
    std::optional<H5::Group> library;
    std::optional<H5::Group> compositions;
    std::optional<H5::Group> operating_state;
    std::optional<H5::Group> solver_options; // optional: nullopt, undiagnosed, when absent
};

// Check 3: the root's required groups, and solver_options when present.
RootGroups open_root_groups(Reporter& reporter, const H5::Group& root) {
    const bool has_solver_options = root.nameExists("solver_options");
    return RootGroups{
        .geometry = open_group(reporter, root, "", "geometry"),
        .boundary_conditions = open_group(reporter, root, "", "boundary_conditions"),
        .library = open_group(reporter, root, "", "library"),
        .compositions = open_group(reporter, root, "", "compositions"),
        .operating_state = open_group(reporter, root, "", "operating_state"),
        .solver_options =
            has_solver_options ? open_group(reporter, root, "", "solver_options") : std::nullopt,
    };
}

// The dataset `name` of `group` (at `path`), or a diagnostic when it is missing or not a dataset.
std::optional<H5::DataSet> open_dataset(Reporter& reporter, const H5::Group& group,
                                        const std::string& path, const std::string& name) {
    const std::string location = child_location(path, name);
    std::optional<H5::DataSet> dataset;
    guarded_read(reporter, location, [&] {
        if (!group.nameExists(name)) {
            reporter.add(location, "dataset is missing");
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

// K, the number of input compositions: the length of /compositions/library_set when that is a
// rank-1 dataset of at least one element. Its diagnostics are the business of check 7, so none
// are reported here; an unknown K leaves the map's upper bound unchecked rather than reporting
// every node against a bound of zero.
std::optional<std::size_t> input_composition_count(const H5::Group& root) {
    try {
        if (!is_child(root, "compositions", H5O_TYPE_GROUP)) {
            return std::nullopt;
        }
        const H5::Group compositions = root.openGroup("compositions");
        if (!is_child(compositions, "library_set", H5O_TYPE_DATASET)) {
            return std::nullopt;
        }
        const H5::DataSet set = compositions.openDataSet("library_set");
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

// The composition map (row-major [nz][ny][nx]). Its values are checked only when the widths
// are usable, since a position's index needs the dimensions. The reactor's positions and
// nodes are filled only when the map is usable; true when it is.
bool read_composition_map(Reporter& reporter, const H5::Group& geometry,
                          std::optional<std::size_t> composition_count, bool widths_usable,
                          ReactorDescription& reactor) {
    const std::string location = "/geometry/composition_map";
    const auto dataset = open_dataset(reporter, geometry, "/geometry", "composition_map");
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

// Which parts of /geometry are usable: the widths make the dimensions known, the map (given the
// widths) makes the nodes known. A check is skipped only when the part it needs is unusable.
struct GeometryUsability {
    bool widths = false;
    bool map = false;
};

// Check 4: /geometry. `composition_count` is K (see input_composition_count).
GeometryUsability read_geometry(Reporter& reporter, const std::optional<H5::Group>& geometry,
                                std::optional<std::size_t> composition_count,
                                ReactorDescription& reactor) {
    if (!geometry) {
        return {};
    }
    check_members(reporter, *geometry, "/geometry", geometry_members);
    const bool width_x_ok =
        read_width(reporter, *geometry, "/geometry", "width_x", reactor.width_x_cm);
    const bool width_y_ok =
        read_width(reporter, *geometry, "/geometry", "width_y", reactor.width_y_cm);
    const bool width_z_ok =
        read_width(reporter, *geometry, "/geometry", "width_z", reactor.width_z_cm);
    const bool widths_usable = width_x_ok && width_y_ok && width_z_ok;
    const bool map_usable =
        read_composition_map(reporter, *geometry, composition_count, widths_usable, reactor);
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

// One face group `name` of /boundary_conditions: its `kind`, and the `albedo` of kind albedo. A
// group whose kind is missing or unknown still has its members checked, against the albedo kind's
// members, so a misspelt member is reported too.
void read_face(Reporter& reporter, const H5::Group& boundary, const std::string& name,
               BoundaryCondition& condition) {
    const std::string parent = "/boundary_conditions";
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

// Check 5: /boundary_conditions. The exterior face is read when required, or when present.
void read_boundary_conditions(Reporter& reporter, const std::optional<H5::Group>& boundary,
                              bool exterior_required, ReactorDescription& reactor) {
    if (!boundary) {
        return;
    }
    check_members(reporter, *boundary, "/boundary_conditions", boundary_members);
    for (std::size_t f = 0; f < face_count; ++f) {
        read_face(reporter, *boundary, std::string(face_name(static_cast<Face>(f))),
                  reactor.bounding_faces[f]);
    }
    if (exterior_required || boundary->nameExists("exterior")) {
        read_face(reporter, *boundary, "exterior", reactor.exterior);
    }
}

// Check 6: /library, and the nodal library it names. `binding.library_file` is the resolved
// path; `library` is filled when the library is read and passes its checks.
void read_library(Reporter& reporter, const std::optional<H5::Group>& group,
                  const std::filesystem::path& input_file, LibraryBinding& binding,
                  std::optional<NodalLibrary>& library) {
    if (!group) {
        return;
    }
    check_members(reporter, *group, "/library", std::span<const std::string_view>{});
    const auto text = read_string_attribute(reporter, *group, "/library", "path");
    if (!text) {
        return;
    }
    if (text->empty()) {
        reporter.add("/library@path", "the library path is empty");
        return;
    }
    const std::filesystem::path path(*text);
    binding.library_file = path.is_absolute() ? path : input_file.parent_path() / path;
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

// /compositions: `library_set` and `library_composition`, each a string [K] with K >= 1. The
// binding's input compositions are filled only when both are usable; true when they are.
bool read_input_compositions(Reporter& reporter, const std::optional<H5::Group>& group,
                             LibraryBinding& binding) {
    const std::string path = "/compositions";
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
// nullopt when it does not resolve, and a diagnostic says why.
std::vector<std::optional<BoundComposition>>
resolve_compositions(Reporter& reporter, const NodalLibrary& library,
                     const std::vector<InputComposition>& inputs) {
    std::vector<std::optional<BoundComposition>> bound;
    EntryReporter entries(reporter, "/compositions");
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

// Check 7: /compositions. The input compositions resolve when the library is read and their
// names are usable. Every input composition is resolved and reported, however many fail; the used
// ones are checked when the map is usable (the nodes are known). Nullopt unless every input
// composition resolves.
std::optional<std::vector<BoundComposition>>
read_compositions(Reporter& reporter, const std::optional<H5::Group>& group,
                  const std::optional<NodalLibrary>& library, const GeometryUsability& geometry,
                  const ReactorDescription& reactor, LibraryBinding& binding) {
    const bool names_ok = read_input_compositions(reporter, group, binding);
    if (!names_ok || !library) {
        return std::nullopt;
    }
    const std::vector<std::optional<BoundComposition>> bound =
        resolve_compositions(reporter, *library, binding.compositions);
    if (geometry.map) {
        check_used_compositions(reporter, *library, reactor, bound);
    }
    std::vector<BoundComposition> complete;
    for (const std::optional<BoundComposition>& entry : bound) {
        if (!entry) {
            return std::nullopt;
        }
        complete.push_back(*entry);
    }
    return complete;
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

// A node-state field of /operating_state (§6): its units and rule.
struct NodeField {
    std::string_view name;
    std::string_view units;
    FloatRule rule;
};

constexpr NodeField doppler_field{
    .name = "doppler_temperature", .units = unit_kelvin, .rule = positive_rule};
constexpr NodeField density_field{
    .name = "moderator_density", .units = unit_kg_per_m3, .rule = positive_rule};
constexpr NodeField moderator_temperature_field{
    .name = "moderator_temperature", .units = unit_kelvin, .rule = positive_rule};
constexpr NodeField boron_field{.name = "boron", .units = unit_ppm, .rule = non_negative_rule};

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

// Check 8: /operating_state. The node states are filled when all four node-state fields are
// usable and the map is usable (the nodes are known).
void read_operating_state(Reporter& reporter, const std::optional<H5::Group>& group,
                          const GeometryUsability& geometry, const ReactorDescription& reactor,
                          OperatingState& state) {
    if (!group) {
        return;
    }
    const std::string path = "/operating_state";
    check_members(reporter, *group, path, operating_state_members);
    const std::optional<double> power = read_thermal_power(reporter, *group, path);
    const auto doppler = read_node_field(reporter, *group, path, doppler_field, geometry, reactor);
    const auto density = read_node_field(reporter, *group, path, density_field, geometry, reactor);
    const auto temperature =
        read_node_field(reporter, *group, path, moderator_temperature_field, geometry, reactor);
    const auto boron = read_node_field(reporter, *group, path, boron_field, geometry, reactor);
    if (power) {
        state.thermal_power_w = *power;
    }
    if (!doppler || !density || !temperature || !boron) {
        return;
    }
    for (std::size_t n = 0; n < doppler->size(); ++n) {
        state.node_states.push_back(NodeState{
            .doppler_temperature_k = (*doppler)[n],
            .moderator_density_kg_m3 = (*density)[n],
            .moderator_temperature_k = (*temperature)[n],
            .boron_ppm = (*boron)[n],
        });
    }
}

// A float64 option of /solver_options (§7): its rule and its member of SolverOptions.
struct FloatOption {
    std::string_view name;
    FloatRule rule;
    double SolverOptions::* member;
};

// An integer option of /solver_options (§7): its member of SolverOptions; at least 1.
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

// Check 9, adjoint (§7): 0 (off) or 1 (on).
void read_adjoint(Reporter& reporter, const H5::Group& group, const std::string& path,
                  SolverOptions& options) {
    if (!group.nameExists("adjoint")) {
        return;
    }
    const std::optional<std::int64_t> value = read_integer_scalar(reporter, group, path, "adjoint");
    if (!value) {
        return;
    }
    if (*value != 0 && *value != 1) {
        reporter.add(child_location(path, "adjoint"),
                     std::format("value {} is not 0 or 1", *value));
        return;
    }
    options.adjoint = *value == 1;
}

// Check 9: /solver_options, optional as a whole; an absent member takes its default. A group
// that is not one is reported at check 3.
void read_solver_options(Reporter& reporter, const std::optional<H5::Group>& group,
                         SolverOptions& options) {
    if (!group) {
        return;
    }
    const std::string path = "/solver_options";
    check_members(reporter, *group, path, solver_option_members);
    read_kernel(reporter, *group, path, options);
    read_float_options(reporter, *group, path, options);
    read_integer_options(reporter, *group, path, options);
    read_adjoint(reporter, *group, path, options);
}

} // namespace

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

        check_members(reporter, root, "", root_members);
        const RootGroups groups = open_root_groups(reporter, root);
        const std::optional<std::size_t> composition_count = input_composition_count(root);
        const GeometryUsability geometry =
            read_geometry(reporter, groups.geometry, composition_count, input.reactor);
        const bool exterior_required =
            geometry.map && input.reactor.node_count() < input.reactor.position_count();
        read_boundary_conditions(reporter, groups.boundary_conditions, exterior_required,
                                 input.reactor);

        std::optional<NodalLibrary> library;
        read_library(reporter, groups.library, input.file, input.binding, library);
        std::optional<std::vector<BoundComposition>> bound = read_compositions(
            reporter, groups.compositions, library, geometry, input.reactor, input.binding);
        read_operating_state(reporter, groups.operating_state, geometry, input.reactor,
                             input.operating_state);
        read_solver_options(reporter, groups.solver_options, input.solver_options);
        if (reporter.count() > 0) {
            return std::unexpected(reporter.take());
        }
        NTS_ASSERT(library && bound,
                   "a problem without diagnostics lacks its library or compositions");
        return Problem{.input = std::move(input),
                       .library = std::move(*library),
                       .compositions = std::move(*bound)};
    } catch (const H5::Exception& exception) {
        reporter.add("/", std::format("HDF5 error in {}: {}", exception.getFuncName(),
                                      exception.getDetailMsg()));
        return std::unexpected(reporter.take());
    }
}

} // namespace nts
