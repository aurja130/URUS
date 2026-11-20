// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

// Verification benchmark "Cross-section table interpolation" (TM §7.2 Eq. (7.2), §7.4; Eq. (3.5)
// for the removal and down-scatter cross sections).
//
// Reference. (1) The table itself: at a grid state every weight of the multilinear sum is 0 or 1,
// so the interpolant returns the stored value. (2) Data f that is multilinear in
// (√T_D, ρ_m, T_m, C_B) (multilinear in 1/(3D_g) for the diffusion coefficients): a piecewise
// multilinear interpolant of values sampled from f reproduces f exactly, so only round-off
// remains. The designed functions are sums of the monomials 1, u, ρ, τ, b, uρ, ub, ρτb, and
// every monomial is affine in each coordinate separately.
//
// Tolerance. 1e-14 relative for every table point and every multilinear-data point. A
// multilinear evaluation of 2^4 = 16 corners sums positive weights times positive values; each
// product and sum rounds by at most 2.2e-16 relative, so the error is a few machine epsilons
// and 1e-14 is far above round-off yet far below any wrong weight. Clamp counts and sides are
// integers and compared exactly. Diffusion coefficients: the table holds D = 1/(3 f); the
// comparison is on D itself, so the tolerance covers the two reciprocals as well.

#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "library_fixture.hpp"
#include "problem_fixture.hpp"
#include "problem_test_support.hpp"

