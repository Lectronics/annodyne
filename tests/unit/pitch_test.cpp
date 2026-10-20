#include "core/analysis/pitch/pitch_estimator.h"
#include "core/analysis/pitch/pyin.h"
#include "support/analysis_metrics.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_hop = 0.005;

/// Truth in cents per frame for an instantaneous-f0 function; NaN outside [0, seconds].
std::vector<double> truth_for(const std::function<double(double)>& f0_hz, std::size_t n_frames) {
    std::vector<double> t(n_frames);
    for (std::size_t i = 0; i < n_frames; ++i) {
        t[i] = to_cents(Hz{f0_hz(static_cast<double>(i) * k_hop)}, Hz{440.0}).value();
    }
    return t;
}

AudioSource source_of(const std::vector<float>& mono, double rate, int channels = 1) {
    return AudioSource::from_buffer(to_buffer(mono, rate, channels));
}

PitchTrack estimate(const std::vector<float>& mono, double rate, int channels = 1) {
    return detect_pitch(source_of(mono, rate, channels), PitchParams{}).value();
}

/// Frames at least `margin` seconds from both ends.
std::pair<std::size_t, std::size_t> inner(const PitchTrack& t, double margin) {
    const auto m = static_cast<std::size_t>(margin / k_hop);
    return {m, t.f0.size() - m};
}

}  // namespace

TEST_CASE("the_grid_covers_the_source_at_the_requested_hop", "[pitch]") {
    const auto x = make_sine(220.0, 1.0, 48000.0);
    const auto t = estimate(x, 48000.0);
    CHECK(t.grid.hop == Seconds{k_hop});
    CHECK(t.grid.n_frames == 201);
    CHECK(t.f0.size() == 201);
    CHECK(t.confidence_prob.size() == 201);
    CHECK(t.voicing_prob.size() == 201);
    CHECK(t.alternates.size() == 2);
    for (const auto& alt : t.alternates) {
        CHECK(alt.size() == 201);
    }
    CHECK(validate(t).has_value());
}

TEST_CASE("stationary_tones_are_within_3_cents", "[pitch]") {
    for (const double rate : {44100.0, 48000.0}) {
        for (const double f : {80.0, 110.0, 220.0, 440.0, 880.0, 1200.0}) {
            INFO("rate " << rate << " f0 " << f);
            const auto x = make_harmonic([f](double) { return f; }, 0.6, rate, harmonic_amps(6));
            const auto t = estimate(x, rate);
            const auto truth = truth_for([f](double) { return f; }, t.f0.size());
            const auto [a, b] = inner(t, 0.06);
            const auto s = score_pitch(t, truth, tolerance::k_pitch_stationary.value(), a, b);
            CHECK(static_cast<std::size_t>(s.voiced_reference) == b - a);
            CHECK(s.correct == s.voiced_reference);  // every voiced frame within 3 cents
            CHECK(max_abs_error_cents(t, truth, a, b) <= tolerance::k_pitch_stationary.value());
        }
    }
}

TEST_CASE("pure_sine_tones_are_tracked", "[pitch]") {
    for (const double f : {130.8, 330.0, 700.0}) {
        const auto x = make_sine(f, 0.6, 44100.0);
        const auto t = estimate(x, 44100.0);
        const auto truth = truth_for([f](double) { return f; }, t.f0.size());
        const auto [a, b] = inner(t, 0.06);
        CHECK(max_abs_error_cents(t, truth, a, b) <= tolerance::k_pitch_stationary.value());
    }
}

TEST_CASE("vibrato_and_glides_reach_10_cent_accuracy", "[pitch]") {
    const auto vibrato = [](double t) {
        return 220.0 * std::pow(2.0, 50.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t) / 1200.0);
    };
    const auto glide = [](double t) { return 220.0 * std::pow(2.0, t); };  // one octave per second
    for (const double rate : {22050.0, 32000.0, 44100.0, 48000.0}) {
        for (const int channels : {1, 2}) {
            if (channels == 2 && rate != 48000.0) {
                continue;  // stereo mixing is rate-independent: check it once
            }
            INFO("rate " << rate << " channels " << channels);
            for (const auto& f0 :
                 {std::function<double(double)>{vibrato}, std::function<double(double)>{glide}}) {
                const auto x = make_harmonic(f0, 1.0, rate, harmonic_amps(8));
                const auto t = estimate(x, rate, channels);
                const auto truth = truth_for(f0, t.f0.size());
                const auto [a, b] = inner(t, 0.06);
                const auto s = score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
                CHECK(s.rpa() >= tolerance::k_rpa_min);
                CHECK(s.octave_errors == 0);
            }
        }
    }
}

TEST_CASE("a_harmonic_tone_in_noise_is_tracked", "[pitch]") {
    auto x = make_harmonic([](double) { return 150.0; }, 1.0, 44100.0, harmonic_amps(10));
    add_white_noise(x, 20.0, 7);
    const auto t = estimate(x, 44100.0);
    const auto truth = truth_for([](double) { return 150.0; }, t.f0.size());
    const auto [a, b] = inner(t, 0.06);
    const auto s = score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
    CHECK(s.rpa() >= tolerance::k_rpa_min);
    CHECK(s.octave_errors == 0);
}

