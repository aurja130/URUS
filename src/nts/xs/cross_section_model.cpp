// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/xs/cross_section_model.hpp"

#include "nts/common/invariant.hpp"
#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nts {
namespace {

// The quantities of each energy group, group 0 first.
constexpr std::array<Quantity, energy_group_count> diffusion_quantities{Quantity::d1, Quantity::d2};
constexpr std::array<Quantity, energy_group_count> absorption_quantities{Quantity::sigma_a1,
                                                                         Quantity::sigma_a2};
constexpr std::array<Quantity, energy_group_count> nu_fission_quantities{Quantity::nu_sigma_f1,
                                                                         Quantity::nu_sigma_f2};
constexpr std::array<Quantity, energy_group_count> kappa_fission_quantities{
    Quantity::kappa_sigma_f1, Quantity::kappa_sigma_f2};
constexpr std::array<Quantity, energy_group_count> inverse_velocity_quantities{Quantity::inv_v1,
                                                                               Quantity::inv_v2};
constexpr std::array<Quantity, energy_group_count> fission_quantities{Quantity::sigma_f1,
                                                                      Quantity::sigma_f2};

// A state lies in one interval of each axis, so at most 2^state_axis_count corners
// (TM Eq. (7.2)).
constexpr std::size_t max_corners = std::size_t{1} << state_axis_count;

// The value of `axis` in a node state.
double state_value(const NodeState& state, StateAxis axis) {
    switch (axis) {
    case StateAxis::fuel_temperature:
        return state.doppler_temperature_k;
    case StateAxis::moderator_density:
        return state.moderator_density_kg_m3;
    case StateAxis::moderator_temperature:
        return state.moderator_temperature_k;
    case StateAxis::boron:
        return state.boron_ppm;
    }
    NTS_ASSERT(false, "unknown state axis");
    return 0.0;
}

// The interpolation coordinate of a value on `axis`: √T_D on the fuel temperature axis and the
// value itself on the others (TM §7.2). Values are positive where the square root is taken.
double coordinate(StateAxis axis, double value) {
    return axis == StateAxis::fuel_temperature ? std::sqrt(value) : value;
}

// The weights of the two points of one axis that bracket a state value (TM Eq. (7.2)). Both
// indices name the same point when the value is clamped or the axis has a single point, and
// then one weight is zero.
struct AxisWeights {
    std::array<std::size_t, 2> point{};
    std::array<double, 2> weight{};
    ClampSide side = ClampSide::none;
};

AxisWeights axis_weights(const AxisPoints& axis, double value) {
    const std::vector<double>& points = axis.points;
    NTS_ASSERT(!points.empty(), "a state axis has at least one point");
    NTS_ASSERT(!std::isnan(value), "a node state value is a number");
    const std::size_t last = points.size() - 1;
    if (std::isless(value, points.front())) {
        return {.point = {0, 0}, .weight = {1.0, 0.0}, .side = ClampSide::below};
    }
    if (std::isgreater(value, points.back())) {
        return {.point = {last, last}, .weight = {0.0, 1.0}, .side = ClampSide::above};
    }
    if (last == 0) {
        return {.point = {0, 0}, .weight = {1.0, 0.0}, .side = ClampSide::none};
    }
    // points.front() <= value <= points.back() and the points are strictly ascending, so the
    // first point above value has index 1 or more; the last interval closes on the last point.
    const auto first_above = std::ranges::upper_bound(points, value);
    const std::size_t upper = static_cast<std::size_t>(first_above - points.begin());
    const std::size_t lower = std::min(upper, last) - 1;
    const double lower_coordinate = coordinate(axis.axis, points[lower]);
    const double width = coordinate(axis.axis, points[lower + 1]) - lower_coordinate;
    // Distinct points can share a coordinate after the square root rounds; such an interval has
    // no width, and its value is weighted to the lower point.
    const double t = width > 0.0 ? (coordinate(axis.axis, value) - lower_coordinate) / width : 0.0;
    return {.point = {lower, lower + 1}, .weight = {1.0 - t, t}, .side = ClampSide::none};
}

// One corner of the box around a state: a state index of the composition set and its weight.
struct Corner {
    std::size_t state = 0;
    double weight = 0.0;
};

// The corners of nonzero weight.
struct Corners {
    std::array<Corner, max_corners> corner{};
    std::size_t count = 0;
};

// The multilinear corners of a state over the axes of `set` (TM Eq. (7.2)); the state index is
// row-major with the last axis fastest.
Corners corners_of(const CompositionSet& set, const std::array<AxisWeights, state_axis_count>& w) {
    Corners corners;
    const std::size_t axis_count = set.axes.size();
    for (std::size_t mask = 0; mask < (std::size_t{1} << axis_count); ++mask) {
        Corner corner{.state = 0, .weight = 1.0};
        for (std::size_t a = 0; a < axis_count; ++a) {
            const std::size_t end = (mask >> a) & std::size_t{1};
            const std::size_t point_count = set.axes[a].points.size();
            corner.state = (corner.state * point_count) + w[a].point[end];
            corner.weight *= w[a].weight[end];
        }
        if (corner.weight > 0.0) {
            corners.corner[corners.count] = corner;
            ++corners.count;
        }
    }
    return corners;
}

// The index of one entry of a composition set (contract §3.3 layout).
std::size_t entry_index(const CompositionSet& set, std::size_t composition, std::size_t state,
                        std::size_t components, std::size_t component) {
    return (((composition * set.state_count) + state) * components) + component;
}

// The table of a quantity that a composition set holds; the reader guarantees the table of every
// quantity the model uses.
const QuantityTable& required_table(const CompositionSet& set, Quantity quantity) {
    const QuantityTable* table = set.find(quantity);
    NTS_ASSERT(table != nullptr, "a required quantity is in the composition set");
    return *table;
}

// The multilinear combination of one component of a quantity at the corners (TM Eq. (7.2)).
double combine(const CompositionSet& set, Quantity quantity, std::size_t composition,
               std::size_t component, const Corners& corners) {
    const QuantityTable& table = required_table(set, quantity);
    const std::size_t components = quantity_spec(quantity).components;
    double sum = 0.0;
    for (std::size_t i = 0; i < corners.count; ++i) {
        const Corner& corner = corners.corner[i];
        const std::size_t entry =
            entry_index(set, composition, corner.state, components, component);
        sum += corner.weight * table.value[entry];
    }
    return sum;
}

// The diffusion coefficient D_g (TM §7.1): the table holds D, and 1/(3D) is the quantity that is
// interpolated over the corners (TM Eq. (7.2)) before D is recovered from it.
double interpolated_diffusion(const CompositionSet& set, Quantity quantity, std::size_t composition,
                              const Corners& corners) {
    const QuantityTable& table = required_table(set, quantity);
    double inverse_sum = 0.0;
    for (std::size_t i = 0; i < corners.count; ++i) {
        const Corner& corner = corners.corner[i];
        const std::size_t entry = entry_index(set, composition, corner.state, 1, 0);
        inverse_sum += corner.weight / (3.0 * table.value[entry]);
    }
    NTS_ASSERT(inverse_sum > 0.0, "the corner weights and the diffusion coefficients are positive");
    return 1.0 / (3.0 * inverse_sum);
}

// Fills every field of one node from the corners of its state (TM §7.1, §7.2).
void fill_cross_sections(const CompositionSet& set, std::size_t composition, const Corners& corners,
                         NodeCrossSections& node) {
    for (std::size_t g = 0; g < energy_group_count; ++g) {
        node.diffusion_cm[g] =
            interpolated_diffusion(set, diffusion_quantities[g], composition, corners);
        node.absorption_per_cm[g] = combine(set, absorption_quantities[g], composition, 0, corners);
        node.nu_fission_per_cm[g] = combine(set, nu_fission_quantities[g], composition, 0, corners);
        node.kappa_fission_j_per_cm[g] =
            combine(set, kappa_fission_quantities[g], composition, 0, corners);
        node.inverse_velocity_s_per_cm[g] =
            combine(set, inverse_velocity_quantities[g], composition, 0, corners);
    }
    // TM Eq. (3.5): the effective down-scatter is stored; Σ_21 is not used by the model.
    node.down_scatter_per_cm = combine(set, Quantity::sigma_s12, composition, 0, corners);
    node.removal_per_cm[0] = node.absorption_per_cm[0] + node.down_scatter_per_cm;
    node.removal_per_cm[1] = node.absorption_per_cm[1];
    for (std::size_t k = 0; k < delayed_group_count; ++k) {
        node.delayed_fraction[k] = combine(set, Quantity::beta, composition, k, corners);
        node.decay_constant_per_s[k] = combine(set, Quantity::lambda, composition, k, corners);
    }
    node.discontinuity_factor.fill(1.0);
    if (set.find(Quantity::discontinuity_factor) != nullptr) {
        for (std::size_t c = 0; c < face_count * energy_group_count; ++c) {
            node.discontinuity_factor[c] =
                combine(set, Quantity::discontinuity_factor, composition, c, corners);
        }
    }
    // Σ_f is optional for both groups together; a lone table is not used.
    if (set.find(Quantity::sigma_f1) != nullptr && set.find(Quantity::sigma_f2) != nullptr) {
        std::array<double, energy_group_count> fission{};
        for (std::size_t g = 0; g < energy_group_count; ++g) {
            fission[g] = combine(set, fission_quantities[g], composition, 0, corners);
        }
        node.fission_per_cm = fission;
    }
}

// Counts the clamped axes of one node into the totals (TM §7.2).
void count_clamps(const StateClamps& clamps, ClampCounts& totals) {
    for (std::size_t a = 0; a < state_axis_count; ++a) {
        if (clamps[a] == ClampSide::below) {
            ++totals[a][0];
        } else if (clamps[a] == ClampSide::above) {
            ++totals[a][1];
        }
    }
}

} // namespace