namespace {

using nts::ClampSide;
using nts::NodeCrossSections;
using nts::NodeState;
using nts::StateClamps;
using nts::test::entry_value;
using nts::test::FixtureAxis;
using nts::test::FixtureLibrary;
using nts::test::FixtureSet;

constexpr std::size_t group_count = nts::energy_group_count;
constexpr std::size_t delayed_count = nts::delayed_group_count;
constexpr std::size_t face_components = nts::face_count * nts::energy_group_count;
constexpr std::size_t axis_count = nts::state_axis_count;
constexpr std::size_t composition_count = 2;
constexpr std::size_t fuel_composition = 0;
constexpr std::size_t reflector_composition = 1;
constexpr std::size_t monomial_count = 8;
constexpr std::size_t position_count = 12; // 2 x 2 x 3
constexpr std::size_t random_state_count = 200;
constexpr std::uint64_t random_seed = 20261119U;
constexpr double relative_tolerance = 1.0e-14;
constexpr double diffusion_gap = 1.0e-6; // relative separation of 1/(3D) from linear D
constexpr double up_scatter = 1.0;       // Σ_21 in every entry; large enough to show a misuse

constexpr ClampSide no_clamp = ClampSide::none;
constexpr ClampSide clamped_below = ClampSide::below;
constexpr ClampSide clamped_above = ClampSide::above;

// Axis bounds of the four-axis library (contract §3.1 order).
constexpr double fuel_low = 500.0;
constexpr double fuel_high = 1200.0;
constexpr double density_low = 600.0;
constexpr double density_high = 750.0;
constexpr double moderator_low = 550.0;
constexpr double moderator_high = 600.0;
constexpr double boron_low = 0.0;
constexpr double boron_high = 1500.0;

// Values of the coordinates of an axis the set lacks (it never clamps and never varies).
constexpr double nominal_fuel = 750.0;
constexpr double nominal_density = 700.0;
constexpr double nominal_moderator = 575.0;
constexpr double nominal_boron = 500.0;

// ----- The designed functions ---------------------------------------------------------------

// Every quantity family of the library; the enumerator indexes families.
enum class Family : std::uint8_t {
    d1,
    d2,
    sigma_a1,
    sigma_a2,
    sigma_s12,
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

struct FamilySpec {
    std::string_view name;
    std::size_t components;
    double amplitude; // scale of the designed function, in the table's units
    double offset;    // added to it
    bool diffusion;   // the table holds D = 1/(3 f), f being the interpolated 1/(3D)
};

constexpr std::array<FamilySpec, 16> families{{
    {.name = "D1", .components = 1, .amplitude = 0.02, .offset = 0.0, .diffusion = true},
    {.name = "D2", .components = 1, .amplitude = 0.05, .offset = 0.0, .diffusion = true},
    {.name = "Sigma_a1", .components = 1, .amplitude = 0.001, .offset = 0.0, .diffusion = false},
    {.name = "Sigma_a2", .components = 1, .amplitude = 0.005, .offset = 0.0, .diffusion = false},
    {.name = "Sigma_s12", .components = 1, .amplitude = 0.0015, .offset = 0.0, .diffusion = false},
    {.name = "nu_Sigma_f1",
     .components = 1,
     .amplitude = 0.0005,
     .offset = 0.0,
     .diffusion = false},
    {.name = "nu_Sigma_f2", .components = 1, .amplitude = 0.01, .offset = 0.0, .diffusion = false},
    {.name = "kappa_Sigma_f1",
     .components = 1,
     .amplitude = 1.0e-13,
     .offset = 0.0,
     .diffusion = false},
    {.name = "kappa_Sigma_f2",
     .components = 1,
     .amplitude = 1.0e-13,
     .offset = 0.0,
     .diffusion = false},
    {.name = "inv_v1", .components = 1, .amplitude = 4.0e-9, .offset = 0.0, .diffusion = false},
    {.name = "inv_v2", .components = 1, .amplitude = 2.0e-7, .offset = 0.0, .diffusion = false},
    {.name = "beta",
     .components = delayed_count,
     .amplitude = 1.0e-3,
     .offset = 0.0,
     .diffusion = false},
    {.name = "lambda",
     .components = delayed_count,
     .amplitude = 0.1,
     .offset = 0.0,
     .diffusion = false},
    {.name = "Sigma_f1", .components = 1, .amplitude = 0.0005, .offset = 0.0, .diffusion = false},
    {.name = "Sigma_f2", .components = 1, .amplitude = 0.005, .offset = 0.0, .diffusion = false},
    {.name = "discontinuity_factor",
     .components = face_components,
     .amplitude = 0.01,
     .offset = 1.0,
     .diffusion = false},
}};

// A state of the four coordinates (√T_D, ρ_m, T_m, C_B).
struct Point {
    double u;
    double rho;
    double tau;
    double b;
};

// The monomials 1, u, ρ, τ, b, uρ, ub, ρτb, each divided by its typical size so that every
// coefficient of the designed function is O(amplitude).
constexpr std::array<double, monomial_count> monomial_scale{1.0,    30.0,  700.0, 575.0,
                                                            1500.0, 2.1e4, 4.5e4, 6.0375e8};

std::array<double, monomial_count> monomials(const Point& s) {
    return {1.0, s.u, s.rho, s.tau, s.b, s.u * s.rho, s.u * s.b, s.rho * s.tau * s.b};
}

const FamilySpec& family_spec(Family family) {
    return families[static_cast<std::size_t>(family)];
}

// The designed interpolated quantity f of a component, for a composition. The weights differ
// per family, component and composition, and are all positive, so f is positive everywhere.
double designed(Family family, std::size_t component, std::size_t composition, const Point& s) {
    const FamilySpec& spec = family_spec(family);
    const std::array<double, monomial_count> m = monomials(s);
    double value = spec.offset;
    for (std::size_t i = 0; i < monomial_count; ++i) {
        // 13 is prime and above the 12 face components, so 3 * component differs modulo 13 for
        // every pair of components of a family: no two components share a designed function.
        const std::size_t phase =
            i + (3 * component) + (2 * composition) + static_cast<std::size_t>(family);
        const double weight = 1.0 + (0.0625 * static_cast<double>(phase % 13));
        value += spec.amplitude * weight * (m[i] / monomial_scale[i]);
    }
    return value;
}

// The table value of a component: D = 1/(3 f) for the diffusion families, f otherwise.
double stored(Family family, std::size_t component, std::size_t composition, const Point& s) {
    const double value = designed(family, component, composition, s);
    return family_spec(family).diffusion ? 1.0 / (3.0 * value) : value;
}

Point point_of(const NodeState& node) {
    return {.u = std::sqrt(node.doppler_temperature_k),
            .rho = node.moderator_density_kg_m3,
            .tau = node.moderator_temperature_k,
            .b = node.boron_ppm};
}

// The four-axis clamp of a node state: each coordinate to its nearest end point (TM §7.2).
Point clamped_point(const NodeState& node) {
    return {.u = std::sqrt(std::clamp(node.doppler_temperature_k, fuel_low, fuel_high)),
            .rho = std::clamp(node.moderator_density_kg_m3, density_low, density_high),
            .tau = std::clamp(node.moderator_temperature_k, moderator_low, moderator_high),
            .b = std::clamp(node.boron_ppm, boron_low, boron_high)};
}

NodeState state_of(double doppler_k, double density, double moderator_k, double boron) {
    return {.doppler_temperature_k = doppler_k,
            .moderator_density_kg_m3 = density,
            .moderator_temperature_k = moderator_k,
            .boron_ppm = boron};
}

// The node state of a row-major state index of `set` (last axis fastest). An axis the set
// lacks holds its nominal value.
NodeState node_of_state(const FixtureSet& set, std::size_t state) {
    NodeState node = state_of(nominal_fuel, nominal_density, nominal_moderator, nominal_boron);
    std::size_t rest = state;
    for (const FixtureAxis& axis : std::views::reverse(set.axes)) {
        const std::size_t count = axis.points.size();
        const double value = axis.points[rest % count];
        rest /= count;
        if (axis.name == "fuel_temperature") {
            node.doppler_temperature_k = value;
        } else if (axis.name == "moderator_density") {
            node.moderator_density_kg_m3 = value;
        } else if (axis.name == "moderator_temperature") {
            node.moderator_temperature_k = value;
        } else {
            node.boron_ppm = value;
        }
    }
    return node;
}

std::size_t state_index(const FixtureSet& set, const std::array<std::size_t, axis_count>& index) {
    std::size_t state = 0;
    for (std::size_t a = 0; a < set.axes.size(); ++a) {
        state = (state * set.axes[a].points.size()) + index[a];
    }
    return state;
}

std::vector<FixtureAxis> four_axes() {
    return {{.name = "fuel_temperature", .units = "K", .points = {500.0, 600.0, 900.0, 1200.0}},
            {.name = "moderator_density", .units = "kg m^-3", .points = {600.0, 650.0, 750.0}},
            {.name = "moderator_temperature", .units = "K", .points = {550.0, 600.0}},
            {.name = "boron", .units = "ppm", .points = {0.0, 400.0, 1500.0}}};
}

// The library of `axes` (contract order) with every entry of both compositions set from the
// designed functions; Σ_21 is set to up_scatter, which the model must not use.
FixtureLibrary designed_library(std::vector<FixtureAxis> axes) {
    FixtureLibrary library =
        nts::test::make_valid_fixture_on_axes(std::move(axes), composition_count);
    FixtureSet& set = library.sets.front();
    for (std::size_t c = 0; c < composition_count; ++c) {
        for (std::size_t state = 0; state < set.state_count(); ++state) {
            const Point s = point_of(node_of_state(set, state));
            for (std::size_t f = 0; f < families.size(); ++f) {
                const auto family = static_cast<Family>(f);
                const FamilySpec& spec = families[f];
                for (std::size_t k = 0; k < spec.components; ++k) {
                    entry_value(set, std::string(spec.name), c, state, k) = stored(family, k, c, s);
                }
            }
            entry_value(set, "Sigma_s21", c, state) = up_scatter;
        }
    }
    return library;
}

// A lookup of the value of a component of a family.
using Lookup = std::function<double(Family, std::size_t)>;

Lookup designed_at(std::size_t composition, const Point& s) {
    return [composition, s](Family family, std::size_t component) {
        return stored(family, component, composition, s);
    };
}

Lookup table_at(FixtureSet& set, std::size_t composition, std::size_t state) {
    return [&set, composition, state](Family family, std::size_t component) {
        return entry_value(set, std::string(family_spec(family).name), composition, state,
                           component);
    };
}

// The cross sections of a node from the stored values `value`, written from TM §§3.1, 7.2.
NodeCrossSections expected_node(const Lookup& value) {
    constexpr std::array<Family, group_count> diffusion{Family::d1, Family::d2};
    constexpr std::array<Family, group_count> absorption{Family::sigma_a1, Family::sigma_a2};
    constexpr std::array<Family, group_count> nu_fission{Family::nu_sigma_f1, Family::nu_sigma_f2};
    constexpr std::array<Family, group_count> kappa{Family::kappa_sigma_f1, Family::kappa_sigma_f2};
    constexpr std::array<Family, group_count> fission{Family::sigma_f1, Family::sigma_f2};
    constexpr std::array<Family, group_count> inverse_velocity{Family::inv_v1, Family::inv_v2};

    NodeCrossSections expected{};
    std::array<double, group_count> sigma_f{};
    for (std::size_t g = 0; g < group_count; ++g) {
        expected.diffusion_cm[g] = value(diffusion[g], 0);
        expected.absorption_per_cm[g] = value(absorption[g], 0);
        expected.nu_fission_per_cm[g] = value(nu_fission[g], 0);
        expected.kappa_fission_j_per_cm[g] = value(kappa[g], 0);
        expected.inverse_velocity_s_per_cm[g] = value(inverse_velocity[g], 0);
        sigma_f[g] = value(fission[g], 0);
    }
    expected.down_scatter_per_cm = value(Family::sigma_s12, 0);
    // Σ_r1 = Σ_a1 + Σ_12 and Σ_r2 = Σ_a2 (TM Eq. (3.5)).
    expected.removal_per_cm = {expected.absorption_per_cm[0] + expected.down_scatter_per_cm,
                               expected.absorption_per_cm[1]};
    expected.fission_per_cm = sigma_f;
    for (std::size_t k = 0; k < delayed_count; ++k) {
        expected.delayed_fraction[k] = value(Family::beta, k);
        expected.decay_constant_per_s[k] = value(Family::lambda, k);
    }
    for (std::size_t i = 0; i < face_components; ++i) {
        expected.discontinuity_factor[i] = value(Family::discontinuity_factor, i);
    }
    return expected;
}

// Deterministic interior states, uniform over each axis of the four-axis library.
std::vector<NodeState> random_states() {
    std::mt19937_64 rng(random_seed); // NOLINT(bugprone-random-generator-seed): reproducible states
    std::uniform_real_distribution<double> doppler(fuel_low, fuel_high);
    std::uniform_real_distribution<double> density(density_low, density_high);
    std::uniform_real_distribution<double> moderator(moderator_low, moderator_high);
    std::uniform_real_distribution<double> boron(boron_low, boron_high);
    std::vector<NodeState> states;
    states.reserve(random_state_count);
    for (std::size_t i = 0; i < random_state_count; ++i) {
        const double t = doppler(rng);
        const double rho = density(rng);
        const double tm = moderator(rng);
        const double b = boron(rng);
        states.push_back(state_of(t, rho, tm, b));
    }
    return states;
}

// Interpolated values agree with the reference to relative_tolerance. The weights and a sum of
// at most 16 positive terms carry a few ulp of rounding, far below this tolerance.
void expect_close(double actual, double expected) {
    EXPECT_NEAR(actual, expected, relative_tolerance * std::abs(expected));
}

void expect_node_near(const NodeCrossSections& actual, const NodeCrossSections& expected) {
    for (std::size_t g = 0; g < group_count; ++g) {
        expect_close(actual.diffusion_cm[g], expected.diffusion_cm[g]);
        expect_close(actual.absorption_per_cm[g], expected.absorption_per_cm[g]);
        expect_close(actual.removal_per_cm[g], expected.removal_per_cm[g]);
        expect_close(actual.nu_fission_per_cm[g], expected.nu_fission_per_cm[g]);
        expect_close(actual.kappa_fission_j_per_cm[g], expected.kappa_fission_j_per_cm[g]);
        expect_close(actual.inverse_velocity_s_per_cm[g], expected.inverse_velocity_s_per_cm[g]);
    }
    expect_close(actual.down_scatter_per_cm, expected.down_scatter_per_cm);
    for (std::size_t k = 0; k < delayed_count; ++k) {
        expect_close(actual.delayed_fraction[k], expected.delayed_fraction[k]);
        expect_close(actual.decay_constant_per_s[k], expected.decay_constant_per_s[k]);
    }
    for (std::size_t i = 0; i < face_components; ++i) {
        expect_close(actual.discontinuity_factor[i], expected.discontinuity_factor[i]);
    }
    ASSERT_EQ(actual.fission_per_cm.has_value(), expected.fission_per_cm.has_value());
    if (expected.fission_per_cm && actual.fission_per_cm) {
        for (std::size_t g = 0; g < group_count; ++g) {
            expect_close((*actual.fission_per_cm)[g], (*expected.fission_per_cm)[g]);
        }
    }
}

void expect_no_clamps(const StateClamps& clamps) {
    EXPECT_EQ(clamps, StateClamps{});
}

// ----- The problem of the clamp counts (TM §7.4 step 4) ------------------------------------

// A node: its position in the row-major [z][y][x] arrays, its node state, the expected sides
// per axis and its input composition. Rows are listed in node order (position order, excluded
// positions 0 and 6 skipped).
struct NodeRow {
    std::size_t position;
    NodeState state;
    std::array<ClampSide, axis_count> clamps;
    std::size_t composition;
};

std::vector<NodeRow> node_rows() {
    return {
        {.position = 1,
         .state = state_of(450.0, 650.0, 575.0, 500.0),
         .clamps = {clamped_below, no_clamp, no_clamp, no_clamp},
         .composition = fuel_composition},
        {.position = 2,
         .state = state_of(1300.0, 600.0, 550.0, 1500.0),
         .clamps = {clamped_above, no_clamp, no_clamp, no_clamp},
         .composition = fuel_composition},
        {.position = 3,
         .state = state_of(500.0, 800.0, 540.0, 0.0),
         .clamps = {no_clamp, clamped_above, clamped_below, no_clamp},
         .composition = fuel_composition},
        {.position = 4,
         .state = state_of(600.0, 599.0, 600.0, 1600.0),
         .clamps = {no_clamp, clamped_below, no_clamp, clamped_above},
         .composition = fuel_composition},
        {.position = 5,
         .state = state_of(1000.0, 750.0, 601.0, 250.0),
         .clamps = {no_clamp, no_clamp, clamped_above, no_clamp},
         .composition = fuel_composition},
        {.position = 7,
         .state = state_of(1200.0, 600.0, 575.0, 0.0),
         .clamps = {no_clamp, no_clamp, no_clamp, no_clamp},
         .composition = fuel_composition},
        {.position = 8,
         .state = state_of(450.0, 800.0, 500.0, 2000.0),
         .clamps = {clamped_below, clamped_above, clamped_below, clamped_above},
         .composition = fuel_composition},
        {.position = 9,
         .state = state_of(500.0, 650.0, 550.0, 1500.0),
         .clamps = {no_clamp, no_clamp, no_clamp, no_clamp},
         .composition = fuel_composition},
        {.position = 10,
         .state = state_of(700.0, 680.0, 570.0, 1501.0),
         .clamps = {no_clamp, no_clamp, no_clamp, clamped_above},
         .composition = fuel_composition},
        {.position = 11,
         .state = state_of(50.0, 700.0, 580.0, 300.0),
         .clamps = {clamped_below, no_clamp, no_clamp, no_clamp},
         .composition = reflector_composition},
    };
}

// Writes the node states of `rows` as the initial state of the problem, with the grid of
// the valid problem: positions 0 and 6 are excluded and hold NaN, which is never read. The
// rows include states outside the tables, so the settings allow table clamps.
void use_node_rows(nts::test::FixtureGroup& problem_root, const std::vector<NodeRow>& rows) {
    constexpr std::size_t nz = 2;
    constexpr std::size_t ny = 2;
    constexpr std::size_t nx = 3;
    static_assert(nz * ny * nx == position_count);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> doppler(position_count, nan);
    std::vector<double> density(position_count, nan);
    std::vector<double> moderator(position_count, nan);
    std::vector<double> boron(position_count, nan);
    for (const NodeRow& row : rows) {
        doppler[row.position] = row.state.doppler_temperature_k;
        density[row.position] = row.state.moderator_density_kg_m3;
        moderator[row.position] = row.state.moderator_temperature_k;
        boron[row.position] = row.state.boron_ppm;
    }
    const std::vector<std::size_t> shape{nz, ny, nx};
    nts::test::FixtureGroup& state = problem_root.groups.at("initial_state");
    state.datasets["doppler_temperature"] = nts::test::float64_array(doppler, shape, "K");
    state.datasets["moderator_density"] = nts::test::float64_array(density, shape, "kg m^-3");
    state.datasets["moderator_temperature"] = nts::test::float64_array(moderator, shape, "K");
    state.datasets["boron"] = nts::test::float64_array(boron, shape, "ppm");
    nts::test::FixtureGroup& settings = problem_root.groups["settings"];
    settings.attributes["document"] = std::string{"settings"};
    settings.attributes["format_version"] = std::int64_t{2};
    settings.datasets["allow_table_clamps"] = nts::test::integer_scalar(1);
}

class CrossSectionInterpolationBenchmark : public nts::test::ProblemTest {
protected:
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(xs_directory_, ignored);
        ProblemTest::TearDown();
    }

