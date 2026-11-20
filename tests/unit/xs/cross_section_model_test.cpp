// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Aurora Jahan
// Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.

#include "nts/io/nodal_library.hpp"
#include "nts/io/problem.hpp"
#include "nts/xs/cross_section_model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../io/library_fixture.hpp"
#include "../io/problem_fixture.hpp"
#include "../io/problem_test_support.hpp"

namespace {

using nts::ClampSide;
using nts::CompositionSet;
using nts::InterpolatedComposition;
using nts::NodalLibrary;
using nts::NodeCrossSections;
using nts::NodeState;
using nts::Quantity;
using nts::StateAxis;
using nts::StateClamps;
using nts::test::entry_value;
using nts::test::FixtureAxis;
using nts::test::FixtureLibrary;
using nts::test::FixtureSet;
using nts::test::make_valid_fixture;
using nts::test::make_valid_fixture_on_axes;

// The default fixture's grid: fuel_temperature {600, 900} K, boron {0, 1000} ppm, and the
// state index is 2 * (temperature index) + (boron index).
constexpr double doppler_low = 600.0;
constexpr double doppler_high = 900.0;
constexpr double boron_low = 0.0;
constexpr double boron_high = 1000.0;
constexpr std::size_t doppler_points = 2;
constexpr std::size_t boron_points = 2;

constexpr std::size_t fuel_composition = 0;
constexpr std::size_t reflector_composition = 1;
constexpr std::size_t node_count = 10;
constexpr double default_moderator_density = 700.0;     // kg m^-3, an axis the default set lacks
constexpr double default_moderator_temperature = 560.0; // K, likewise
constexpr double far_outside = 1.0e6;
// Interpolated values carry a few ulp of rounding from the weights and the sum of up to 8 terms.
constexpr double relative_tolerance = 1.0e-14;
// Read in place of an absent Σ_f after its presence has been asserted.
constexpr std::array<double, nts::energy_group_count> no_fission{};

constexpr std::size_t grid_state(std::size_t doppler, std::size_t boron) {
    return (doppler * boron_points) + boron;
}

constexpr std::array<Quantity, nts::energy_group_count> diffusion_quantities{Quantity::d1,
                                                                             Quantity::d2};
constexpr std::array<Quantity, nts::energy_group_count> absorption_quantities{Quantity::sigma_a1,
                                                                              Quantity::sigma_a2};
constexpr std::array<Quantity, nts::energy_group_count> nu_fission_quantities{
    Quantity::nu_sigma_f1, Quantity::nu_sigma_f2};
constexpr std::array<Quantity, nts::energy_group_count> kappa_fission_quantities{
    Quantity::kappa_sigma_f1, Quantity::kappa_sigma_f2};
constexpr std::array<Quantity, nts::energy_group_count> inverse_velocity_quantities{
    Quantity::inv_v1, Quantity::inv_v2};
constexpr std::array<Quantity, nts::energy_group_count> fission_quantities{Quantity::sigma_f1,
                                                                           Quantity::sigma_f2};

NodeState make_state(double doppler, double boron, double density = default_moderator_density,
                     double moderator = default_moderator_temperature) {
    return {.doppler_temperature_k = doppler,
            .moderator_density_kg_m3 = density,
            .moderator_temperature_k = moderator,
            .boron_ppm = boron};
}

InterpolatedComposition interpolate(const NodalLibrary& library, std::size_t composition,
                                    const NodeState& state) {
    return nts::interpolate_composition(library.composition_sets.at(0), composition, state);
}

// The table value as read back: layout ((composition * states + state) * components + c).
double table_value(const CompositionSet& set, Quantity quantity, std::size_t composition,
                   std::size_t state, std::size_t component = 0) {
    const std::size_t components = nts::quantity_spec(quantity).components;
    const std::size_t index = (((composition * set.state_count) + state) * components) + component;
    return set.find(quantity)->value.at(index);
}

// The four grid corners of the default grid, in the order (T lo, B lo), (T lo, B hi),
// (T hi, B lo), (T hi, B hi).
std::array<double, 4> grid_corners(const CompositionSet& set, Quantity quantity,
                                   std::size_t composition, std::size_t component = 0) {
    return {table_value(set, quantity, composition, grid_state(0, 0), component),
            table_value(set, quantity, composition, grid_state(0, 1), component),
            table_value(set, quantity, composition, grid_state(1, 0), component),
            table_value(set, quantity, composition, grid_state(1, 1), component)};
}

// Bilinear combination of the corners with the fractions t_T and t_B (TM Eq. (7.2)).
double bilinear(const std::array<double, 4>& corners, double t_doppler, double t_boron) {
    return ((1.0 - t_doppler) * (1.0 - t_boron) * corners[0]) +
           ((1.0 - t_doppler) * t_boron * corners[1]) + (t_doppler * (1.0 - t_boron) * corners[2]) +
           (t_doppler * t_boron * corners[3]);
}

// t = (√T − √T_lo) / (√T_hi − √T_lo) (TM §7.2 with s = √T_D).
double fuel_fraction(double doppler) {
    return (std::sqrt(doppler) - std::sqrt(doppler_low)) /
           (std::sqrt(doppler_high) - std::sqrt(doppler_low));
}

// t = (C_B − C_lo) / (C_hi − C_lo) (TM §7.2, linear in C_B).
double boron_fraction(double boron) {
    return (boron - boron_low) / (boron_high - boron_low);
}

StateClamps clamps_on(StateAxis axis, ClampSide side) {
    StateClamps clamps{};
    clamps[static_cast<std::size_t>(axis)] = side;
    return clamps;
}

void expect_same(const NodeCrossSections& a, const NodeCrossSections& b) {
    EXPECT_EQ(a.diffusion_cm, b.diffusion_cm);
    EXPECT_EQ(a.absorption_per_cm, b.absorption_per_cm);
    EXPECT_EQ(a.down_scatter_per_cm, b.down_scatter_per_cm);
    EXPECT_EQ(a.removal_per_cm, b.removal_per_cm);
    EXPECT_EQ(a.nu_fission_per_cm, b.nu_fission_per_cm);
    EXPECT_EQ(a.kappa_fission_j_per_cm, b.kappa_fission_j_per_cm);
    EXPECT_EQ(a.fission_per_cm, b.fission_per_cm);
    EXPECT_EQ(a.inverse_velocity_s_per_cm, b.inverse_velocity_s_per_cm);
    EXPECT_EQ(a.delayed_fraction, b.delayed_fraction);
    EXPECT_EQ(a.decay_constant_per_s, b.decay_constant_per_s);
    EXPECT_EQ(a.discontinuity_factor, b.discontinuity_factor);
}

class CrossSectionModelTest : public testing::Test {
protected:
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    // Writes the library and reads it back; the test set is the only one.
    NodalLibrary read_library(const FixtureLibrary& library) {
        nts::test::write_fixture(library, path_);
        auto result = nts::read_nodal_library(path_);
        EXPECT_TRUE(result.has_value()) << "the fixture library was rejected";
        return result ? std::move(*result) : NodalLibrary{};
    }

private:
    std::filesystem::path path_ = [] {
        const std::string name =
            std::format("nts_xs_{}_{}.h5",
                        testing::UnitTest::GetInstance()->current_test_info()->name(), getpid());
        return std::filesystem::temp_directory_path() / name;
    }();
};

// ----- Grid reproduction and the interpolation rules ----------------------------------------

TEST_F(CrossSectionModelTest, GridPointsReproduceTheTable) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    constexpr std::array<double, doppler_points> dopplers{doppler_low, doppler_high};
    constexpr std::array<double, boron_points> borons{boron_low, boron_high};
    for (const std::size_t composition : {fuel_composition, reflector_composition}) {
        for (std::size_t d = 0; d < dopplers.size(); ++d) {
            for (std::size_t b = 0; b < borons.size(); ++b) {
                const std::size_t state = grid_state(d, b);
                SCOPED_TRACE(testing::Message()
                             << "composition " << composition << ", state " << state);
                const InterpolatedComposition result =
                    interpolate(library, composition, make_state(dopplers[d], borons[b]));
                const NodeCrossSections& xs = result.cross_sections;
                EXPECT_EQ(result.clamps, StateClamps{});
                for (std::size_t g = 0; g < nts::energy_group_count; ++g) {
                    const double diffusion =
                        table_value(set, diffusion_quantities[g], composition, state);
                    // D_g goes through 1/(3D_g) and back, so it carries a few ulp.
                    EXPECT_NEAR(xs.diffusion_cm[g], diffusion, relative_tolerance * diffusion);
                    EXPECT_DOUBLE_EQ(
                        xs.absorption_per_cm[g],
                        table_value(set, absorption_quantities[g], composition, state));
                    EXPECT_DOUBLE_EQ(
                        xs.nu_fission_per_cm[g],
                        table_value(set, nu_fission_quantities[g], composition, state));
                    EXPECT_DOUBLE_EQ(
                        xs.kappa_fission_j_per_cm[g],
                        table_value(set, kappa_fission_quantities[g], composition, state));
                    EXPECT_DOUBLE_EQ(
                        xs.inverse_velocity_s_per_cm[g],
                        table_value(set, inverse_velocity_quantities[g], composition, state));
                    ASSERT_TRUE(xs.fission_per_cm.has_value());
                    EXPECT_DOUBLE_EQ(xs.fission_per_cm.value_or(no_fission)[g],
                                     table_value(set, fission_quantities[g], composition, state));
                }
                const double sigma_a1 = table_value(set, Quantity::sigma_a1, composition, state);
                const double sigma_a2 = table_value(set, Quantity::sigma_a2, composition, state);
                const double sigma_s12 = table_value(set, Quantity::sigma_s12, composition, state);
                EXPECT_DOUBLE_EQ(xs.down_scatter_per_cm, sigma_s12);
                EXPECT_DOUBLE_EQ(xs.removal_per_cm[0], sigma_a1 + sigma_s12);
                EXPECT_DOUBLE_EQ(xs.removal_per_cm[1], sigma_a2);
                for (std::size_t k = 0; k < nts::delayed_group_count; ++k) {
                    EXPECT_DOUBLE_EQ(xs.delayed_fraction[k],
                                     table_value(set, Quantity::beta, composition, state, k));
                    EXPECT_DOUBLE_EQ(xs.decay_constant_per_s[k],
                                     table_value(set, Quantity::lambda, composition, state, k));
                }
                for (std::size_t c = 0; c < xs.discontinuity_factor.size(); ++c) {
                    EXPECT_DOUBLE_EQ(
                        xs.discontinuity_factor[c],
                        table_value(set, Quantity::discontinuity_factor, composition, state, c));
                }
            }
        }
    }
}