TEST_CASE("a_weak_fundamental_is_not_an_octave_error", "[pitch]") {
    // Partial 1 is 26 dB below partial 2: the signal looks like 400 Hz unless the 200 Hz period
    // wins.
    std::vector<double> amps{0.025, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15};
    const auto x = make_harmonic([](double) { return 200.0; }, 0.8, 44100.0, amps);
    const auto t = estimate(x, 44100.0);
    const auto truth = truth_for([](double) { return 200.0; }, t.f0.size());
    const auto [a, b] = inner(t, 0.06);
    const auto s = score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
    CHECK(s.octave_errors == 0);
    CHECK(s.rpa() >= tolerance::k_rpa_min);
    // the octave is available as an alternative
    int with_octave = 0;
    for (std::size_t i = a; i < b; ++i) {
        for (const auto& alt : t.alternates) {
            if (!std::isnan(alt[i].value()) &&
                std::abs(std::abs(alt[i].value() - truth[i]) - 1200.0) < 100.0) {
                ++with_octave;
                break;
            }
        }
    }
    CHECK(with_octave > 0);
}

TEST_CASE("silence_and_noise_are_unvoiced", "[pitch]") {
    const std::vector<float> silence(44100, 0.0F);
    const auto ts = estimate(silence, 44100.0);
    for (const Cents c : ts.f0) {
        REQUIRE(std::isnan(c.value()));
    }
    const auto noise = make_noise(1.0, 44100.0, 0.1, 3);
    const auto tn = estimate(noise, 44100.0);
    const std::vector<double> none(tn.f0.size(), std::nan(""));
    const auto s = score_pitch(tn, none, 10.0, 0, tn.f0.size());
    CHECK(s.false_alarm_rate() < 0.1);
}

TEST_CASE("a_note_surrounded_by_silence_is_voiced_only_inside", "[pitch]") {
    std::vector<float> x(44100, 0.0F);
    const auto tone = make_harmonic([](double) { return 262.0; }, 0.5, 44100.0, harmonic_amps(5));
    std::ranges::copy(tone, x.begin() + 11025);  // 0.25 s .. 0.75 s
    const auto t = estimate(x, 44100.0);
    int voiced_inside = 0;
    int voiced_outside = 0;
    for (std::size_t i = 0; i < t.f0.size(); ++i) {
        const double time = static_cast<double>(i) * k_hop;
        const bool voiced = !std::isnan(t.f0[i].value());
        if (time > 0.3 && time < 0.7) {
            voiced_inside += voiced ? 1 : 0;
        } else if (time < 0.2 || time > 0.8) {
            voiced_outside += voiced ? 1 : 0;
        }
    }
    CHECK(voiced_inside >= 75);  // 80 frames
    CHECK(voiced_outside == 0);
}