    // Writes `fixture` as the library, reads it back and keeps its one composition set.
    void load_library(FixtureLibrary fixture) {
        std::filesystem::create_directories(xs_directory_);
        nts::test::write_fixture(fixture, library_path());
        fixture_ = std::move(fixture);
        auto result = nts::read_nodal_library(library_path());
        ASSERT_TRUE(result.has_value())
            << (result ? std::string{} : nts::test::all_text(result.error()));
        ASSERT_EQ(result->composition_sets.size(), 1U);
        set_ = std::move(result->composition_sets.front());
    }

    [[nodiscard]] std::filesystem::path library_path() const {
        return xs_directory_ / "library.h5";
    }

    [[nodiscard]] FixtureLibrary& fixture() { return fixture_; }
    [[nodiscard]] const nts::CompositionSet& composition_set() const { return set_; }

private:
    FixtureLibrary fixture_;
    nts::CompositionSet set_;
    std::filesystem::path xs_directory_ = [] {
        std::string name =
            std::format("nts_xs_{}_{}",
                        testing::UnitTest::GetInstance()->current_test_info()->name(), getpid());
        std::ranges::replace(name, '/', '_'); // parameterized test names hold '/'
        return std::filesystem::temp_directory_path() / name;
    }();
};

} // namespace

// ----- Table points and multilinear data ----------------------------------------------------