TEST_F(CrossSectionModelTest, DopplerCoordinateIsSquareRootOfTemperature) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    constexpr double doppler = 750.0;
    const double t = (std::sqrt(doppler) - std::sqrt(doppler_low)) /
                     (std::sqrt(doppler_high) - std::sqrt(doppler_low));
    const double lo = table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 0));
    const double hi = table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(1, 0));
    const double expected = ((1.0 - t) * lo) + (t * hi);
    // The value linear in T (t = 0.5) must be distinguishable from the √T one.
    const double linear_in_t = 0.5 * (lo + hi);
    EXPECT_GT(std::abs(expected - linear_in_t), 1e-6 * std::abs(expected));

    const double actual = interpolate(library, fuel_composition, make_state(doppler, boron_low))
                              .cross_sections.absorption_per_cm[0];
    // Two weights and two terms: a few ulp of rounding.
    EXPECT_NEAR(actual, expected, relative_tolerance * std::abs(expected));
}

TEST_F(CrossSectionModelTest, BoronIsLinear) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    constexpr double boron = 250.0;
    const double t = 0.25; // (250 - 0) / (1000 - 0)
    const double lo = table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 0));
    const double hi = table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 1));
    const double expected = ((1.0 - t) * lo) + (t * hi);

    const double actual = interpolate(library, fuel_composition, make_state(doppler_low, boron))
                              .cross_sections.absorption_per_cm[0];
    EXPECT_NEAR(actual, expected, relative_tolerance * std::abs(expected));
}

