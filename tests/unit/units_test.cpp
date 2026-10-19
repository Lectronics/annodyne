#include "core/types/time_range.h"
#include "core/types/units.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using namespace anno;
using namespace anno::unit_literals;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("quantities_add_subtract_and_scale_within_one_unit", "[units]") {
    CHECK((1.5_s + 0.5_s) == 2.0_s);
    CHECK((1.5_s - 0.5_s) == 1.0_s);
    CHECK(-(3.0_db) == Decibels{-3.0});
    CHECK((2.0_s * 3.0) == 6.0_s);
    CHECK((3.0 * 2.0_s) == 6.0_s);
    CHECK((6.0_s / 3.0) == 2.0_s);
    CHECK((Samples{10} * 3) == Samples{30});

    Seconds total{};
    total += 1.0_s;
    total -= 0.25_s;
    CHECK(total == 0.75_s);
    CHECK(Seconds{} == 0.0_s);
    CHECK(1.0_s < 2.0_s);
}

TEST_CASE("quantity_divided_by_quantity_is_a_fractional_ratio", "[units]") {
    CHECK((3.0_s / 2.0_s) == Ratio{1.5});
    // Integer Samples must not truncate.
    CHECK((Samples{3} / Samples{2}) == Ratio{1.5});
}

TEST_CASE("ratio_scales_floating_point_quantities", "[units]") {
    CHECK((Cents{100.0} * Ratio{0.5}) == Cents{50.0});
    CHECK((Ratio{0.5} * Cents{100.0}) == Cents{50.0});
    CHECK((Seconds{2.0} * Ratio{1.5}) == Seconds{3.0});
    CHECK((Seconds{3.0} / Ratio{1.5}) == Seconds{2.0});
    CHECK((Ratio{2.0} * Ratio{0.25}) == Ratio{0.5});
    CHECK((Ratio{1.0} / Ratio{4.0}) == Ratio{0.25});
}

TEST_CASE("time_points_are_affine_on_their_axis", "[units]") {
    const SourceTime a = SourceTime{} + 2.0_s;
    const SourceTime b = a + 0.5_s;
    CHECK((b - a) == 0.5_s);
    CHECK((b - 0.5_s) == a);
    CHECK((0.5_s + a) == b);
    CHECK(a < b);
    CHECK(SourceTime{} == SourceTime::unchecked_from_origin(0.0_s));
}

TEST_CASE("to_samples_rounds_as_requested", "[units]") {
    const Hz rate{48000.0};
    CHECK(to_samples(1.0_s, rate, Rounding::nearest) == Samples{48000});

    // 1.5 frames at 1 Hz: halves round away from zero for nearest.
    const Hz one{1.0};
    CHECK(to_samples(1.5_s, one, Rounding::nearest) == Samples{2});
    CHECK(to_samples(1.5_s, one, Rounding::down) == Samples{1});
    CHECK(to_samples(1.5_s, one, Rounding::up) == Samples{2});
    CHECK(to_samples(Seconds{-1.5}, one, Rounding::nearest) == Samples{-2});
    CHECK(to_samples(Seconds{-1.5}, one, Rounding::down) == Samples{-2});
    CHECK(to_samples(Seconds{-1.5}, one, Rounding::up) == Samples{-1});
    CHECK(to_samples(1.25_s, one, Rounding::nearest) == Samples{1});
}

TEST_CASE("samples_and_seconds_round_trip", "[units]") {
    const Hz rate{44100.0};
    for (const std::int64_t n : {0LL, 1LL, 441LL, 44100LL, 158760000LL}) {  // up to one hour
        const Samples frames{n};
        CHECK(to_samples(to_seconds(frames, rate), rate, Rounding::nearest) == frames);
    }
    CHECK_THAT(to_seconds(Samples{22050}, rate).value(),
               WithinAbs(0.5, tolerance::k_seconds_conversion_abs.value()));
}

TEST_CASE("time_points_convert_to_and_from_sample_indices", "[units]") {
    const Hz rate{48000.0};
    const SourceTime t = SourceTime{} + 0.5_s;
    CHECK(to_sample_index(t, rate, Rounding::nearest) == Samples{24000});
    CHECK(to_time_point<SourceAxis>(Samples{24000}, rate) == t);
}

