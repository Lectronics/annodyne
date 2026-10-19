#include "core/analysis/onsets/onsets.h"
#include "core/analysis/pitch/pitch_estimator.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;

double seconds_of(SourceTime t) {
    return (t - SourceTime{}).value();
}

AudioSource source_of(const std::vector<float>& mono, double rate = k_rate) {
    return AudioSource::from_buffer(to_buffer(mono, rate));
}

/// Adds `burst` into `x` at `start_s`, with an abrupt start (1 ms ramp) and a 5 ms fade-out.
void add_burst(std::vector<float>& x, const std::vector<float>& burst, double start_s,
               double rate = k_rate) {
    const auto first = static_cast<std::size_t>(std::llround(start_s * rate));
    const auto ramp = static_cast<std::size_t>(0.001 * rate);
    const auto fade = static_cast<std::size_t>(0.005 * rate);
    for (std::size_t i = 0; i < burst.size() && first + i < x.size(); ++i) {
        double g = 1.0;
        if (i < ramp) {
            g = static_cast<double>(i) / static_cast<double>(ramp);
        }
        if (burst.size() - i < fade) {
            g = static_cast<double>(burst.size() - i) / static_cast<double>(fade);
        }
        x[first + i] += static_cast<float>(g * burst[i]);
    }
}

std::vector<float> tone(double f0, double seconds, double amplitude) {
    return make_harmonic([f0](double) { return f0; }, seconds, k_rate, harmonic_amps(6, amplitude));
}

}  // namespace

TEST_CASE("tone_bursts_are_found_within_5_ms", "[onsets]") {
    std::vector<float> x(static_cast<std::size_t>(2.4 * k_rate), 0.0F);
    const std::vector<double> starts{0.3, 0.8, 1.3, 1.8};
    for (const double s : starts) {
        add_burst(x, tone(330.0, 0.15, 0.5), s);
    }
    const auto onsets = detect_onsets(source_of(x), OnsetParams{}).value();
    REQUIRE(onsets.times.size() == starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
        CHECK(std::abs(seconds_of(onsets.times[i]) - starts[i]) <= 0.005);
        CHECK(onsets.strength[i] >= 1.0);
        CHECK(onsets.kind[i] == OnsetKind::attack);
    }
    CHECK(validate(onsets).has_value());
}

TEST_CASE("a_quiet_burst_40_db_below_a_loud_one_is_still_found", "[onsets]") {
    std::vector<float> x(static_cast<std::size_t>(1.6 * k_rate), 0.0F);
    add_burst(x, tone(330.0, 0.15, 0.5), 0.3);
    add_burst(x, tone(330.0, 0.15, 0.005), 1.0);
    const auto onsets = detect_onsets(source_of(x), OnsetParams{}).value();
    REQUIRE(onsets.times.size() == 2);
    CHECK(std::abs(seconds_of(onsets.times[0]) - 0.3) <= 0.005);
    CHECK(std::abs(seconds_of(onsets.times[1]) - 1.0) <= 0.005);
}

TEST_CASE("silence_and_tiny_inputs_have_no_onsets", "[onsets]") {
    CHECK(detect_onsets(source_of(std::vector<float>(44100, 0.0F)), OnsetParams{})
              .value()
              .times.empty());
    CHECK(detect_onsets(source_of(make_sine(440.0, 0.01, k_rate)), OnsetParams{})
              .value()
              .times.empty());
    const auto empty = detect_onsets(source_of({}), OnsetParams{}).value();
    CHECK(empty.times.empty());
    CHECK(validate(empty).has_value());
}

TEST_CASE("a_wide_vibrato_tone_gives_no_false_onsets_and_fewer_than_plain_flux", "[onsets]") {
    // +-200 cents at 6 Hz with 25 partials: partials glide across filterbank bands, which plain
    // spectral flux reads as new energy and SuperFlux's frequency max filter ignores.
    const auto vibrato = [](double t) {
        return 220.0 * std::pow(2.0, 200.0 * std::sin(2.0 * std::numbers::pi * 6.0 * t) / 1200.0);
    };
    auto x = make_harmonic(vibrato, 3.0, k_rate, harmonic_amps(25));
    for (std::size_t i = 0; i < 4000;
         ++i) {  // soft start and end: the tone's own boundaries are not the point
        const auto gain = static_cast<float>(i) / 4000.0F;
        x[i] *= gain;
        x[x.size() - 1 - i] *= gain;
    }
    const AudioSource src = source_of(x);
    const auto superflux = detect_onsets(src, OnsetParams{}).value();
    OnsetParams plain_params;
    plain_params.max_filter_bins = 1;  // no frequency max filter: plain spectral flux
    const auto plain = detect_onsets(src, plain_params).value();
    CHECK(superflux.times.size() <= 1);  // the slow start may count once
    CHECK(plain.times.size() >= 3);
    CHECK(plain.times.size() > superflux.times.size());
}