TEST_F(CrossSectionModelTest, InteriorPointIsBilinearWithFourCornerWeights) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const double t_doppler = fuel_fraction(800.0);
    const double t_boron = 0.7;
    // Corners: state 0 (600, 0), state 1 (600, 1000), state 2 (900, 0), state 3 (900, 1000).
    const double w00 = (1.0 - t_doppler) * (1.0 - t_boron);
    const double w01 = (1.0 - t_doppler) * t_boron;
    const double w10 = t_doppler * (1.0 - t_boron);
    const double w11 = t_doppler * t_boron;
    const double expected =
        (w00 * table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 0))) +
        (w01 * table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 1))) +
        (w10 * table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(1, 0))) +
        (w11 * table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(1, 1)));

    const double actual = interpolate(library, fuel_composition, make_state(800.0, 700.0))
                              .cross_sections.absorption_per_cm[0];
    // Four weights and four terms: a few ulp of rounding.
    EXPECT_NEAR(actual, expected, relative_tolerance * std::abs(expected));
}

TEST_F(CrossSectionModelTest, DiffusionIsRecoveredFromInterpolatedInverse) {
    FixtureLibrary fixture = make_valid_fixture();
    FixtureSet& fixture_set = fixture.sets.front();
    const std::array<std::string, nts::energy_group_count> names{"D1", "D2"};
    const std::array<double, nts::energy_group_count> cold{1.0, 0.5};
    const std::array<double, nts::energy_group_count> hot{2.0, 1.5};
    for (std::size_t g = 0; g < nts::energy_group_count; ++g) {
        for (std::size_t b = 0; b < boron_points; ++b) {
            entry_value(fixture_set, names[g], fuel_composition, grid_state(0, b)) = cold[g];
            entry_value(fixture_set, names[g], fuel_composition, grid_state(1, b)) = hot[g];
        }
    }
    const NodalLibrary library = read_library(fixture);

    // The √T midpoint: t = 0.5. Boron 500 ppm: both boron corners are equal.
    const double mid_doppler =
        std::pow((std::sqrt(doppler_low) + std::sqrt(doppler_high)) / 2.0, 2.0);
    const NodeCrossSections xs =
        interpolate(library, fuel_composition, make_state(mid_doppler, 500.0)).cross_sections;
    for (std::size_t g = 0; g < nts::energy_group_count; ++g) {
        const double inverse_3d = (0.5 * (1.0 / (3.0 * cold[g]))) + (0.5 * (1.0 / (3.0 * hot[g])));
        const double expected = 1.0 / (3.0 * inverse_3d);
        EXPECT_NEAR(xs.diffusion_cm[g], expected, relative_tolerance * expected);
    }
    // Group 1: 1/(3·(0.5/3 + 0.5/6)) = 4/3, not the linear-in-D value 1.5.
    EXPECT_NEAR(xs.diffusion_cm[0], 4.0 / 3.0, relative_tolerance * (4.0 / 3.0));
}