TEST_CASE("cents_and_hz_convert_against_an_explicit_reference", "[units]") {
    CHECK_THAT(to_hz(k_a4_cents, k_a4_standard_hz).value(),
               WithinRel(440.0, tolerance::k_hz_conversion_rel.value()));
    CHECK_THAT(to_hz(Cents{8100.0}, k_a4_standard_hz).value(),
               WithinRel(880.0, tolerance::k_hz_conversion_rel.value()));
    CHECK_THAT(to_cents(Hz{880.0}, k_a4_standard_hz).value(),
               WithinAbs(8100.0, tolerance::k_cents_conversion_abs.value()));
    // A detected reference of 441.3 Hz moves where 6900 cents lands.
    CHECK_THAT(to_hz(k_a4_cents, Hz{441.3}).value(),
               WithinRel(441.3, tolerance::k_hz_conversion_rel.value()));
    CHECK_THAT(to_cents(to_hz(Cents{6123.4}, Hz{441.3}), Hz{441.3}).value(),
               WithinAbs(6123.4, tolerance::k_cents_conversion_abs.value()));
}

TEST_CASE("intervals_convert_to_frequency_ratios", "[units]") {
    CHECK_THAT(to_ratio(Cents{1200.0}).value(),
               WithinRel(2.0, tolerance::k_hz_conversion_rel.value()));
    CHECK_THAT(to_ratio(Cents{-1200.0}).value(),
               WithinRel(0.5, tolerance::k_hz_conversion_rel.value()));
    CHECK(to_ratio(Cents{0.0}) == Ratio{1.0});
}

TEST_CASE("decibels_and_linear_gain_convert", "[units]") {
    CHECK_THAT(to_linear_gain(Decibels{-20.0}).value(),
               WithinRel(0.1, tolerance::k_gain_conversion_rel.value()));
    CHECK_THAT(to_linear_gain(Decibels{-6.020599913279624}).value(),
               WithinRel(0.5, tolerance::k_gain_conversion_rel.value()));
    CHECK_THAT(to_decibels(LinearGain{0.1}).value(),
               WithinAbs(-20.0, tolerance::k_gain_conversion_rel.value() * 20.0));
    // Polarity is not level.
    CHECK(to_decibels(LinearGain{-0.1}) == to_decibels(LinearGain{0.1}));
}

TEST_CASE("to_decibels_never_returns_minus_infinity", "[units]") {
    CHECK(to_decibels(LinearGain{0.0}) == k_silence_floor_db);
    CHECK(to_decibels(LinearGain{1e-300}) == k_silence_floor_db);
    CHECK(std::isfinite(to_decibels(k_silence_floor_gain).value()));
}

TEST_CASE("time_range_make_rejects_reversed_ranges", "[units]") {
    const SourceTime a = SourceTime{} + 1.0_s;
    const SourceTime b = SourceTime{} + 3.0_s;

    const auto ok = SourceRange::make(a, b);
    REQUIRE(ok.has_value());
    CHECK(ok->start() == a);
    CHECK(ok->end() == b);
    CHECK(ok->duration() == 2.0_s);

    const auto reversed = SourceRange::make(b, a);
    REQUIRE_FALSE(reversed.has_value());
    CHECK(reversed.error().code == ErrorCode::invalid_argument);

    CHECK(SourceRange::make(a, a).has_value());  // empty range is valid
}

TEST_CASE("time_range_from_start_and_default", "[units]") {
    const OutputRange r = OutputRange::from_start(OutputTime{} + 1.0_s, 0.5_s);
    CHECK(r.end() == OutputTime{} + 1.5_s);
    const OutputRange empty{};
    CHECK(empty.start() == OutputTime{});
    CHECK(empty.duration() == 0.0_s);
}

TEST_CASE("serialized_seconds_round_trip_bit_exactly", "[units]") {
    const SourceTime t = SourceTime{} + Seconds{1234.567890123456789};
    const double s = to_serialized_seconds(t);
    const auto back = from_serialized_seconds<SourceAxis>(s);
    REQUIRE(back.has_value());
    CHECK(*back == t);  // exact on purpose: serialization must be lossless
}

TEST_CASE("serialized_seconds_reject_non_finite_values", "[units]") {
    for (const double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
        const auto r = from_serialized_seconds<OutputAxis>(bad);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::corrupt_data);
    }
}

TEST_CASE("literals_build_the_right_types", "[units]") {
    CHECK(512_samples == Samples{512});
    CHECK(200.0_cents == Cents{200.0});
    CHECK(440_hz == Hz{440.0});
    CHECK(0.5_ratio == Ratio{0.5});
    CHECK(2_s == Seconds{2.0});
}
