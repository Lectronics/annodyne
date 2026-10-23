#include "core/tuning/chromatic_grid.h"
#include "core/tuning/scale_grid.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;

namespace {

// Absolute cents of a pitch class (C = 0) in octave 4 (C4 = 6000, A4 = 6900).
double cents_of(int pitch_class, int octave = 4) {
    return 1200.0 * octave + 100.0 * pitch_class;
}

double snap(const ScaleGrid& g, double cents) {
    return target_pitch(Cents{cents}, g).value().value();
}

}  // namespace

TEST_CASE("the_named_scales_have_their_degrees", "[scale_grid]") {
    CHECK(scale_degrees(ScaleType::major) == std::vector<int>{0, 2, 4, 5, 7, 9, 11});
    CHECK(scale_degrees(ScaleType::natural_minor) == std::vector<int>{0, 2, 3, 5, 7, 8, 10});
    CHECK(scale_degrees(ScaleType::harmonic_minor) == std::vector<int>{0, 2, 3, 5, 7, 8, 11});
    CHECK(scale_degrees(ScaleType::major_pentatonic) == std::vector<int>{0, 2, 4, 7, 9});
    CHECK(scale_degrees(ScaleType::minor_pentatonic) == std::vector<int>{0, 3, 5, 7, 10});
    CHECK(scale_degrees(ScaleType::dorian) == std::vector<int>{0, 2, 3, 5, 7, 9, 10});
    CHECK(scale_degrees(ScaleType::mixolydian) == std::vector<int>{0, 2, 4, 5, 7, 9, 10});
    CHECK(scale_degrees(ScaleType::chromatic).size() == 12);
}

TEST_CASE("c_major_snaps_to_its_notes_and_a_note_in_the_scale_stays", "[scale_grid]") {
    const ScaleGrid g = make_scale_grid(ScaleType::major, 0).value();
    CHECK(snap(g, cents_of(0)) == cents_of(0));  // C4 stays
    CHECK(snap(g, cents_of(1) - 10.0) ==
          cents_of(0));  // C#4 - 10 cents -> C4 (1 is not in C major)
    CHECK(snap(g, cents_of(1) + 10.0) == cents_of(2));    // -> D4
    CHECK(snap(g, cents_of(6) - 20.0) == cents_of(5));    // F#4 - 20 -> F4 (6 is not in the scale)
    CHECK(snap(g, cents_of(11) + 30.0) == cents_of(11));  // B4 + 30: B stays nearest (C is 70 away)
    CHECK(snap(g, cents_of(0, 5) - 30.0) ==
          cents_of(0, 5));  // just under C5 -> C5, across the octave
}

TEST_CASE("a_pitch_between_two_scale_points_rounds_toward_plus_infinity", "[scale_grid]") {
    const ScaleGrid g = make_scale_grid(ScaleType::major, 0).value();
    CHECK(snap(g, cents_of(1)) == cents_of(2));  // C#4 sits exactly between C4 and D4
    CHECK(snap(g, cents_of(3)) == cents_of(4));  // D#4 between D4 and E4
}

TEST_CASE("the_root_moves_the_scale_and_a_minor_differs_from_major", "[scale_grid]") {
    const ScaleGrid d_major = make_scale_grid(ScaleType::major, 2).value();  // D E F# G A B C#
    CHECK(snap(d_major, cents_of(5) + 10.0) == cents_of(6));                 // F4 + 10 -> F#4
    const ScaleGrid a_minor = make_scale_grid(ScaleType::natural_minor, 9).value();
    CHECK(snap(a_minor, cents_of(11) + 5.0) == cents_of(11));  // B in A minor
    CHECK(snap(a_minor, cents_of(10) - 10.0) ==
          cents_of(9));  // A#4 - 10 -> A4 (A# is not in A minor)
    const ScaleGrid c_major = make_scale_grid(ScaleType::major, 0).value();
    CHECK(snap(c_major, cents_of(3) + 1.0) == cents_of(4));  // E-flat region in C major -> E
    const ScaleGrid c_minor = make_scale_grid(ScaleType::natural_minor, 0).value();
    CHECK(snap(c_minor, cents_of(3) + 1.0) == cents_of(3));  // E-flat is a note of C minor
}

TEST_CASE("a_chromatic_scale_agrees_with_the_chromatic_grid_and_a_reference_moves_the_points",
          "[scale_grid]") {
    for (const double a4 : {440.0, 432.0, 415.3}) {
        const ScaleGrid g = make_scale_grid(ScaleType::chromatic, 0, Hz{a4}).value();
        const ChromaticGrid c{.a4_reference = Hz{a4}};
        for (int i = 0; i < 81; ++i) {
            const double cents = 5000.0 + 37.3 * i;
            INFO("a4 " << a4 << " cents " << cents);
            CHECK(snap(g, cents) == target_pitch(Cents{cents}, c).value().value());
        }
    }
    // a 432 Hz reference moves every point by 1200 log2(432/440) cents
    const ScaleGrid g432 = make_scale_grid(ScaleType::major, 9, Hz{432.0}).value();
    const double shift = 1200.0 * std::log2(432.0 / 440.0);
    CHECK(std::abs(snap(g432, 6900.0 + shift) - (6900.0 + shift)) < 1e-6);
}

TEST_CASE("a_bad_grid_or_pitch_is_an_invalid_argument", "[scale_grid]") {
    const ScaleGrid good = make_scale_grid(ScaleType::major, 0).value();
    CHECK(target_pitch(Cents{std::nan("")}, good).error().code == ErrorCode::invalid_argument);
    CHECK(make_scale_grid(ScaleType::major, 12).error().code == ErrorCode::invalid_argument);
    CHECK(make_scale_grid(ScaleType::major, -1).error().code == ErrorCode::invalid_argument);
    CHECK(make_scale_grid(ScaleType::major, 0, Hz{0.0}).error().code ==
          ErrorCode::invalid_argument);
    ScaleGrid g = good;
    g.degrees = {};
    CHECK(target_pitch(Cents{6000.0}, g).error().code == ErrorCode::invalid_argument);
    g.degrees = {0, 4, 4};
    CHECK(target_pitch(Cents{6000.0}, g).error().code == ErrorCode::invalid_argument);
    g.degrees = {0, 12};
    CHECK(target_pitch(Cents{6000.0}, g).error().code == ErrorCode::invalid_argument);
    g = good;
    g.root = 20;
    CHECK(target_pitch(Cents{6000.0}, g).error().code == ErrorCode::invalid_argument);
}