TEST_F(CrossSectionModelTest, RemovalIsAbsorptionPlusDownScatterForGroupOne) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const double t_doppler = fuel_fraction(800.0);
    const double t_boron = boron_fraction(700.0);
    const double removal1 =
        bilinear(grid_corners(set, Quantity::sigma_a1, fuel_composition), t_doppler, t_boron) +
        bilinear(grid_corners(set, Quantity::sigma_s12, fuel_composition), t_doppler, t_boron);
    const double removal2 =
        bilinear(grid_corners(set, Quantity::sigma_a2, fuel_composition), t_doppler, t_boron);

    const NodeCrossSections xs =
        interpolate(library, fuel_composition, make_state(800.0, 700.0)).cross_sections;
    EXPECT_NEAR(xs.removal_per_cm[0], removal1, relative_tolerance * removal1);
    EXPECT_NEAR(xs.removal_per_cm[1], removal2, relative_tolerance * removal2);
}

TEST_F(CrossSectionModelTest, DownScatterIsInterpolatedSigma12) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const double t_doppler = fuel_fraction(800.0);
    const double t_boron = boron_fraction(700.0);
    const double expected =
        bilinear(grid_corners(set, Quantity::sigma_s12, fuel_composition), t_doppler, t_boron);

    const NodeCrossSections xs =
        interpolate(library, fuel_composition, make_state(800.0, 700.0)).cross_sections;
    EXPECT_NEAR(xs.down_scatter_per_cm, expected, relative_tolerance * expected);
}

