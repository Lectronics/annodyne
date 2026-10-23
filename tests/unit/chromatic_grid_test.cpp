#include "core/tuning/chromatic_grid.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace anno;

namespace {
double snap(double cents, Hz ref = k_a4_standard_hz) {
    return target_pitch(Cents{cents}, ChromaticGrid{ref}).value().value();
}
}  // namespace

TEST_CASE("a_pitch_on_the_grid_is_its_own_target", "[chromatic_grid]") {
    for (const double c : {6900.0, 6000.0, 7200.0, 0.0, -1200.0, 12345.0 - 45.0}) {
        CHECK(snap(c) == c);
    }
}

TEST_CASE("the_nearest_semitone_wins_either_side_of_the_half", "[chromatic_grid]") {
    CHECK(snap(6900.0 + 49.0) == 6900.0);
    CHECK(snap(6900.0 + 51.0) == 7000.0);
    CHECK(snap(6900.0 - 49.0) == 6900.0);
    CHECK(snap(6900.0 - 51.0) == 6800.0);
}

TEST_CASE("a_pitch_exactly_between_two_semitones_rounds_toward_plus_infinity", "[chromatic_grid]") {
    CHECK(snap(6950.0) == 7000.0);
    CHECK(snap(6850.0) == 6900.0);
    CHECK(snap(-50.0) == 0.0);
    CHECK(snap(-150.0) == -100.0);
}

TEST_CASE("the_reference_moves_the_whole_grid", "[chromatic_grid]") {
    const double shift = 1200.0 * std::log2(432.0 / 440.0);  // about -31.8 cents
    CHECK(snap(6900.0, Hz{432.0}) == Catch::Approx(6900.0 + shift).margin(1e-9));
    CHECK(snap(7000.0 + shift, Hz{432.0}) == Catch::Approx(7000.0 + shift).margin(1e-9));
    // A pitch at 440 Hz is 31.8 cents above the 432 grid's A4: still nearest to it.
    CHECK(snap(6900.0, Hz{432.0}) < 6900.0);
}

TEST_CASE("a_bad_pitch_or_reference_is_an_invalid_argument", "[chromatic_grid]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(target_pitch(Cents{nan}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(target_pitch(Cents{inf}, {}).error().code == ErrorCode::invalid_argument);
    for (const double ref : {nan, inf, 0.0, -440.0}) {
        CHECK(target_pitch(Cents{6900.0}, ChromaticGrid{Hz{ref}}).error().code ==
              ErrorCode::invalid_argument);
    }
}

TEST_CASE("ties_and_idempotence_hold_for_a_shifted_reference", "[chromatic_grid]") {
    const Hz ref{432.0};
    const double on_grid = snap(7000.0, ref);
    CHECK(snap(on_grid, ref) == Catch::Approx(on_grid).margin(1e-9));
    CHECK(snap(snap(7033.3, ref), ref) == Catch::Approx(snap(7033.3, ref)).margin(1e-9));
    CHECK(snap(on_grid + 50.0 + 1e-6, ref) == Catch::Approx(on_grid + 100.0).margin(1e-9));
    CHECK(snap(on_grid + 50.0 - 1e-6, ref) == Catch::Approx(on_grid).margin(1e-9));
}

TEST_CASE("very_large_finite_pitches_stay_finite", "[chromatic_grid]") {
    CHECK(std::isfinite(snap(1e12)));
    CHECK(std::isfinite(snap(-1e12)));
}
