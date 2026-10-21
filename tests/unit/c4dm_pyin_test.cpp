#include "core/analysis/pitch/c4dm_pyin.h"
#include "support/analysis_metrics.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <c4dm_pyin/MonoPitchHMM.h>
#include <c4dm_pyin/Yin.h>
#include <c4dm_pyin/YinUtil.h>
#include <cmath>
#include <functional>
#include <utility>

// The vendored c4dm/pyin tracker (ADR-0014) behind PitchEstimator: the same synthetic cases as the
// annodyne estimator (pitch_test.cpp), plus a check of the vendored FFT shim.

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_hop = 0.005;

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

PitchTrack estimate(const std::vector<float>& mono, double rate, int channels = 1,
                    const C4dmPYinParams& params = {}) {
    return C4dmPYinEstimator{params}
        .estimate(source_of(mono, rate, channels), PitchParams{})
        .value();
}

std::pair<std::size_t, std::size_t> inner(const PitchTrack& t, double margin) {
    const auto m = static_cast<std::size_t>(margin / k_hop);
    return {m, t.f0.size() - m};
}

}  // namespace

TEST_CASE("the_vendored_fft_shim_gives_the_exact_yin_autocorrelation", "[c4dm_pyin]") {
    // YinUtil::fastDifference = P(0) + P(tau) - 2 r(tau), where r is the autocorrelation term that
    // goes through Vamp::FFTReal (our shim on pocketfft). The shim is exact if r matches the direct
    // sum. Upstream computes the running power term P with a one-sample slip (it adds
    // in[tau + N] where the exact sum adds in[tau - 1 + N]); that is kept for parity, so the
    // expectation uses the same recurrence (documented in third_party/c4dm_pyin/ANNODYNE.md).
    constexpr int k_n = 256;
    const std::vector<float> noise = make_noise(1.0, 2.0 * k_n, 1.0, 5);  // 2 * k_n samples
    const std::vector<double> x(noise.begin(), noise.end());
    const auto at = [&](int i) { return x[static_cast<std::size_t>(i)]; };
    YinUtil util(k_n);
    std::vector<double> fast(k_n);
    util.fastDifference(x.data(), fast.data());
    std::vector<double> power(k_n);
    for (int j = 0; j < k_n; ++j) {
        power[0] += at(j) * at(j);
    }
    for (int tau = 1; tau < k_n; ++tau) {
        power[static_cast<std::size_t>(tau)] = power[static_cast<std::size_t>(tau - 1)] -
                                               at(tau - 1) * at(tau - 1) +
                                               at(tau + k_n) * at(tau + k_n);
    }
    for (int tau = 0; tau < k_n; ++tau) {
        double r = 0.0;
        for (int j = 0; j < k_n; ++j) {
            r += at(j) * at(j + tau);
        }
        const double expected = power[0] + power[static_cast<std::size_t>(tau)] - 2.0 * r;
        INFO("tau " << tau);
        CHECK(std::abs(fast[static_cast<std::size_t>(tau)] - expected) <=
              tolerance::k_fft_vs_dft_abs * 1e3);
    }
}

TEST_CASE("c4dm_the_grid_covers_the_source_at_the_requested_hop", "[c4dm_pyin]") {
    const auto t = estimate(make_sine(220.0, 1.0, 48000.0), 48000.0);
    CHECK(t.grid.hop == Seconds{k_hop});
    CHECK(t.grid.n_frames == 201);
    CHECK(t.f0.size() == 201);
    CHECK(t.alternates.size() == 2);
    CHECK(validate(t).has_value());
}

TEST_CASE("c4dm_stationary_tones_are_within_3_cents", "[c4dm_pyin]") {
    for (const double rate : {44100.0, 48000.0}) {
        for (const double f : {80.0, 110.0, 220.0, 440.0, 880.0, 1200.0}) {
            INFO("rate " << rate << " f0 " << f);
            const auto x = make_harmonic([f](double) { return f; }, 0.6, rate, harmonic_amps(6));
            const auto t = estimate(x, rate);
            const auto truth = truth_for([f](double) { return f; }, t.f0.size());
            const auto [a, b] = inner(t, 0.06);
            const auto s = score_pitch(t, truth, tolerance::k_pitch_stationary.value(), a, b);
            CHECK(static_cast<std::size_t>(s.voiced_reference) == b - a);
            CHECK(s.correct == s.voiced_reference);
        }
    }
}