// The reference itself: within each family and composition, no two components share a designed
// value, so a component swap in the model cannot pass the benchmark.
TEST(CrossSectionInterpolationReference, ComponentsOfAFamilyAreDistinct) {
    constexpr Point sample{.u = 28.0, .rho = 700.0, .tau = 575.0, .b = 1000.0};
    for (std::size_t f = 0; f < families.size(); ++f) {
        const auto family = static_cast<Family>(f);
        for (std::size_t composition = 0; composition < composition_count; ++composition) {
            for (std::size_t a = 0; a < families[f].components; ++a) {
                for (std::size_t b = a + 1; b < families[f].components; ++b) {
                    EXPECT_NE(designed(family, a, composition, sample),
                              designed(family, b, composition, sample))
                        << families[f].name << " composition " << composition << " components " << a
                        << " and " << b;
                }
            }
        }
    }
}

TEST_F(CrossSectionInterpolationBenchmark, TableGridPointsAreReproduced) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    FixtureSet& table = fixture().sets.front();
    for (std::size_t c = 0; c < composition_count; ++c) {
        for (std::size_t state = 0; state < table.state_count(); ++state) {
            SCOPED_TRACE(std::format("composition {} state {}", c, state));
            const NodeState node = node_of_state(table, state);
            const nts::InterpolatedComposition actual =
                nts::interpolate_composition(composition_set(), c, node);
            expect_no_clamps(actual.clamps);
            expect_node_near(actual.cross_sections, expected_node(table_at(table, c, state)));
        }
    }
}