TEST_F(CrossSectionModelTest, SigmaS21IsNotUsed) {
    FixtureLibrary changed = make_valid_fixture();
    FixtureSet& changed_set = changed.sets.front();
    for (const std::size_t composition : {fuel_composition, reflector_composition}) {
        for (std::size_t s = 0; s < changed_set.state_count(); ++s) {
            entry_value(changed_set, "Sigma_s21", composition, s) = 0.5;
        }
    }
    const NodalLibrary base = read_library(make_valid_fixture());
    const NodalLibrary other = read_library(changed);
    for (const std::size_t composition : {fuel_composition, reflector_composition}) {
        SCOPED_TRACE(testing::Message() << "composition " << composition);
        const NodeState state = make_state(800.0, 700.0);
        expect_same(interpolate(base, composition, state).cross_sections,
                    interpolate(other, composition, state).cross_sections);
    }
}

// ----- Clamping -----------------------------------------------------------------------------

TEST_F(CrossSectionModelTest, FuelTemperatureBelowGridClampsToFirstPoint) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const InterpolatedComposition result =
        interpolate(library, fuel_composition, make_state(400.0, boron_low));
    EXPECT_DOUBLE_EQ(result.cross_sections.absorption_per_cm[0],
                     table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 0)));
    EXPECT_EQ(result.clamps, clamps_on(StateAxis::fuel_temperature, ClampSide::below));
}

TEST_F(CrossSectionModelTest, FuelTemperatureAboveGridClampsToLastPoint) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const InterpolatedComposition result =
        interpolate(library, fuel_composition, make_state(1200.0, boron_low));
    EXPECT_DOUBLE_EQ(result.cross_sections.absorption_per_cm[0],
                     table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(1, 0)));
    EXPECT_EQ(result.clamps, clamps_on(StateAxis::fuel_temperature, ClampSide::above));
}

TEST_F(CrossSectionModelTest, BoronAboveGridClampsToLastPoint) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const InterpolatedComposition result =
        interpolate(library, fuel_composition, make_state(doppler_low, 2000.0));
    EXPECT_DOUBLE_EQ(result.cross_sections.absorption_per_cm[0],
                     table_value(set, Quantity::sigma_a1, fuel_composition, grid_state(0, 1)));
    EXPECT_EQ(result.clamps, clamps_on(StateAxis::boron, ClampSide::above));
}

TEST_F(CrossSectionModelTest, EndPointsAreNotClamped) {
    const NodalLibrary library = read_library(make_valid_fixture());
    constexpr std::array<double, doppler_points> dopplers{doppler_low, doppler_high};
    constexpr std::array<double, boron_points> borons{boron_low, boron_high};
    for (const double doppler : dopplers) {
        for (const double boron : borons) {
            SCOPED_TRACE(testing::Message() << "T " << doppler << " K, boron " << boron << " ppm");
            EXPECT_EQ(interpolate(library, fuel_composition, make_state(doppler, boron)).clamps,
                      StateClamps{});
        }
    }
}