TEST_CASE("c4dm_vibrato_and_glides_reach_10_cent_accuracy", "[c4dm_pyin]") {
    const auto vibrato = [](double t) {
        return 220.0 * std::pow(2.0, 50.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t) / 1200.0);
    };
    const auto glide = [](double t) { return 220.0 * std::pow(2.0, t); };
    for (const double rate : {22050.0, 32000.0, 44100.0, 48000.0}) {
        for (const auto& f0 :
             {std::function<double(double)>{vibrato}, std::function<double(double)>{glide}}) {
            INFO("rate " << rate);
            const auto x = make_harmonic(f0, 1.0, rate, harmonic_amps(8));
            const auto t = estimate(x, rate);
            const auto truth = truth_for(f0, t.f0.size());
            const auto [a, b] = inner(t, 0.06);
            const auto s = score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
            CHECK(s.rpa() >= tolerance::k_rpa_min);
            CHECK(s.octave_errors == 0);
        }
    }
}

TEST_CASE("c4dm_a_harmonic_tone_in_noise_is_tracked", "[c4dm_pyin]") {
    auto x = make_harmonic([](double) { return 150.0; }, 1.0, 44100.0, harmonic_amps(10));
    add_white_noise(x, 20.0, 7);
    const auto t = estimate(x, 44100.0);
    const auto truth = truth_for([](double) { return 150.0; }, t.f0.size());
    const auto [a, b] = inner(t, 0.06);
    const auto s = score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
    CHECK(s.rpa() >= tolerance::k_rpa_min);
    CHECK(s.octave_errors == 0);
}

TEST_CASE("c4dm_octave_cases_have_no_octave_errors", "[c4dm_pyin]") {
    const std::vector<double> weak{0.025, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15};  // 26 dB weak partial 1
    const std::vector<double> third{0.45, 0.1, 1.0, 0.1, 0.05, 0.03};       // dominant partial 3
    for (const auto& [f, amps] : {std::pair{200.0, weak}, std::pair{135.0, third}}) {
        INFO("f0 " << f);
        const auto x = make_harmonic([f](double) { return f; }, 0.8, 44100.0, amps);
        const auto t = estimate(x, 44100.0);
        const auto truth = truth_for([f](double) { return f; }, t.f0.size());
        const auto [a, b] = inner(t, 0.06);
        const auto s = score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
        CHECK(s.octave_errors == 0);
        CHECK(s.rpa() >= tolerance::k_rpa_min);
    }
}

TEST_CASE("c4dm_silence_noise_and_gaps_are_unvoiced", "[c4dm_pyin]") {
    const std::vector<float> silence(44100, 0.0F);
    for (const Cents c : estimate(silence, 44100.0).f0) {
        REQUIRE(std::isnan(c.value()));
    }
    const auto tn = estimate(make_noise(1.0, 44100.0, 0.1, 3), 44100.0);
    const std::vector<double> none(tn.f0.size(), std::nan(""));
    CHECK(score_pitch(tn, none, 10.0, 0, tn.f0.size()).false_alarm_rate() < 0.1);

    std::vector<float> x(44100, 0.0F);
    const auto tone = make_harmonic([](double) { return 262.0; }, 0.5, 44100.0, harmonic_amps(5));
    std::ranges::copy(tone, x.begin() + 11025);
    const auto t = estimate(x, 44100.0);
    int inside = 0;
    int outside = 0;
    for (std::size_t i = 0; i < t.f0.size(); ++i) {
        const double time = static_cast<double>(i) * k_hop;
        const bool voiced = !std::isnan(t.f0[i].value());
        if (time > 0.3 && time < 0.7) {
            inside += voiced ? 1 : 0;
        } else if (time < 0.2 || time > 0.8) {
            outside += voiced ? 1 : 0;
        }
    }
    CHECK(inside >= 75);
    CHECK(outside == 0);
}