TEST_F(CrossSectionInterpolationBenchmark, MultilinearDataReproducedAtRandomInteriorStates) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    for (const NodeState& node : random_states()) {
        const Point s = point_of(node);
        for (std::size_t c = 0; c < composition_count; ++c) {
            SCOPED_TRACE(std::format("composition {} at T_D {} K, rho {}, T_m {} K, C_B {} ppm", c,
                                     node.doppler_temperature_k, node.moderator_density_kg_m3,
                                     node.moderator_temperature_k, node.boron_ppm));
            const nts::InterpolatedComposition actual =
                nts::interpolate_composition(composition_set(), c, node);
            expect_no_clamps(actual.clamps);
            expect_node_near(actual.cross_sections, expected_node(designed_at(c, s)));
        }
    }
}

// TM §7.2: the interpolated quantity is 1/(3D_g), so D_g = 1/(3 · interpolated 1/(3D_g)). The
// linear interpolant of D itself over the same cell is measurably different.
TEST_F(CrossSectionInterpolationBenchmark, DiffusionIsInterpolatedAsInverseDiffusion) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    FixtureSet& table = fixture().sets.front();
    const NodeState node = state_of(800.0, 700.0, 575.0, 700.0);
    const Point s = point_of(node);
    // Cell: √600 ≤ u ≤ √900, 650 ≤ ρ ≤ 750, 550 ≤ T_m ≤ 600, 400 ≤ C_B ≤ 1500.
    const std::array<double, axis_count> lower{std::sqrt(600.0), 650.0, 550.0, 400.0};
    const std::array<double, axis_count> upper{std::sqrt(900.0), 750.0, 600.0, 1500.0};
    const std::array<double, axis_count> coordinate{s.u, s.rho, s.tau, s.b};
    const std::array<std::size_t, axis_count> lower_index{1, 1, 0, 1};
    std::array<double, axis_count> t{};
    for (std::size_t a = 0; a < axis_count; ++a) {
        t[a] = (coordinate[a] - lower[a]) / (upper[a] - lower[a]);
    }
    constexpr std::array<Family, group_count> diffusion{Family::d1, Family::d2};
    for (std::size_t c = 0; c < composition_count; ++c) {
        const nts::InterpolatedComposition actual =
            nts::interpolate_composition(composition_set(), c, node);
        for (std::size_t g = 0; g < group_count; ++g) {
            SCOPED_TRACE(std::format("composition {} group {}", c, g));
            // Eq. (7.2) applied to the stored D: weight 1 - t on the lower, t on the upper corner.
            double linear = 0.0;
            for (std::size_t corner = 0; corner < (std::size_t{1} << axis_count); ++corner) {
                double weight = 1.0;
                std::array<std::size_t, axis_count> index = lower_index;
                for (std::size_t a = 0; a < axis_count; ++a) {
                    const bool upper_corner = ((corner >> a) & 1U) != 0;
                    weight *= upper_corner ? t[a] : 1.0 - t[a];
                    index[a] += upper_corner ? 1U : 0U;
                }
                const std::size_t state = state_index(table, index);
                linear += weight * entry_value(table, std::string(family_spec(diffusion[g]).name),
                                               c, state, 0);
            }
            const double reference = stored(diffusion[g], 0, c, s);
            expect_close(actual.cross_sections.diffusion_cm[g], reference);
            EXPECT_GT(std::abs(linear - actual.cross_sections.diffusion_cm[g]) /
                          actual.cross_sections.diffusion_cm[g],
                      diffusion_gap);
        }
    }
}