InterpolatedComposition interpolate_composition(const CompositionSet& set, std::size_t composition,
                                                const NodeState& state) {
    NTS_ASSERT(composition < set.composition_count(), "the composition is in the set");
    NTS_ASSERT(set.axes.size() <= state_axis_count, "a composition set has at most four axes");
    std::array<AxisWeights, state_axis_count> weights{};
    InterpolatedComposition result;
    for (std::size_t a = 0; a < set.axes.size(); ++a) {
        weights[a] = axis_weights(set.axes[a], state_value(state, set.axes[a].axis));
        result.clamps[static_cast<std::size_t>(set.axes[a].axis)] = weights[a].side;
    }
    const Corners corners = corners_of(set, weights);
    fill_cross_sections(set, composition, corners, result.cross_sections);
    return result;
}

CrossSectionField evaluate_cross_sections(const Problem& problem) {
    const ReactorDescription& reactor = problem.input.reactor;
    const std::vector<NodeState>& states = problem.input.operating_state.node_states;
    NTS_ASSERT(states.size() == reactor.node_count(), "there is one node state per node");

    CrossSectionField field;
    field.nodes.reserve(reactor.node_count());
    field.clamps.reserve(reactor.node_count());
    for (std::size_t node = 0; node < reactor.node_count(); ++node) {
        const std::size_t position = reactor.node_positions[node];
        NTS_ASSERT(reactor.composition_map[position] >= 0, "a node position holds a composition");
        const auto input_index = static_cast<std::size_t>(reactor.composition_map[position]);
        NTS_ASSERT(input_index < problem.compositions.size(), "the input composition is bound");
        const BoundComposition& bound = problem.compositions[input_index];
        NTS_ASSERT(bound.set < problem.library.composition_sets.size(),
                   "the set is in the library");
        const InterpolatedComposition interpolated = interpolate_composition(
            problem.library.composition_sets[bound.set], bound.composition, states[node]);
        field.nodes.push_back(interpolated.cross_sections);
        field.clamps.push_back(interpolated.clamps);
        count_clamps(interpolated.clamps, field.clamp_counts);
    }
    return field;
}

} // namespace nts