TEST_CASE("parameters_and_sources_are_validated", "[pitch]") {
    const auto x = make_sine(220.0, 0.2, 44100.0);
    const AudioSource src = source_of(x, 44100.0);
    CHECK(detect_pitch(src, PitchParams{.min_f0 = Hz{0.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(detect_pitch(src, PitchParams{.min_f0 = Hz{500.0}, .max_f0 = Hz{100.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(detect_pitch(src, PitchParams{.hop = Seconds{0.0}}).error().code ==
          ErrorCode::invalid_argument);
    // a window shorter than two periods of min_f0 (0.04 s < 2 / 20 Hz)
    CHECK(detect_pitch(src, PitchParams{.min_f0 = Hz{20.0}}).error().code ==
          ErrorCode::invalid_argument);
    // the source rate cannot represent max_f0
    const auto low_rate = make_sine(100.0, 0.5, 2000.0);
    CHECK(detect_pitch(source_of(low_rate, 2000.0), PitchParams{}).error().code ==
          ErrorCode::invalid_argument);
    // empty source: an empty valid track
    const auto empty = detect_pitch(source_of({}, 44100.0), PitchParams{}).value();
    CHECK(empty.grid.n_frames == 0);
    CHECK(validate(empty).has_value());
    // shorter than the window: still valid
    const auto tiny = estimate(make_sine(220.0, 0.01, 44100.0), 44100.0);
    CHECK(validate(tiny).has_value());
}

TEST_CASE("the_estimator_is_deterministic_and_named", "[pitch]") {
    const auto x =
        make_harmonic([](double t) { return 180.0 + 20.0 * t; }, 0.5, 48000.0, harmonic_amps(6));
    const auto a = estimate(x, 48000.0);
    const auto b = estimate(x, 48000.0);
    for (std::size_t i = 0; i < a.f0.size(); ++i) {
        const bool same =
            (std::isnan(a.f0[i].value()) && std::isnan(b.f0[i].value())) || a.f0[i] == b.f0[i];
        REQUIRE(same);
    }
    const PYinEstimator estimator;
    CHECK(estimator.name() == "pyin");
    CHECK(estimator.version() >= 1);
}

namespace {

/// Estimates with explicit pYIN parameters (the front-end ratios).
PitchTrack estimate_with(const std::vector<float>& mono, double rate, const PYinParams& pyin,
                         int channels = 1) {
    return PYinEstimator{pyin}.estimate(source_of(mono, rate, channels), PitchParams{}).value();
}

PYinParams no_front_end() {
    PYinParams p;
    p.highpass_min_f0_ratio = std::nullopt;
    p.lowpass_max_f0_ratio = std::nullopt;
    return p;
}

void add_sine(std::vector<float>& x, double hz, double amplitude, double rate) {
    const std::vector<float> s =
        make_sine(hz, static_cast<double>(x.size()) / rate, rate, amplitude);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += s[i];
    }
}

/// Frames of the track inside [a, b) within 3 cents of a constant truth.
std::size_t frames_within_3_cents(const PitchTrack& t, double f_hz, std::size_t a, std::size_t b) {
    const double truth = to_cents(Hz{f_hz}, Hz{440.0}).value();
    std::size_t good = 0;
    for (std::size_t i = a; i < b; ++i) {
        if (is_voiced(t, i) && std::abs(t.f0[i].value() - truth) <= 3.0) {
            ++good;
        }
    }
    return good;
}

}  // namespace

TEST_CASE("tones_at_the_ends_of_the_range_survive_the_front_end", "[pitch][filter]") {
    // The zero-phase pair is -6 dB at its cutoff; the default ratios keep min_f0 and max_f0 inside.
    for (const double rate : {44100.0, 48000.0}) {
        for (const double f : {55.0, 1500.0}) {
            INFO("rate " << rate << " f0 " << f);
            const auto x = make_harmonic([f](double) { return f; }, 0.8, rate, harmonic_amps(4));
            const auto t = estimate(x, rate);
            const auto [a, b] = inner(t, 0.1);
            CHECK(frames_within_3_cents(t, f, a, b) == b - a);
        }
    }
}

TEST_CASE("rumble_and_hiss_outside_the_voice_band_do_not_disturb_the_pitch", "[pitch][filter]") {
    for (const double rate : {44100.0, 48000.0}) {
        auto x = make_harmonic([](double) { return 220.0; }, 0.8, rate, harmonic_amps(5, 0.2));
        add_sine(x, 18.0, 0.5, rate);     // sub-voice rumble, louder than the voice
        add_sine(x, 7000.0, 0.15, rate);  // hiss-like tone far above the voice
        const auto t = estimate(x, rate);
        const auto [a, b] = inner(t, 0.1);
        INFO("rate " << rate);
        CHECK(frames_within_3_cents(t, 220.0, a, b) == b - a);
    }
}

TEST_CASE("the_front_end_works_for_stereo_and_for_low_source_rates", "[pitch][filter]") {
    // 22.05 kHz is below the 24 kHz analysis rate: the filter still runs, without resampling.
    for (const double rate : {22050.0, 32000.0}) {
        const auto x = make_harmonic([](double) { return 330.0; }, 0.6, rate, harmonic_amps(4));
        for (const int channels : {1, 2}) {
            const auto t = estimate(x, rate, channels);
            const auto [a, b] = inner(t, 0.1);
            INFO("rate " << rate << " channels " << channels);
            CHECK(frames_within_3_cents(t, 330.0, a, b) == b - a);
        }
    }
}

TEST_CASE("the_front_end_can_be_switched_off_per_side", "[pitch][filter]") {
    const auto x = make_harmonic([](double) { return 220.0; }, 0.5, 48000.0, harmonic_amps(5));
    const auto base = estimate_with(x, 48000.0, no_front_end());
    PYinParams only_low = no_front_end();
    only_low.lowpass_max_f0_ratio = 1.5;
    PYinParams only_high = no_front_end();
    only_high.highpass_min_f0_ratio = 1.0;
    for (const PYinParams& p : {only_low, only_high, PYinParams{}}) {
        const auto t = estimate_with(x, 48000.0, p);
        REQUIRE(t.f0.size() == base.f0.size());
        const auto [a, b] = inner(t, 0.06);
        CHECK(frames_within_3_cents(t, 220.0, a, b) == b - a);
    }
}

TEST_CASE("front_end_parameters_are_validated", "[pitch][filter]") {
    const auto x = make_sine(220.0, 0.3, 48000.0);
    for (const double bad : {0.0, -1.0, std::nan("")}) {
        PYinParams p;
        p.lowpass_max_f0_ratio = bad;
        CHECK(PYinEstimator{p}.estimate(source_of(x, 48000.0), PitchParams{}).error().code ==
              ErrorCode::invalid_argument);
        PYinParams q;
        q.highpass_min_f0_ratio = bad;
        CHECK(PYinEstimator{q}.estimate(source_of(x, 48000.0), PitchParams{}).error().code ==
              ErrorCode::invalid_argument);
    }
}

TEST_CASE("the_estimator_version_names_the_front_end", "[pitch]") {
    CHECK(PYinEstimator{}.version() == 2);  // 2: band-pass front end (ADR-0011)
}