TEST_F(CrossSectionInterpolationBenchmark, RemovalCrossSectionIncludesDownScatter) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    for (const NodeState& node : random_states()) {
        for (std::size_t c = 0; c < composition_count; ++c) {
            SCOPED_TRACE(std::format("composition {} at T_D {} K", c, node.doppler_temperature_k));
            const NodeCrossSections& xs =
                nts::interpolate_composition(composition_set(), c, node).cross_sections;
            // Σ_r1 = Σ_a1 + Σ_12 (TM Eq. (3.5)); Σ_r2 = Σ_a2 without down-scatter.
            expect_close(xs.removal_per_cm[0], xs.absorption_per_cm[0] + xs.down_scatter_per_cm);
            expect_close(xs.removal_per_cm[1], xs.absorption_per_cm[1]);
        }
    }
}

// TM Eq. (3.5): the engine assumes no up-scatter, so Σ_21 never enters Σ_12.
TEST_F(CrossSectionInterpolationBenchmark, DownScatterIgnoresUpScatter) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    for (const NodeState& node : random_states()) {
        const Point s = point_of(node);
        for (std::size_t c = 0; c < composition_count; ++c) {
            SCOPED_TRACE(std::format("composition {} at T_D {} K", c, node.doppler_temperature_k));
            const nts::InterpolatedComposition actual =
                nts::interpolate_composition(composition_set(), c, node);
            expect_close(actual.cross_sections.down_scatter_per_cm,
                         stored(Family::sigma_s12, 0, c, s));
        }
    }
}