TEST_F(CrossSectionModelTest, AxesTheSetLacksNeverClamp) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const InterpolatedComposition result =
        interpolate(library, fuel_composition, make_state(750.0, 500.0, far_outside, far_outside));
    EXPECT_EQ(result.clamps, StateClamps{});
}

// A NaN state is no point of any axis: it is an invariant violation, on a one-point axis (which
// would otherwise return its point unclamped) and on one of several points alike. The sets are
// built in memory: a death-test child aborts before any TearDown could remove a written file,
// and the assertion fires before any table is read.
TEST(CrossSectionModelDeathTest, NaNStateIsRejectedOnEveryAxisLength) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    CompositionSet set;
    set.composition_names = {"fuel"};
    for (const std::vector<double>& boron_axis :
         {std::vector<double>{0.0, 1000.0}, std::vector<double>{975.0}}) {
        SCOPED_TRACE(testing::Message() << boron_axis.size() << " boron points");
        set.axes = {{.axis = StateAxis::fuel_temperature, .points = {600.0, 900.0}},
                    {.axis = StateAxis::boron, .points = boron_axis}};
        set.state_count = 2 * boron_axis.size();
        EXPECT_DEATH(
            static_cast<void>(nts::interpolate_composition(set, 0, make_state(750.0, nan))),
            "a node state value is a number");
    }
}

// ----- Axes of other kinds ------------------------------------------------------------------

TEST_F(CrossSectionModelTest, ModeratorDensityAxisIsLinear) {
    const NodalLibrary library = read_library(make_valid_fixture_on_axes(
        {FixtureAxis{
             .name = "fuel_temperature", .units = "K", .points = {doppler_low, doppler_high}},
         FixtureAxis{.name = "moderator_density", .units = "kg m^-3", .points = {600.0, 700.0}},
         FixtureAxis{.name = "boron", .units = "ppm", .points = {boron_low, boron_high}}}));
    const CompositionSet& set = library.composition_sets.at(0);
    // States row-major over (T, rho, B): (600, 600, 0) = 0 and (600, 700, 0) = 2.
    const double t = 0.25; // (625 - 600) / (700 - 600), no square root
    const double lo = table_value(set, Quantity::sigma_a1, fuel_composition, 0);
    const double hi = table_value(set, Quantity::sigma_a1, fuel_composition, 2);
    const double expected = ((1.0 - t) * lo) + (t * hi);

    const InterpolatedComposition result =
        interpolate(library, fuel_composition, make_state(doppler_low, boron_low, 625.0));
    EXPECT_NEAR(result.cross_sections.absorption_per_cm[0], expected,
                relative_tolerance * std::abs(expected));
    EXPECT_EQ(result.clamps, StateClamps{});
}

TEST_F(CrossSectionModelTest, ModeratorDensityAboveAxisClampsToLastPoint) {
    const NodalLibrary library = read_library(make_valid_fixture_on_axes(
        {FixtureAxis{
             .name = "fuel_temperature", .units = "K", .points = {doppler_low, doppler_high}},
         FixtureAxis{.name = "moderator_density", .units = "kg m^-3", .points = {600.0, 700.0}},
         FixtureAxis{.name = "boron", .units = "ppm", .points = {boron_low, boron_high}}}));
    const CompositionSet& set = library.composition_sets.at(0);
    const InterpolatedComposition result =
        interpolate(library, fuel_composition, make_state(doppler_low, boron_low, 800.0));
    EXPECT_DOUBLE_EQ(result.cross_sections.absorption_per_cm[0],
                     table_value(set, Quantity::sigma_a1, fuel_composition, 2));
    EXPECT_EQ(result.clamps, clamps_on(StateAxis::moderator_density, ClampSide::above));
}