TEST_CASE("a_legato_pitch_step_is_a_pitch_change_onset", "[onsets]") {
    const auto step = [](double t) {
        return t < 0.5 ? 440.0 : 493.88;
    };  // A4 -> B4, continuous level
    const auto x = make_harmonic(step, 1.0, k_rate, harmonic_amps(6));
    const AudioSource src = source_of(x);
    const auto pitch = detect_pitch(src, PitchParams{}).value();
    const auto onsets = detect_onsets(src, pitch, OnsetParams{}).value();
    int changes = 0;
    for (std::size_t i = 0; i < onsets.times.size(); ++i) {
        if (onsets.kind[i] == OnsetKind::pitch_change) {
            ++changes;
            CHECK(std::abs(seconds_of(onsets.times[i]) - 0.5) <= 0.015);
            CHECK(onsets.strength[i] >= 1.0);
        }
    }
    CHECK(changes == 1);
}

TEST_CASE("vibrato_alone_does_not_make_pitch_change_onsets", "[onsets]") {
    const auto vibrato = [](double t) {
        return 220.0 * std::pow(2.0, 60.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t) / 1200.0);
    };
    const auto x = make_harmonic(vibrato, 1.5, k_rate, harmonic_amps(8));
    const AudioSource src = source_of(x);
    const auto pitch = detect_pitch(src, PitchParams{}).value();
    const auto onsets = detect_onsets(src, pitch, OnsetParams{}).value();
    for (const OnsetKind k : onsets.kind) {
        CHECK(k != OnsetKind::pitch_change);
    }
}

TEST_CASE("a_noise_burst_is_a_consonant_and_a_tone_burst_an_attack", "[onsets]") {
    std::vector<float> x(static_cast<std::size_t>(2.0 * k_rate), 0.0F);
    add_burst(x, make_noise(0.12, k_rate, 0.2, 5), 0.4);
    add_burst(x, tone(262.0, 0.3, 0.4), 1.2);
    const AudioSource src = source_of(x);
    const auto pitch = detect_pitch(src, PitchParams{}).value();
    const auto onsets = detect_onsets(src, pitch, OnsetParams{}).value();
    REQUIRE(onsets.times.size() == 2);
    CHECK(onsets.kind[0] == OnsetKind::consonant);
    CHECK(onsets.kind[1] == OnsetKind::attack);
    // without a pitch track there is no voicing data: every spectral onset is an attack
    const auto spectral = detect_onsets(src, OnsetParams{}).value();
    REQUIRE(spectral.times.size() == 2);
    CHECK(spectral.kind[0] == OnsetKind::attack);
}

TEST_CASE("onsets_work_at_other_sample_rates_and_are_deterministic", "[onsets]") {
    for (const double rate : {22050.0, 32000.0, 48000.0}) {
        std::vector<float> x(static_cast<std::size_t>(1.2 * rate), 0.0F);
        add_burst(x, make_harmonic([](double) { return 330.0; }, 0.15, rate, harmonic_amps(6, 0.5)),
                  0.4, rate);
        const AudioSource src = source_of(x, rate);
        const auto a = detect_onsets(src, OnsetParams{}).value();
        const auto b = detect_onsets(src, OnsetParams{}).value();
        REQUIRE(a.times.size() == 1);
        CHECK(std::abs(seconds_of(a.times[0]) - 0.4) <=
              0.006);  // the 5 ms hop is rounded to samples
        CHECK(a.times == b.times);
        CHECK(a.strength == b.strength);
    }
}

TEST_CASE("onset_parameters_are_validated", "[onsets]") {
    const AudioSource src = source_of(make_sine(440.0, 0.3, k_rate));
    OnsetParams p;
    p.hop = Seconds{0.0};
    CHECK(detect_onsets(src, p).error().code == ErrorCode::invalid_argument);
    p = OnsetParams{};
    p.window = Seconds{-1.0};
    CHECK(detect_onsets(src, p).error().code == ErrorCode::invalid_argument);
    p = OnsetParams{};
    p.delta = -1.0;
    CHECK(detect_onsets(src, p).error().code == ErrorCode::invalid_argument);
    p = OnsetParams{};
    p.max_filter_bins = 0;
    CHECK(detect_onsets(src, p).error().code == ErrorCode::invalid_argument);
}