// TM §7.2: Σ_f is interpolated only when both groups' tables are present.
TEST_F(CrossSectionInterpolationBenchmark, FissionAbsentUnlessBothGroupsTabled) {
    FixtureLibrary fixture = designed_library(four_axes());
    fixture.sets.front().quantities.erase("Sigma_f2");
    ASSERT_NO_FATAL_FAILURE(load_library(std::move(fixture)));
    const NodeState node = state_of(800.0, 700.0, 575.0, 700.0);
    for (std::size_t c = 0; c < composition_count; ++c) {
        const nts::InterpolatedComposition actual =
            nts::interpolate_composition(composition_set(), c, node);
        EXPECT_FALSE(actual.cross_sections.fission_per_cm.has_value());
    }
}

// TM §5.6: discontinuity factors are 1 when the set has none.
TEST_F(CrossSectionInterpolationBenchmark, DiscontinuityFactorsAreOneWhenAbsent) {
    FixtureLibrary fixture = designed_library(four_axes());
    fixture.sets.front().quantities.erase("discontinuity_factor");
    ASSERT_NO_FATAL_FAILURE(load_library(std::move(fixture)));
    const NodeState node = state_of(800.0, 700.0, 575.0, 700.0);
    for (std::size_t c = 0; c < composition_count; ++c) {
        const nts::InterpolatedComposition actual =
            nts::interpolate_composition(composition_set(), c, node);
        for (std::size_t i = 0; i < face_components; ++i) {
            EXPECT_DOUBLE_EQ(actual.cross_sections.discontinuity_factor[i], 1.0) << "index " << i;
        }
    }
}

// ----- Clamping (TM §7.2) -------------------------------------------------------------------

// The lowest and highest states of the table are end points, not outside it: not clamped, and
// the values are the table's.
TEST_F(CrossSectionInterpolationBenchmark, ExactEndPointIsNotClamped) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    FixtureSet& table = fixture().sets.front();
    const std::array<std::size_t, 2> end_states{0, table.state_count() - 1};
    for (const std::size_t state : end_states) {
        for (std::size_t c = 0; c < composition_count; ++c) {
            SCOPED_TRACE(std::format("composition {} state {}", c, state));
            const nts::InterpolatedComposition actual =
                nts::interpolate_composition(composition_set(), c, node_of_state(table, state));
            expect_no_clamps(actual.clamps);
            expect_node_near(actual.cross_sections, expected_node(table_at(table, c, state)));
        }
    }
}

// A state outside one axis is clamped on that axis only, to its nearest end point, and the
// values are those at the clamped coordinate.
TEST_F(CrossSectionInterpolationBenchmark, ClampedCoordinateIsReportedOnItsAxisAndSide) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(four_axes())));
    struct ClampCase {
        NodeState state;
        std::size_t axis;
        ClampSide side;
    };
    const std::array<ClampCase, 8> cases{{
        {.state = state_of(450.0, 700.0, 575.0, 700.0), .axis = 0, .side = clamped_below},
        {.state = state_of(1300.0, 700.0, 575.0, 700.0), .axis = 0, .side = clamped_above},
        {.state = state_of(750.0, 550.0, 575.0, 700.0), .axis = 1, .side = clamped_below},
        {.state = state_of(750.0, 800.0, 575.0, 700.0), .axis = 1, .side = clamped_above},
        {.state = state_of(750.0, 700.0, 540.0, 700.0), .axis = 2, .side = clamped_below},
        {.state = state_of(750.0, 700.0, 620.0, 700.0), .axis = 2, .side = clamped_above},
        {.state = state_of(750.0, 700.0, 575.0, -10.0), .axis = 3, .side = clamped_below},
        {.state = state_of(750.0, 700.0, 575.0, 1600.0), .axis = 3, .side = clamped_above},
    }};
    for (const ClampCase& clamp_case : cases) {
        StateClamps expected_clamps{};
        expected_clamps[clamp_case.axis] = clamp_case.side;
        const Point clamped = clamped_point(clamp_case.state);
        for (std::size_t c = 0; c < composition_count; ++c) {
            SCOPED_TRACE(std::format(
                "axis {} composition {} at T_D {} K, rho {}, T_m {} K, C_B {}", clamp_case.axis, c,
                clamp_case.state.doppler_temperature_k, clamp_case.state.moderator_density_kg_m3,
                clamp_case.state.moderator_temperature_k, clamp_case.state.boron_ppm));
            const nts::InterpolatedComposition actual =
                nts::interpolate_composition(composition_set(), c, clamp_case.state);
            EXPECT_EQ(actual.clamps, expected_clamps);
            expect_node_near(actual.cross_sections, expected_node(designed_at(c, clamped)));
        }
    }
}

// TM §7.3: an axis the set lacks never clamps.
TEST_F(CrossSectionInterpolationBenchmark, MissingAxisNeverClamps) {
    ASSERT_NO_FATAL_FAILURE(load_library(designed_library(
        {{.name = "fuel_temperature", .units = "K", .points = {500.0, 600.0, 900.0, 1200.0}},
         {.name = "boron", .units = "ppm", .points = {0.0, 400.0, 1500.0}}})));
    const NodeState node = state_of(750.0, 1.0e6, -5.0, 700.0);
    const Point reference{
        .u = std::sqrt(750.0), .rho = nominal_density, .tau = nominal_moderator, .b = 700.0};
    for (std::size_t c = 0; c < composition_count; ++c) {
        SCOPED_TRACE(std::format("composition {}", c));
        const nts::InterpolatedComposition actual =
            nts::interpolate_composition(composition_set(), c, node);
        expect_no_clamps(actual.clamps);
        expect_node_near(actual.cross_sections, expected_node(designed_at(c, reference)));
    }
}