TEST_F(CrossSectionModelTest, SetWithoutAxesGivesTheSingleTableValue) {
    const NodalLibrary library = read_library(make_valid_fixture_on_axes({}, 2));
    const InterpolatedComposition result = interpolate(
        library, fuel_composition, make_state(1200.0, 2000.0, far_outside, far_outside));
    const CompositionSet& set = library.composition_sets.at(0);
    EXPECT_DOUBLE_EQ(result.cross_sections.absorption_per_cm[0],
                     table_value(set, Quantity::sigma_a1, fuel_composition, 0));
    EXPECT_DOUBLE_EQ(result.cross_sections.nu_fission_per_cm[0],
                     table_value(set, Quantity::nu_sigma_f1, fuel_composition, 0));
    EXPECT_EQ(result.clamps, StateClamps{});
}

// ----- Discontinuity factors ----------------------------------------------------------------

TEST_F(CrossSectionModelTest, AbsentDiscontinuityFactorsAreOne) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.sets.front().quantities.erase("discontinuity_factor");
    const NodalLibrary library = read_library(fixture);
    const NodeCrossSections xs =
        interpolate(library, fuel_composition, make_state(800.0, 700.0)).cross_sections;
    for (const double factor : xs.discontinuity_factor) {
        EXPECT_DOUBLE_EQ(factor, 1.0);
    }
}

TEST_F(CrossSectionModelTest, DiscontinuityFactorsReproduceAtGridPointByFaceAndGroup) {
    FixtureLibrary fixture = make_valid_fixture();
    FixtureSet& fixture_set = fixture.sets.front();
    // Component c is face * 2 + group in the table's [6][2] layout; each is distinct.
    constexpr std::size_t factor_components = nts::face_count * nts::energy_group_count;
    const auto designed = [](std::size_t c) { return 0.5 + (0.05 * static_cast<double>(c)); };
    for (std::size_t c = 0; c < factor_components; ++c) {
        entry_value(fixture_set, "discontinuity_factor", fuel_composition, grid_state(0, 0), c) =
            designed(c);
    }
    const NodalLibrary library = read_library(fixture);
    const NodeCrossSections xs =
        interpolate(library, fuel_composition, make_state(doppler_low, boron_low)).cross_sections;
    for (std::size_t c = 0; c < factor_components; ++c) {
        EXPECT_DOUBLE_EQ(xs.discontinuity_factor[c], designed(c)) << "component " << c;
    }
}

// ----- Fission cross section ----------------------------------------------------------------

TEST_F(CrossSectionModelTest, FissionIsInterpolatedWhenBothGroupsArePresent) {
    const NodalLibrary library = read_library(make_valid_fixture());
    const CompositionSet& set = library.composition_sets.at(0);
    const double t_doppler = fuel_fraction(800.0);
    const double t_boron = boron_fraction(700.0);
    const NodeCrossSections xs =
        interpolate(library, fuel_composition, make_state(800.0, 700.0)).cross_sections;
    ASSERT_TRUE(xs.fission_per_cm.has_value());
    for (std::size_t g = 0; g < nts::energy_group_count; ++g) {
        const double expected = bilinear(grid_corners(set, fission_quantities[g], fuel_composition),
                                         t_doppler, t_boron);
        EXPECT_NEAR(xs.fission_per_cm.value_or(no_fission)[g], expected,
                    relative_tolerance * expected);
    }
}

TEST_F(CrossSectionModelTest, FissionIsAbsentWhenAGroupIsAbsent) {
    for (const std::string group : {"Sigma_f1", "Sigma_f2"}) {
        SCOPED_TRACE("removed " + group);
        FixtureLibrary fixture = make_valid_fixture();
        fixture.sets.front().quantities.erase(group);
        const NodalLibrary library = read_library(fixture);
        EXPECT_FALSE(interpolate(library, fuel_composition, make_state(800.0, 700.0))
                         .cross_sections.fission_per_cm.has_value());
    }
}

