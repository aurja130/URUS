// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#pragma once

// The nodal library in memory, read and validated under the library contract
// (docs/contracts/nodal_library.md, format version 1).

#include "nts/common/diagnostic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nts {

// The state-axis vocabulary of contract §3.1, in its dimension order.
enum class StateAxis : std::uint8_t {
    fuel_temperature,
    moderator_density,
    moderator_temperature,
    boron
};
inline constexpr std::size_t state_axis_count = 4;

[[nodiscard]] std::string_view state_axis_name(StateAxis axis);
[[nodiscard]] std::string_view state_axis_units(StateAxis axis);

// The quantities of contract §3.3; the enumerator is the index into quantity_specs().
enum class Quantity : std::uint8_t {
    d1,
    d2,
    sigma_a1,
    sigma_a2,
    sigma_s12,
    sigma_s21,
    nu_sigma_f1,
    nu_sigma_f2,
    kappa_sigma_f1,
    kappa_sigma_f2,
    inv_v1,
    inv_v2,
    beta,
    lambda,
    sigma_f1,
    sigma_f2,
    discontinuity_factor
};
inline constexpr std::size_t quantity_count = 17;
inline constexpr std::size_t delayed_group_count = 8;
inline constexpr std::size_t face_count = 6;
inline constexpr std::size_t energy_group_count = 2;

// The entry rules of contract §3.3. Every value must also be finite.
enum class EntryRule : std::uint8_t {
    positive,         // x > 0
    non_negative,     // x >= 0
    delayed_fraction, // every component >= 0, their sum < 1
    decay_constant    // every component >= 0, and > 0 where the entry's beta component is > 0
};

struct QuantitySpec {
    Quantity quantity;
    std::string_view name;  // HDF5 group name
    std::string_view units; // the one accepted `units` attribute
    std::size_t components; // 1; 8 for beta and lambda; 6 faces x 2 groups for the factors
    bool required;
    EntryRule rule;
};

[[nodiscard]] std::span<const QuantitySpec, quantity_count> quantity_specs();
[[nodiscard]] const QuantitySpec& quantity_spec(Quantity quantity);

struct AxisPoints {
    StateAxis axis;
    std::vector<double> points;
};

// Layout: ((composition * state_count + state) * components + component).
struct QuantityTable {
    std::vector<double> value;
    std::vector<double> std_dev;
};

struct CompositionSet {
    std::string name;
    std::vector<AxisPoints> axes; // vocabulary order, only the axes present
    std::vector<std::string> composition_names;
    // Product of the axis lengths; a state index is row-major over the axes, last fastest.
    std::size_t state_count = 1;
    std::vector<std::uint8_t> valid; // [composition * state_count + state]
    std::array<std::optional<QuantityTable>, quantity_count> quantities; // nullopt: absent

    [[nodiscard]] std::size_t composition_count() const;
    [[nodiscard]] const QuantityTable* find(Quantity quantity) const;
};

struct NodalLibrary {
    std::filesystem::path file;
    std::int64_t format_version = 0;
    double group_boundary_ev = 0.0;
    std::string generator;
    std::string generator_commit;
    std::vector<CompositionSet> composition_sets; // sorted by name
};

// Reads the whole library and checks contract §4, items 1-5. All problems are returned
// together.
[[nodiscard]] std::expected<NodalLibrary, Diagnostics>
read_nodal_library(const std::filesystem::path& file);

// The entry text of contract §5, e.g. "composition 2 'reflector', fuel_temperature[1] = 900 K".
[[nodiscard]] std::string describe_entry(const CompositionSet& set, std::size_t composition,
                                         std::size_t state);

// "delayed group 3" (beta, lambda), "face x-, group 1" (discontinuity factor), "" otherwise.
[[nodiscard]] std::string describe_component(Quantity quantity, std::size_t component);

// True when the value of `quantity` in the entry breaks its rule in some component; false
// for an absent quantity.
[[nodiscard]] bool breaks_entry_rule(const CompositionSet& set, Quantity quantity,
                                     std::size_t composition, std::size_t state);

// Per quantity, the entries (any validity flag) whose value breaks the rule.
[[nodiscard]] std::array<std::size_t, quantity_count>
count_rule_violations(const CompositionSet& set);

// Contract §4, per composition in use: a diagnostic for each invalid entry of a listed
// composition (at most 20, then one giving the number not listed) and for an index outside
// the set. Empty when every listed composition is usable.
[[nodiscard]] Diagnostics check_compositions_usable(const NodalLibrary& library,
                                                    const CompositionSet& set,
                                                    std::span<const std::size_t> compositions);

} // namespace nts