// A length-one axis is a fixed value: its own point is not clamped.
TEST_F(CrossSectionInterpolationBenchmark, LengthOneAxisIsNotClampedAtItsPoint) {
    ASSERT_NO_FATAL_FAILURE(load_library(
        designed_library({{.name = "fuel_temperature", .units = "K", .points = {600.0, 900.0}},
                          {.name = "boron", .units = "ppm", .points = {1000.0}}})));
    const NodeState node = state_of(750.0, nominal_density, nominal_moderator, 1000.0);
    const Point reference{
        .u = std::sqrt(750.0), .rho = nominal_density, .tau = nominal_moderator, .b = 1000.0};
    for (std::size_t c = 0; c < composition_count; ++c) {
        SCOPED_TRACE(std::format("composition {}", c));
        const nts::InterpolatedComposition actual =
            nts::interpolate_composition(composition_set(), c, node);
        expect_no_clamps(actual.clamps);
        expect_node_near(actual.cross_sections, expected_node(designed_at(c, reference)));
    }
}

// A length-one axis clamps every value other than its point, below and above, to that point.
TEST_F(CrossSectionInterpolationBenchmark, LengthOneAxisClampsOffItsPoint) {
    ASSERT_NO_FATAL_FAILURE(load_library(
        designed_library({{.name = "fuel_temperature", .units = "K", .points = {600.0, 900.0}},
                          {.name = "boron", .units = "ppm", .points = {1000.0}}})));
    const Point reference{
        .u = std::sqrt(750.0), .rho = nominal_density, .tau = nominal_moderator, .b = 1000.0};
    const std::array<std::pair<double, ClampSide>, 2> offsets{
        {{999.0, clamped_below}, {1001.0, clamped_above}}};
    for (const auto& [boron, side] : offsets) {
        StateClamps expected_clamps{};
        expected_clamps[static_cast<std::size_t>(nts::StateAxis::boron)] = side;
        for (std::size_t c = 0; c < composition_count; ++c) {
            SCOPED_TRACE(std::format("composition {} boron {} ppm", c, boron));
            const NodeState node = state_of(750.0, nominal_density, nominal_moderator, boron);
            const nts::InterpolatedComposition actual =
                nts::interpolate_composition(composition_set(), c, node);
            EXPECT_EQ(actual.clamps, expected_clamps);
            expect_node_near(actual.cross_sections, expected_node(designed_at(c, reference)));
        }
    }
}

// ----- Clamp counts of a whole problem (TM §7.4 step 4) ------------------------------------

TEST_F(CrossSectionInterpolationBenchmark, ProblemClampCountsAreExact) {
    library() = designed_library(four_axes());
    use_node_rows(problem(), node_rows());
    const nts::Problem loaded = accepted();
    const nts::CrossSectionField field = nts::evaluate_cross_sections(loaded);
    // Per axis [below, above]: fuel 3/1, density 1/2, moderator 2/1, boron 0/3.
    const nts::ClampCounts expected{{{3, 1}, {1, 2}, {2, 1}, {0, 3}}};
    EXPECT_EQ(field.clamp_counts, expected);
}

TEST_F(CrossSectionInterpolationBenchmark, ProblemNodeClampSidesAreExact) {
    library() = designed_library(four_axes());
    const std::vector<NodeRow> rows = node_rows();
    use_node_rows(problem(), rows);
    const nts::Problem loaded = accepted();
    const nts::CrossSectionField field = nts::evaluate_cross_sections(loaded);
    ASSERT_EQ(field.clamps.size(), rows.size());
    for (std::size_t n = 0; n < rows.size(); ++n) {
        SCOPED_TRACE(std::format("node {}", n));
        EXPECT_EQ(field.clamps[n], rows[n].clamps);
    }
}

TEST_F(CrossSectionInterpolationBenchmark, ProblemClampedNodeValuesAreEndPointValues) {
    library() = designed_library(four_axes());
    const std::vector<NodeRow> rows = node_rows();
    use_node_rows(problem(), rows);
    const nts::Problem loaded = accepted();
    const nts::CrossSectionField field = nts::evaluate_cross_sections(loaded);
    ASSERT_EQ(field.nodes.size(), rows.size());
    for (std::size_t n = 0; n < rows.size(); ++n) {
        SCOPED_TRACE(std::format("node {} at position {}", n, rows[n].position));
        expect_node_near(field.nodes[n], expected_node(designed_at(rows[n].composition,
                                                                   clamped_point(rows[n].state))));
    }
}