TEST_F(CrossSectionModelTest, FissionIsAbsentWhenBothGroupsAreAbsent) {
    FixtureLibrary fixture = make_valid_fixture();
    fixture.sets.front().quantities.erase("Sigma_f1");
    fixture.sets.front().quantities.erase("Sigma_f2");
    const NodalLibrary library = read_library(fixture);
    EXPECT_FALSE(interpolate(library, fuel_composition, make_state(800.0, 700.0))
                     .cross_sections.fission_per_cm.has_value());
}

// ----- evaluate_cross_sections on a whole problem -------------------------------------------

class CrossSectionFieldTest : public nts::test::ProblemTest {};

TEST_F(CrossSectionFieldTest, NodesTakeTheCompositionOfTheirPosition) {
    const nts::Problem loaded = accepted();
    const nts::CrossSectionField field = nts::evaluate_cross_sections(loaded);
    ASSERT_EQ(field.nodes.size(), node_count);
    const CompositionSet& set = loaded.library.composition_sets.at(0);
    // The node state is 700 K and 500 ppm. Node 0 is position 1 (fuel), node 9 is position 11
    // (reflector).
    const double t_doppler = fuel_fraction(700.0);
    const double t_boron = boron_fraction(500.0);
    const double fuel_expected =
        bilinear(grid_corners(set, Quantity::sigma_a1, fuel_composition), t_doppler, t_boron);
    const double reflector_expected =
        bilinear(grid_corners(set, Quantity::sigma_a1, reflector_composition), t_doppler, t_boron);
    EXPECT_NEAR(field.nodes[0].absorption_per_cm[0], fuel_expected,
                relative_tolerance * fuel_expected);
    EXPECT_NEAR(field.nodes[node_count - 1].absorption_per_cm[0], reflector_expected,
                relative_tolerance * reflector_expected);
}

TEST_F(CrossSectionFieldTest, ClampsAreCountedPerNodeAndAxis) {
    // Doppler temperatures in [z][y][x] order, x fastest. Positions 0 and 6 are excluded.
    // Positions 1-3 (nodes 0-2) at 400 K are below the grid; positions 4-5 (nodes 3-4) at
    // 1200 K are above it; every other node is at 700 K.
    const std::vector<double> doppler{700.0, 400.0, 400.0, 400.0, 1200.0, 1200.0,
                                      700.0, 700.0, 700.0, 700.0, 700.0,  700.0};
    initial_state().datasets["doppler_temperature"] =
        nts::test::float64_array(doppler, {2, 2, 3}, "K");
    allow_table_clamps(1); // the nodes below and above the grid are clamped, not rejected
    const nts::Problem loaded = accepted();
    const nts::CrossSectionField field = nts::evaluate_cross_sections(loaded);

    ASSERT_EQ(field.clamps.size(), node_count);
    for (std::size_t n = 0; n < node_count; ++n) {
        ClampSide side = ClampSide::none;
        if (n < 3) {
            side = ClampSide::below;
        } else if (n < 5) {
            side = ClampSide::above;
        }
        EXPECT_EQ(field.clamps[n], clamps_on(StateAxis::fuel_temperature, side)) << "node " << n;
    }

    const auto fuel = static_cast<std::size_t>(StateAxis::fuel_temperature);
    EXPECT_EQ(field.clamp_counts[fuel][0], 3U);
    EXPECT_EQ(field.clamp_counts[fuel][1], 2U);
    for (const StateAxis axis :
         {StateAxis::moderator_density, StateAxis::moderator_temperature, StateAxis::boron}) {
        const auto index = static_cast<std::size_t>(axis);
        EXPECT_EQ(field.clamp_counts[index][0], 0U);
        EXPECT_EQ(field.clamp_counts[index][1], 0U);
    }
}

} // namespace