TEST_CASE("c4dm_fixed_lag_smoothing_tracks_too", "[c4dm_pyin]") {
    const auto x = make_harmonic([](double) { return 262.0; }, 1.0, 44100.0, harmonic_amps(6));
    const auto t = estimate(x, 44100.0, 1, {.fixed_lag_frames = 100});
    const auto truth = truth_for([](double) { return 262.0; }, t.f0.size());
    const auto [a, b] = inner(t, 0.06);
    CHECK(score_pitch(t, truth, tolerance::k_pitch_stationary.value(), a, b).rpa() >= 0.99);
    CHECK(validate(t).has_value());
}

TEST_CASE("c4dm_parameters_and_sources_are_validated", "[c4dm_pyin]") {
    const AudioSource src = source_of(make_sine(220.0, 0.2, 44100.0), 44100.0);
    const C4dmPYinEstimator est;
    CHECK(est.estimate(src, PitchParams{.min_f0 = Hz{0.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(est.estimate(src, PitchParams{.min_f0 = Hz{500.0}, .max_f0 = Hz{100.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(est.estimate(src, PitchParams{.hop = Seconds{0.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(est.estimate(source_of(make_sine(100.0, 0.5, 2000.0), 2000.0), PitchParams{})
              .error()
              .code == ErrorCode::invalid_argument);
    for (const C4dmPYinParams& bad :
         {C4dmPYinParams{.threshold_distribution = 8}, C4dmPYinParams{.threshold_distribution = -1},
          C4dmPYinParams{.low_amp_rms = -0.1}, C4dmPYinParams{.fixed_lag_frames = -1},
          C4dmPYinParams{.max_analysis_rate = Hz{100.0}},
          C4dmPYinParams{.down_margin3 = std::nan("")}}) {
        CHECK(C4dmPYinEstimator{bad}.estimate(src, PitchParams{}).error().code ==
              ErrorCode::invalid_argument);
    }
    const auto empty = est.estimate(source_of({}, 44100.0), PitchParams{}).value();
    CHECK(empty.grid.n_frames == 0);
    CHECK(validate(empty).has_value());
    CHECK(validate(estimate(make_sine(220.0, 0.01, 44100.0), 44100.0)).has_value());
}

TEST_CASE("c4dm_the_estimator_is_deterministic_and_named", "[c4dm_pyin]") {
    const auto x =
        make_harmonic([](double t) { return 180.0 + 20.0 * t; }, 0.5, 48000.0, harmonic_amps(6));
    const auto a = estimate(x, 48000.0);
    const auto b = estimate(x, 48000.0);
    for (std::size_t i = 0; i < a.f0.size(); ++i) {
        const bool same =
            (std::isnan(a.f0[i].value()) && std::isnan(b.f0[i].value())) || a.f0[i] == b.f0[i];
        REQUIRE(same);
    }
    const C4dmPYinEstimator estimator;
    CHECK(estimator.name() == "c4dm_pyin");
    CHECK(estimator.version() == 1);
}

TEST_CASE("c4dm_quiet_input_is_scaled_down_by_the_low_amplitude_rule", "[c4dm_pyin]") {
    // The plugin scales candidate mass by (rms + 0.001) / 0.101 below rms 0.1: a very quiet tone
    // that is voiced with the rule off is unvoiced with it on (the behaviour the review warns
    // about: an absolute level suits no DAW take).
    auto x = make_harmonic([](double) { return 262.0; }, 0.6, 44100.0, harmonic_amps(6, 0.002));
    const auto on = estimate(x, 44100.0, 1, {.low_amp_rms = 0.1});
    const auto off = estimate(x, 44100.0, 1, {.low_amp_rms = 0.0});
    const auto count = [](const PitchTrack& t) {
        return std::ranges::count_if(t.f0, [](Cents c) { return !std::isnan(c.value()); });
    };
    CHECK(count(off) > 80);
    CHECK(count(on) < count(off));
}

TEST_CASE("c4dm_with_the_guard_off_equals_the_plugin_pipeline", "[c4dm_pyin]") {
    // The plugin's process(): per block Yin::processProbabilisticYin, candidates as (MIDI pitch,
    // probability), MonoPitchHMM obs + Viterbi, nearestFreq of the path. Same blocks as the
    // estimator (start = round(i * hop * rate) - k_block / 4), full Viterbi, no low-amplitude
    // scaling (the signal is loud).
    constexpr double k_rate = 44100.0;
    constexpr std::size_t k_block = 2048;
    const auto glide = [](double t) { return 200.0 * std::pow(2.0, 0.4 * t); };
    const auto x = make_harmonic(glide, 0.8, k_rate, harmonic_amps(8, 0.9));
    const C4dmPYinParams params{.low_amp_rms = 0.0, .down_margin3 = -1.0, .down_margin2 = -1.0};
    const PitchTrack t = estimate(x, k_rate, 1, params);

    Yin yin(k_block, static_cast<std::size_t>(k_rate), 0.0, true);
    yin.setThresholdDistr(2.0F);
    MonoPitchHMM hmm(0);
    std::vector<std::vector<std::pair<double, double>>> frames;
    std::vector<double> buffer(k_block);
    for (std::size_t i = 0; i < t.f0.size(); ++i) {
        const auto start =
            static_cast<std::int64_t>(std::llround(static_cast<double>(i) * k_hop * k_rate)) -
            static_cast<std::int64_t>(k_block / 4);
        for (std::size_t k = 0; k < k_block; ++k) {
            const std::int64_t at = start + static_cast<std::int64_t>(k);
            const bool inside = at >= 0 && std::cmp_less(at, x.size());
            buffer[k] = inside ? static_cast<double>(x[static_cast<std::size_t>(at)]) : 0.0;
        }
        const Yin::YinOutput out = yin.processProbabilisticYin(buffer.data());
        std::vector<std::pair<double, double>> pairs;
        for (const auto& [hz, p] : out.freqProb) {
            if (hz >= 50.0 && hz <= 1600.0) {
                pairs.emplace_back(12.0 * std::log2(hz / 440.0) + 69.0, p);
            }
        }
        frames.push_back(pairs);
        const auto obs = hmm.calculateObsProb(pairs);
        if (i == 0) {
            hmm.initialise(obs);
        } else {
            hmm.process(obs);
        }
    }
    const std::vector<int> path = hmm.track();
    REQUIRE(path.size() == t.f0.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const float hz = hmm.nearestFreq(path[i], frames[i]);
        INFO("frame " << i);
        if (hz > 0.0F) {
            CHECK(std::abs(t.f0[i].value() - (6900.0 + 1200.0 * std::log2(hz / 440.0))) < 1e-6);
        } else {
            CHECK(std::isnan(t.f0[i].value()));
        }
    }
}

TEST_CASE("c4dm_the_sub_octave_guard_restores_a_tone_whose_third_subharmonic_dips_deepest",
          "[c4dm_pyin]") {
    // Partials of f0 / 3: every third one strong (these make the signal periodic at f0), the
    // others weak but present (they make it exactly periodic only at 3 / f0). The CMND dips deepest
    // at the long lag; a dip at the true lag is nearly as deep.
    constexpr double k_f0 = 190.0;
    std::vector<double> amps(30, 0.12);
    for (std::size_t k = 2; k < amps.size(); k += 3) {
        amps[k] = 1.0;
    }
    const auto x = make_harmonic([](double) { return k_f0 / 3.0; }, 0.8, 44100.0, amps);
    const auto truth = truth_for([](double) { return k_f0; }, 161);
    const auto score = [&](const C4dmPYinParams& p) {
        const auto t = estimate(x, 44100.0, 1, p);
        const auto [a, b] = inner(t, 0.1);
        return score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b);
    };
    const auto off = score({.down_margin3 = -1.0, .down_margin2 = -1.0});
    const auto on = score({});
    CHECK(on.rpa() >= tolerance::k_rpa_min);
    CHECK(on.octave_errors == 0);
    CHECK(off.rpa() < on.rpa());  // the guard is what fixes it
}

TEST_CASE("c4dm_is_the_default_of_detect_pitch", "[c4dm_pyin]") {
    const auto x = make_harmonic([](double) { return 262.0; }, 0.5, 44100.0, harmonic_amps(6));
    const auto direct = estimate(x, 44100.0);
    const auto entry = detect_pitch(source_of(x, 44100.0), PitchParams{}).value();
    REQUIRE(direct.f0.size() == entry.f0.size());
    for (std::size_t i = 0; i < entry.f0.size(); ++i) {
        const bool same = (std::isnan(direct.f0[i].value()) && std::isnan(entry.f0[i].value())) ||
                          direct.f0[i] == entry.f0[i];
        REQUIRE(same);
    }
}

namespace {

std::size_t voiced_count(const PitchTrack& t) {
    return static_cast<std::size_t>(
        std::ranges::count_if(t.f0, [](Cents c) { return !std::isnan(c.value()); }));
}

}  // namespace

TEST_CASE("c4dm_fixed_lag_agrees_with_full_viterbi_and_decodes_every_frame", "[c4dm_pyin]") {
    const auto f0 = [](double t) { return 200.0 * std::pow(2.0, 0.5 * t); };
    const auto x = make_harmonic(f0, 1.2, 44100.0, harmonic_amps(8));
    const auto full = estimate(x, 44100.0);
    const auto lagged = estimate(x, 44100.0, 1, {.fixed_lag_frames = 50});
    REQUIRE(full.f0.size() == lagged.f0.size());
    CHECK(!std::isnan(lagged.f0[0].value()));  // frame 0 is not left undecoded
    std::size_t agree = 0;
    for (std::size_t i = 0; i < full.f0.size(); ++i) {
        agree += std::abs(full.f0[i].value() - lagged.f0[i].value()) <= 1.0 ? 1 : 0;
    }
    CHECK(static_cast<double>(agree) >= 0.95 * static_cast<double>(full.f0.size()));
    // a lag longer than the source decodes it all in one go and equals full Viterbi
    const auto huge = estimate(x, 44100.0, 1, {.fixed_lag_frames = 100000});
    for (std::size_t i = 0; i < full.f0.size(); ++i) {
        REQUIRE(huge.f0[i].value() == full.f0[i].value());
    }
}

TEST_CASE("c4dm_long_sources_get_a_bounded_lag_automatically", "[c4dm_pyin]") {
    const auto x = make_harmonic([](double) { return 220.0; }, 1.0, 44100.0, harmonic_amps(6));
    const auto automatic = estimate(x, 44100.0, 1, {.full_viterbi_max_duration = Seconds{0.5}});
    const auto explicit_lag = estimate(x, 44100.0, 1, {.fixed_lag_frames = 100});
    const auto full = estimate(x, 44100.0);
    REQUIRE(automatic.f0.size() == explicit_lag.f0.size());
    for (std::size_t i = 0; i < automatic.f0.size(); ++i) {
        REQUIRE(automatic.f0[i].value() == explicit_lag.f0[i].value());
    }
    CHECK(voiced_count(full) > 0);
}

TEST_CASE("c4dm_works_at_other_sample_rates_and_obeys_the_track_invariants", "[c4dm_pyin]") {
    for (const double rate : {8000.0, 11025.0, 22050.0, 44100.0, 96000.0}) {
        INFO("rate " << rate);
        const auto x = make_harmonic([](double) { return 262.0; }, 0.8, rate, harmonic_amps(6));
        const auto t = estimate(x, rate);
        const auto truth = truth_for([](double) { return 262.0; }, t.f0.size());
        const auto [a, b] = inner(t, 0.1);
        CHECK(score_pitch(t, truth, tolerance::k_pitch_dynamic.value(), a, b).rpa() >= 0.95);
        REQUIRE(validate(t).has_value());
        REQUIRE(t.alternates.size() == 2);
        for (std::size_t i = 0; i < t.f0.size(); ++i) {
            REQUIRE((t.voicing_prob[i] >= 0.0 && t.voicing_prob[i] <= 1.0));
            REQUIRE((t.confidence_prob[i] >= 0.0 && t.confidence_prob[i] <= 1.0));
            REQUIRE(is_voiced(t, i) == !std::isnan(t.f0[i].value()));
            REQUIRE(std::isnan(t.alternates[0][i].value()));
            REQUIRE(std::isnan(t.alternates[1][i].value()));
        }
    }
}

TEST_CASE("c4dm_candidates_outside_min_and_max_f0_are_dropped", "[c4dm_pyin]") {
    const auto x = make_harmonic([](double) { return 1000.0; }, 0.6, 44100.0, harmonic_amps(4));
    const auto limited = C4dmPYinEstimator{}
                             .estimate(source_of(x, 44100.0), PitchParams{.max_f0 = Hz{800.0}})
                             .value();
    for (const Cents c : limited.f0) {
        if (!std::isnan(c.value())) {
            REQUIRE(c.value() <= to_cents(Hz{800.0}, Hz{440.0}).value() + 50.0);
        }
    }
    // an effective floor of 61.7 Hz: a 55 Hz tone is not reported as itself
    const auto low = make_harmonic([](double) { return 55.0; }, 0.8, 44100.0, harmonic_amps(8));
    const auto t = estimate(low, 44100.0);
    const double truth = to_cents(Hz{55.0}, Hz{440.0}).value();
    for (const Cents c : t.f0) {
        if (!std::isnan(c.value())) {
            REQUIRE(std::abs(c.value() - truth) > 50.0);
        }
    }
}

TEST_CASE("c4dm_more_parameters_are_validated", "[c4dm_pyin]") {
    const AudioSource src = source_of(make_sine(220.0, 0.2, 44100.0), 44100.0);
    const double nan = std::nan("");
    for (const C4dmPYinParams& bad :
         {C4dmPYinParams{.low_amp_rms = nan}, C4dmPYinParams{.down_margin2 = nan},
          C4dmPYinParams{.max_analysis_rate = Hz{nan}}, C4dmPYinParams{.long_source_lag_frames = 0},
          C4dmPYinParams{.full_viterbi_max_duration = Seconds{0.0}}}) {
        CHECK(C4dmPYinEstimator{bad}.estimate(src, PitchParams{}).error().code ==
              ErrorCode::invalid_argument);
    }
    CHECK(C4dmPYinEstimator{}.estimate(src, PitchParams{.n_alternates = -1}).error().code ==
          ErrorCode::invalid_argument);
    // the analysis rate (8 kHz here) cannot represent max_f0 5 kHz even though the source can
    CHECK(C4dmPYinEstimator{{.max_analysis_rate = Hz{8000.0}}}
              .estimate(src, PitchParams{.max_f0 = Hz{5000.0}})
              .error()
              .code == ErrorCode::invalid_argument);
}

TEST_CASE("c4dm_frames_are_time_aligned_on_a_glide", "[c4dm_pyin]") {
    // 1200 cents per second: a time offset of d seconds is a pitch error of 1200 d cents. The
    // plugin's convention (block start + block / 4) must put the estimate at the frame time.
    const auto glide = [](double t) { return 200.0 * std::pow(2.0, t); };
    const auto x = make_harmonic(glide, 1.0, 44100.0, harmonic_amps(8));
    const auto t = estimate(x, 44100.0);
    const auto truth = truth_for(glide, t.f0.size());
    std::vector<double> errors;
    const auto [a, b] = inner(t, 0.1);
    for (std::size_t i = a; i < b; ++i) {
        if (!std::isnan(t.f0[i].value())) {
            errors.push_back(t.f0[i].value() - truth[i]);
        }
    }
    std::ranges::sort(errors);
    const double median_cents = errors[errors.size() / 2];
    const double bias_ms = 1000.0 * median_cents / 1200.0;
    INFO("median pitch error " << median_cents << " cents = " << bias_ms << " ms");
    CHECK(std::abs(bias_ms) <= 5.0);  // one hop; the plugin's own YIN timing is within a frame
}
