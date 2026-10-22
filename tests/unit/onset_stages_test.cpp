#include "core/analysis/onsets/onset_stages.h"
#include "core/analysis/onsets/onsets.h"
#include "core/analysis/orchestrator/analyse.h"
#include "support/onset_bench.h"
#include "support/onset_score.h"
#include "support/synth_signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

using namespace anno;
using namespace anno::test;
using namespace anno::test::onset_bench;
using Catch::Approx;

namespace {

constexpr double k_sr = 44100.0;

double peak_db(const std::vector<float>& x) {
    double p = 0.0;
    for (const float v : x) {
        p = std::max(p, std::abs(static_cast<double>(v)));
    }
    return 20.0 * std::log10(p);
}

double rms_db(const std::vector<float>& x) {
    return 20.0 * std::log10(rms(x));
}

/// A hand-made spectrogram: hop 5 ms, centres 100, 200, ... Hz.
BandSpectrogram make_bands(std::int64_t frames, std::int64_t bands, double fill = 0.0) {
    BandSpectrogram s;
    s.hop = Seconds{0.005};
    s.n_frames = frames;
    s.n_bands = bands;
    s.centres.reserve(static_cast<std::size_t>(bands));
    for (std::int64_t b = 0; b < bands; ++b) {
        s.centres.emplace_back(100.0 * static_cast<double>(b + 1));
    }
    s.data.assign(static_cast<std::size_t>(frames * bands), fill);
    return s;
}

double& at(BandSpectrogram& s, std::int64_t frame, std::int64_t band) {
    return s.data[static_cast<std::size_t>(frame * s.n_bands + band)];
}

Activation activation_of(std::vector<double> values, double hop_s = 0.01) {
    return Activation{.values = std::move(values), .hop = Seconds{hop_s}};
}

std::vector<double> to_seconds(const OnsetList& list) {
    std::vector<double> t;
    t.reserve(list.times.size());
    for (const SourceTime s : list.times) {
        t.push_back((s - SourceTime{}).value());
    }
    return t;
}

}  // namespace

// ---- P0 -------------------------------------------------------------------------------------

TEST_CASE("normalise_level_reaches_the_target_peak_or_rms_at_any_input_level", "[onset_stages]") {
    for (const double db : {0.0, -20.0, -40.0}) {
        const auto x = attenuated(make_sine(440.0, 1.0, k_sr, 0.5), db);
        const auto peak =
            normalise_level(x, Hz{k_sr}, {.mode = LevelNorm::peak, .target = Decibels{-6.0}})
                .value();
        CHECK(peak_db(peak.samples) == Approx(-6.0).margin(0.1));
        const auto level = normalise_level(x, Hz{k_sr}, {}).value();
        CHECK(rms_db(level.samples) == Approx(-23.0).margin(0.1));
        // The reported gain is what was applied.
        CHECK(rms_db(level.samples) - rms_db(x) == Approx(level.gain.value()).margin(1e-3));
    }
}

TEST_CASE("normalise_level_counts_only_the_active_frames_and_bounds_the_gain", "[onset_stages]") {
    // 1 s loud then 1 s at -100 dB: the quiet half is below the active floor and must not pull the
    // measured RMS down.
    auto loud = make_sine(440.0, 1.0, k_sr, 0.5);
    const auto quiet = make_sine(440.0, 1.0, k_sr, 0.5 * 1e-5);
    loud.insert(loud.end(), quiet.begin(), quiet.end());
    const auto out = normalise_level(loud, Hz{k_sr}, {}).value();
    CHECK(out.gain.value() == Approx(-23.0 - 20.0 * std::log10(0.5 / std::sqrt(2.0))).margin(0.05));
    // A very quiet file: the gain stops at max_gain and the level stays below the target.
    const auto faint = make_sine(440.0, 1.0, k_sr, 1e-5);
    const auto limited = normalise_level(faint, Hz{k_sr}, {}).value();
    CHECK(limited.gain.value() == Approx(40.0).margin(1e-9));
    CHECK(rms_db(limited.samples) < -23.0 - 10.0);
}

TEST_CASE("normalise_level_leaves_silence_alone_and_never_writes_its_input", "[onset_stages]") {
    const std::vector<float> silence(4410, 0.0F);
    const auto out = normalise_level(silence, Hz{k_sr}, {}).value();
    CHECK(out.gain.value() == 0.0);
    CHECK(out.samples == silence);
    auto x = make_sine(440.0, 0.5, k_sr, 0.2);
    const auto copy = x;
    (void)normalise_level(x, Hz{k_sr}, {}).value();
    CHECK(x == copy);
    CHECK(normalise_level({}, Hz{k_sr}, {}).value().samples.empty());
    auto bad = x;
    bad[10] = std::nanf("");
    CHECK(normalise_level(bad, Hz{k_sr}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(normalise_level(x, Hz{0.0}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(normalise_level(x, Hz{k_sr}, {.max_gain = Decibels{-1.0}}).error().code ==
          ErrorCode::invalid_argument);
}

// ---- P1 -------------------------------------------------------------------------------------

TEST_CASE("band_spectrogram_counts_frames_and_returns_the_actual_hop", "[onset_stages]") {
    const auto x = make_sine(1000.0, 1.0, k_sr);
    const auto bands = band_spectrogram(x, Hz{k_sr}, {}).value();
    // hop 5 ms = 220.5 samples rounds to 221.
    CHECK(bands.hop.value() == Approx(221.0 / k_sr));
    CHECK(bands.n_frames == static_cast<std::int64_t>(x.size()) / 221 + 1);
    CHECK(bands.n_bands > 100);
    CHECK(validate(bands).has_value());
    CHECK(std::ranges::is_sorted(bands.centres, {}, &Hz::value));
}

TEST_CASE("band_spectrogram_gives_no_frames_for_a_short_or_empty_signal", "[onset_stages]") {
    const auto short_signal = make_sine(1000.0, 0.02, k_sr);  // < one 2048-sample window
    for (const auto& x : {short_signal, std::vector<float>{}}) {
        const auto bands = band_spectrogram(x, Hz{k_sr}, {}).value();
        CHECK(bands.n_frames == 0);
        CHECK(bands.data.empty());
        CHECK(bands.hop.value() == Approx(221.0 / k_sr));
    }
}

TEST_CASE("a_sine_lights_the_band_nearest_its_frequency", "[onset_stages]") {
    for (const double hz : {500.0, 1000.0, 3000.0}) {
        const auto x = make_sine(hz, 1.0, k_sr);
        const auto bands = band_spectrogram(x, Hz{k_sr}, {}).value();
        const auto frame = static_cast<std::size_t>(bands.n_frames / 2);
        const auto row = std::span<const double>(bands.data)
                             .subspan(frame * static_cast<std::size_t>(bands.n_bands),
                                      static_cast<std::size_t>(bands.n_bands));
        const auto peak = static_cast<std::size_t>(std::ranges::max_element(row) - row.begin());
        INFO(hz);
        // Within one band (a band is 1/24 octave) of the frequency.
        CHECK(std::abs(std::log2(bands.centres[peak].value() / hz)) < 1.5 / 24.0);
        // Bands four or more steps away are at least 12 dB down.
        for (std::size_t b = 0; b < row.size(); ++b) {
            const auto distance = b > peak ? b - peak : peak - b;
            if (distance >= 4) {
                CHECK(20.0 * std::log10(row[b] / row[peak]) < -12.0);
            }
        }
    }
}

TEST_CASE("band_spectrogram_rejects_bad_parameters", "[onset_stages]") {
    const auto x = make_sine(1000.0, 0.5, k_sr);
    const auto code = [&](BandParams p, double rate = k_sr) {
        return band_spectrogram(x, Hz{rate}, p).error().code;
    };
    CHECK(code({.window = Seconds{0.0}}) == ErrorCode::invalid_argument);
    CHECK(code({.hop = Seconds{-1.0}}) == ErrorCode::invalid_argument);
    CHECK(code({.bands_per_octave = 0}) == ErrorCode::invalid_argument);
    CHECK(code({.f_min = Hz{5000.0}, .f_max = Hz{1000.0}}) == ErrorCode::invalid_argument);
    CHECK(code({}, 0.0) == ErrorCode::invalid_argument);
}

TEST_CASE("validate_rejects_an_inconsistent_band_spectrogram", "[onset_stages]") {
    auto s = make_bands(4, 3);
    CHECK(validate(s).has_value());
    auto wrong_size = s;
    wrong_size.data.pop_back();
    CHECK_FALSE(validate(wrong_size).has_value());
    auto unsorted = s;
    std::swap(unsorted.centres[0], unsorted.centres[1]);
    CHECK_FALSE(validate(unsorted).has_value());
    auto negative = s;
    negative.data[0] = -1.0;
    CHECK_FALSE(validate(negative).has_value());
    auto nan = s;
    nan.data[1] = std::nan("");
    CHECK_FALSE(validate(nan).has_value());
}

// ---- P2 -------------------------------------------------------------------------------------

TEST_CASE("adaptive_whiten_follows_the_decaying_maximum_formula", "[onset_stages]") {
    // One band, x = 1, 0.5, 2 (hop 5 ms, relax chosen so decay = 0.5): p0 = max(1, floor) = 1;
    // p1 = max(0.5, 0.5 * 1, floor) = 0.5; p2 = max(2, 0.5 * 0.5, floor) = 2.
    auto s = make_bands(3, 1);
    at(s, 0, 0) = 1.0;
    at(s, 1, 0) = 0.5;
    at(s, 2, 0) = 2.0;
    const double relax = -0.005 / std::log(0.5);
    const auto w = adaptive_whiten(s, {.relax = Seconds{relax}, .floor_magnitude = 1e-4}).value();
    CHECK(w.data[0] == Approx(1.0).margin(1e-12));
    CHECK(w.data[1] == Approx(1.0).margin(1e-12));
    CHECK(w.data[2] == Approx(1.0).margin(1e-12));
    // After the maximum, quiet frames decay as p0 * decay^n while that exceeds x: y = x / p.
    auto q = make_bands(5, 1);
    at(q, 0, 0) = 1.0;
    for (std::int64_t f = 1; f < 5; ++f) {
        at(q, f, 0) = 0.01;
    }
    const auto wq = adaptive_whiten(q, {.relax = Seconds{relax}, .floor_magnitude = 1e-4}).value();
    for (std::int64_t f = 1; f < 5; ++f) {
        CHECK(wq.data[static_cast<std::size_t>(f)] ==
              Approx(0.01 / std::pow(0.5, static_cast<double>(f))).margin(1e-12));
    }
}

TEST_CASE("adaptive_whiten_stays_in_zero_one_and_finite_on_silence", "[onset_stages]") {
    auto s = make_bands(50, 4);
    for (std::int64_t f = 10; f < 20; ++f) {
        for (std::int64_t b = 0; b < 4; ++b) {
            at(s, f, b) = 0.5 + 0.1 * static_cast<double>(b);
        }
    }
    const auto w = adaptive_whiten(s, {}).value();
    for (const double v : w.data) {
        CHECK(std::isfinite(v));
        CHECK(v >= 0.0);
        CHECK(v <= 1.0);
    }
    CHECK(adaptive_whiten(s, {.relax = Seconds{0.0}}).error().code == ErrorCode::invalid_argument);
    CHECK(adaptive_whiten(s, {.floor_magnitude = 0.0}).error().code == ErrorCode::invalid_argument);
}

// ---- P3 -------------------------------------------------------------------------------------

TEST_CASE("log_compress_is_log1p_of_the_gain_times_the_value", "[onset_stages]") {
    auto s = make_bands(1, 4);
    at(s, 0, 1) = std::numbers::e - 1.0;  // log(1 + (e - 1)) = 1
    at(s, 0, 2) = 3.0;
    at(s, 0, 3) = 10.0;
    const auto l = log_compress(s, {.log_gain = 1.0}).value();
    CHECK(l.data[0] == 0.0);
    CHECK(l.data[1] == Approx(1.0).margin(1e-12));
    CHECK(l.data[2] == Approx(std::log(4.0)).margin(1e-12));
    CHECK(l.data[3] == Approx(std::log(11.0)).margin(1e-12));
    const auto gained = log_compress(s, {.log_gain = 1000.0}).value();
    CHECK(gained.data[1] == Approx(std::log1p(1000.0 * (std::numbers::e - 1.0))).margin(1e-12));
    CHECK(std::ranges::is_sorted(gained.data, {}, [](double v) { return v; }) ==
          std::ranges::is_sorted(s.data));
    CHECK(log_compress(s, {.log_gain = 0.0}).error().code == ErrorCode::invalid_argument);
}

// ---- D --------------------------------------------------------------------------------------

TEST_CASE("superflux_of_a_constant_spectrogram_is_zero_and_the_first_frames_are_skipped",
          "[onset_stages]") {
    const auto flat = superflux(make_bands(20, 5, 1.0), {}).value();
    CHECK(flat.hop.value() == Approx(0.005));
    CHECK(flat.values.size() == 20);
    for (const double v : flat.values) {
        CHECK(v == 0.0);
    }
}

TEST_CASE("superflux_is_the_positive_rise_over_the_lagged_neighbourhood_maximum",
          "[onset_stages]") {
    // hop 5 ms, lag 15 ms -> mu = 3. Band 2 of 5 steps from 0 to 1 at frame 5 and stays.
    auto s = make_bands(12, 5);
    for (std::int64_t f = 5; f < 12; ++f) {
        at(s, f, 2) = 1.0;
    }
    const auto a = superflux(s, {}).value();
    for (std::size_t f = 0; f < 12; ++f) {
        INFO(f);
        CHECK(a.values[f] == Approx(f >= 5 && f <= 7 ? 0.2 : 0.0).margin(1e-12));
    }
}

TEST_CASE("the_frequency_max_filter_ignores_a_one_band_move_and_plain_flux_does_not",
          "[onset_stages]") {
    // The energy sits in band 2, then moves to band 3 at frame 5 (a partial gliding in vibrato).
    auto s = make_bands(12, 6);
    for (std::int64_t f = 0; f < 12; ++f) {
        at(s, f, f < 5 ? 2 : 3) = 1.0;
    }
    const auto filtered = superflux(s, {.max_filter_bins = 3}).value();
    const auto plain = superflux(s, {.max_filter_bins = 1}).value();
    for (std::size_t f = 5; f <= 7; ++f) {
        CHECK(filtered.values[f] == 0.0);
        CHECK(plain.values[f] == Approx(1.0 / 6.0).margin(1e-12));
    }
}

TEST_CASE("superflux_rejects_an_even_filter_width_and_a_bad_lag_and_handles_no_frames",
          "[onset_stages]") {
    const auto s = make_bands(10, 3);
    CHECK(superflux(s, {.max_filter_bins = 2}).error().code == ErrorCode::invalid_argument);
    CHECK(superflux(s, {.max_filter_bins = 0}).error().code == ErrorCode::invalid_argument);
    CHECK(superflux(s, {.lag = Seconds{0.0}}).error().code == ErrorCode::invalid_argument);
    const auto none = superflux(make_bands(0, 3), {}).value();
    CHECK(none.values.empty());
    CHECK(none.hop.value() == Approx(0.005));
}

// ---- K --------------------------------------------------------------------------------------

namespace {

// hop 10 ms: pre_max = post_max = 3 frames, pre_avg = 10, post_avg = 7, min_gap = 3 frames.
WindowedPeakParams picker(double delta = 0.1) {
    return {.pre_max = Seconds{0.03},
            .post_max = Seconds{0.03},
            .pre_avg = Seconds{0.10},
            .post_avg = Seconds{0.07},
            .min_gap = Seconds{0.03},
            .delta_activation = delta};
}

std::vector<std::size_t> frames_of(const PeakList& list) {
    std::vector<std::size_t> f;
    f.reserve(list.peaks.size());
    for (const ActivationPeak& p : list.peaks) {
        f.push_back(p.frame);
    }
    return f;
}

}  // namespace

TEST_CASE("a_single_peak_is_picked_with_strength_over_mean_plus_delta", "[onset_stages]") {
    std::vector<double> a(60, 0.0);
    a[30] = 1.0;
    const auto peaks = pick_peaks_windowed(activation_of(a), picker()).value();
    REQUIRE(peaks.peaks.size() == 1);
    CHECK(peaks.peaks[0].frame == 30);
    // Mean over [20, 37] = 18 frames of width: 1 / 18.
    CHECK(peaks.peaks[0].strength == Approx(1.0 / (1.0 / 18.0 + 0.1)).margin(1e-12));
    CHECK(peaks.hop.value() == Approx(0.01));
}

TEST_CASE("a_frame_that_is_not_the_window_maximum_is_not_a_peak", "[onset_stages]") {
    std::vector<double> a(60, 0.0);
    a[30] = 1.0;
    a[32] = 1.5;
    CHECK(frames_of(pick_peaks_windowed(activation_of(a), picker()).value()) ==
          std::vector<std::size_t>{32});
}

TEST_CASE("an_equal_neighbour_to_the_left_loses_the_tie_so_the_first_frame_wins",
          "[onset_stages]") {
    std::vector<double> a(60, 0.0);
    a[30] = 1.0;
    a[31] = 1.0;
    CHECK(frames_of(pick_peaks_windowed(activation_of(a), picker()).value()) ==
          std::vector<std::size_t>{30});
}

TEST_CASE("a_peak_under_the_local_mean_plus_delta_is_dropped", "[onset_stages]") {
    std::vector<double> a(100, 1.0);
    a[50] = 1.05;  // mean of its window is about 1.003; threshold about 1.103
    const auto frames = frames_of(pick_peaks_windowed(activation_of(a), picker()).value());
    CHECK(std::ranges::find(frames, 50U) == frames.end());
}

TEST_CASE("inside_min_gap_the_first_kept_peak_wins_even_if_weaker", "[onset_stages]") {
    // pre/post max 2 frames, min_gap 5 frames: peaks at 30 (1.0) and 34 (1.2) are both window
    // maxima, 4 frames apart, so the second is dropped.
    std::vector<double> a(80, 0.0);
    a[30] = 1.0;
    a[34] = 1.2;
    auto p = picker();
    p.pre_max = Seconds{0.02};
    p.post_max = Seconds{0.02};
    p.min_gap = Seconds{0.05};
    CHECK(frames_of(pick_peaks_windowed(activation_of(a), p).value()) ==
          std::vector<std::size_t>{30});
}

TEST_CASE("zero_activation_is_never_a_peak_even_with_delta_zero_and_ends_count", "[onset_stages]") {
    CHECK(pick_peaks_windowed(activation_of(std::vector<double>(50, 0.0)), picker(0.0))
              .value()
              .peaks.empty());
    std::vector<double> a(40, 0.0);
    a[39] = 1.0;  // the last frame
    CHECK(frames_of(pick_peaks_windowed(activation_of(a), picker()).value()) ==
          std::vector<std::size_t>{39});
    a[39] = 0.0;
    a[0] = 1.0;  // the first frame
    CHECK(frames_of(pick_peaks_windowed(activation_of(a), picker()).value()) ==
          std::vector<std::size_t>{0});
    CHECK(pick_peaks_windowed(activation_of({}), picker()).value().peaks.empty());
}

TEST_CASE("pick_peaks_windowed_rejects_bad_parameters", "[onset_stages]") {
    const auto a = activation_of(std::vector<double>(10, 1.0));
    auto negative = picker();
    negative.pre_max = Seconds{-0.01};
    CHECK(pick_peaks_windowed(a, negative).error().code == ErrorCode::invalid_argument);
    CHECK(pick_peaks_windowed(a, picker(-1.0)).error().code == ErrorCode::invalid_argument);
    CHECK(pick_peaks_windowed(activation_of({1.0}, 0.0), picker()).error().code ==
          ErrorCode::invalid_argument);
}

// ---- L --------------------------------------------------------------------------------------

namespace {

/// Silence with a sine that starts at `start` samples with a cosine attack of `attack` samples.
std::vector<float> onset_signal(std::size_t start, std::size_t attack, std::size_t total = 44100) {
    std::vector<float> x(total, 0.0F);
    for (std::size_t i = start; i < total; ++i) {
        const double ramp = i - start < attack ? cos_ramp(i - start, attack) : 1.0;
        x[i] = static_cast<float>(
            0.5 * ramp * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / k_sr));
    }
    return x;
}

PeakList peak_at(double seconds, double strength = 2.0) {
    const double hop = 0.005;
    return {.hop = Seconds{hop},
            .peaks = {{.frame = static_cast<std::size_t>(std::llround(seconds / hop)),
                       .strength = strength}}};
}

}  // namespace

TEST_CASE("localise_puts_a_sharp_onset_on_its_sample_from_a_coarse_frame", "[onset_stages]") {
    const std::size_t start = 22050;          // 0.5 s
    const auto x = onset_signal(start, 220);  // 5 ms attack
    // The coarse peak is 10 ms late.
    const auto out = localise(x, Hz{k_sr}, peak_at(0.51), {}).value();
    REQUIRE(out.size() == 1);
    CHECK(out[0].outcome == LocaliseOutcome::rose);
    CHECK(std::abs(out[0].sample - static_cast<std::int64_t>(start)) < 44);  // 1 ms
    CHECK(out[0].time == SourceTime{} + Seconds{static_cast<double>(out[0].sample) / k_sr});
    CHECK(out[0].strength == 2.0);
}

TEST_CASE("localise_finds_the_start_of_a_slow_attack_inside_the_ramp", "[onset_stages]") {
    const std::size_t start = 22050;
    const std::size_t attack = 3528;  // 80 ms
    const auto x = onset_signal(start, attack);
    const auto out = localise(x, Hz{k_sr}, peak_at(0.5), {}).value();
    REQUIRE(out.size() == 1);
    // The halfway point (in dB) between the floor and the top is reached in the first fifth of the
    // ramp, never before the ramp starts.
    const auto first = static_cast<std::int64_t>(start);
    CHECK(out[0].sample >= first - 2);
    CHECK(out[0].sample <= first + static_cast<std::int64_t>(attack / 5));
}

TEST_CASE("localise_drops_a_note_end_and_leaves_a_steady_signal_at_the_frame_time",
          "[onset_stages]") {
    // A note that stops: flux fires there too, but it is not an onset.
    auto x = onset_signal(0, 1);
    std::fill(x.begin() + 22050, x.end(), 0.0F);
    CHECK(localise(x, Hz{k_sr}, peak_at(0.5), {}).value().empty());
    // A steady sine: no rise, so the frame's own sample.
    const auto steady = make_sine(440.0, 1.0, k_sr, 0.5);
    const auto out = localise(steady, Hz{k_sr}, peak_at(0.5), {}).value();
    REQUIRE(out.size() == 1);
    CHECK(out[0].outcome == LocaliseOutcome::unchanged);
    CHECK(out[0].sample == 22050);
}

TEST_CASE("localise_rejects_bad_parameters_and_a_peak_beyond_the_signal", "[onset_stages]") {
    const auto x = onset_signal(22050, 220);
    CHECK(localise(x, Hz{k_sr}, peak_at(5.0), {}).error().code == ErrorCode::invalid_argument);
    CHECK(localise(x, Hz{k_sr}, peak_at(0.5), {.search = Seconds{0.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(localise(x, Hz{k_sr}, peak_at(0.5), {.min_rise = Decibels{-1.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(localise(x, Hz{k_sr}, PeakList{.hop = Seconds{0.0}, .peaks = {}}, {}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(localise({}, Hz{k_sr}, PeakList{.hop = Seconds{0.005}, .peaks = {}}, {}).value().empty());
}

// ---- the composed pipeline on the bench -----------------------------------------------------
// Pinned from the bench run of 2026-10-22 (defaults chosen on these same signals: optimistic).

namespace {

OnsetScore run_pipeline(const Family& f, const TransientParams& p, double tolerance = 0.025,
                        double attenuation_db = 0.0, bool consonants = false) {
    const auto src =
        AudioSource::from_buffer(to_buffer(attenuated(f.samples, attenuation_db), k_sr));
    const auto times = to_seconds(detect_transients(src, p).value());
    return score_onsets(times, consonants ? f.consonant_truth : f.truth, tolerance);
}

TransientParams normalised() {
    TransientParams p;
    p.level = LevelNormParams{};
    return p;
}

}  // namespace

TEST_CASE("the_pipeline_finds_every_clean_onset_with_no_false_positive", "[onset_stages]") {
    for (const Family& f : {family_a(), family_a0(), family_d(), family_f()}) {
        INFO(f.name);
        const auto s = run_pipeline(f, normalised());
        CHECK(std::cmp_equal(s.tp, f.truth.size()));
        CHECK(s.fp == 0);
        CHECK(std::abs(s.median_offset_s()) < 0.006);
    }
}

TEST_CASE("the_pipeline_finds_soft_attacks_and_consonant_starts", "[onset_stages]") {
    const auto b = run_pipeline(family_b(), normalised());
    CHECK(b.tp >= 7);  // 7 of 8 on the first run
    CHECK(b.fp == 0);
    const auto e = family_e();
    const auto consonants = run_pipeline(e, normalised(), 0.025, 0.0, true);
    CHECK(consonants.tp == 8);  // every consonant start; the vowels come with extra detections
}

TEST_CASE("legato_and_fast_notes_are_the_recorded_weak_cases", "[onset_stages]") {
    // C (pitch changes at constant level) and G (8 notes per second) are not solved by spectral
    // flux alone; the numbers are pinned so a regression shows and an improvement is noticed.
    const auto c = run_pipeline(family_c(), normalised());
    CHECK(c.tp >= 3);
    CHECK(c.fp <= 5);
    const auto g = run_pipeline(family_g(), normalised());
    CHECK(g.tp >= 10);
    CHECK(g.fp <= 6);
    // Known issue: the localiser puts G's onsets about 23 ms early (research: +7 ms for aubio).
    CHECK(g.median_offset_s() > -0.030);
}

TEST_CASE("level_normalisation_makes_the_fixed_threshold_level_independent", "[onset_stages]") {
    const auto pooled = [](const TransientParams& p, double db) {
        OnsetScore total;
        for (const Family& f : pooled_families()) {
            accumulate(total, run_pipeline(f, p, 0.025, db));
        }
        return total;
    };
    const double reference = pooled(normalised(), 0.0).f_measure();
    CHECK(reference > 0.85);
    for (const double db : {-10.0, -20.0, -30.0, -40.0}) {
        INFO(db);
        CHECK(std::abs(pooled(normalised(), db).f_measure() - reference) < 0.01);
    }
    // Without P0 the same fixed threshold loses the onsets once the level is far enough down.
    CHECK(pooled(TransientParams{}, -40.0).f_measure() < 0.2);
}

// ---- the method switch of detect_onsets / analyse() -----------------------------------------

TEST_CASE("analyse_runs_the_staged_pipeline_by_default_and_onset_params_default_is_phase_3",
          "[onset_stages]") {
    CHECK(AnalyseParams{}.onsets.method == SpectralMethod::windowed_peaks);
    CHECK(AnalyseParams{}.onsets.pipeline.level.has_value());
    CHECK(OnsetParams{}.method == SpectralMethod::adaptive_threshold);
    const OnsetParams analysis = analysis_onset_params();
    CHECK(analysis.method == SpectralMethod::windowed_peaks);
    CHECK(analysis.pipeline.log.log_gain == 1.0);
    CHECK(analysis.pipeline.peaks.delta_activation == Approx(0.05));
}

TEST_CASE("detect_onsets_checks_the_shared_fields_for_both_methods_and_the_chain_for_phase_3",
          "[onset_stages]") {
    const auto src = AudioSource::from_buffer(to_buffer(family_a().samples, k_sr));
    for (const SpectralMethod method :
         {SpectralMethod::adaptive_threshold, SpectralMethod::windowed_peaks}) {
        OnsetParams bad_step;
        bad_step.method = method;
        bad_step.energy_step_window = Seconds{0.0};
        CHECK(detect_onsets(src, bad_step).error().code == ErrorCode::invalid_argument);
        OnsetParams bad_gap;
        bad_gap.method = method;
        bad_gap.min_gap = Seconds{-1.0};
        CHECK(detect_onsets(src, bad_gap).error().code == ErrorCode::invalid_argument);
    }
    // A Phase 3 field is checked only for the Phase 3 chain; the pipeline checks its own.
    OnsetParams chain;
    chain.threshold_window = Seconds{0.0};
    CHECK(detect_onsets(src, chain).error().code == ErrorCode::invalid_argument);
    OnsetParams pipeline = analysis_onset_params();
    pipeline.threshold_window = Seconds{0.0};
    CHECK(detect_onsets(src, pipeline).has_value());
    pipeline.pipeline.peaks.delta_activation = -1.0;
    CHECK(detect_onsets(src, pipeline).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("the_two_methods_and_the_energy_step_detector_on_the_bench_families", "[onset_stages]") {
    // Recorded 2026-10-22 (docs/design/phase5-transient-pipeline.md, ADR-0022). The energy-step
    // detector (legato level steps, ADR-0010a) false-fires on synthetic tremolo (family D: 23) and
    // on noise (family F: 8) in BOTH methods; with it off both spectral chains are clean there.
    // The pipeline finds more of the fast notes (G: 11 of 16 against 6) and one soft attack fewer
    // than Phase 3 (B: 7 of 8 against 8).
    const auto score = [](const Family& f, OnsetParams p, bool energy_steps) {
        if (!energy_steps) {
            p.energy_step_db = Decibels{200.0};
        }
        const auto src = AudioSource::from_buffer(to_buffer(f.samples, k_sr));
        return score_onsets(to_seconds(detect_onsets(src, p).value()), f.truth, 0.025);
    };
    for (const Family& f : {family_a(), family_a0(), family_d(), family_f()}) {
        INFO(f.name);
        for (const OnsetParams& p : {OnsetParams{}, analysis_onset_params()}) {
            const auto clean = score(f, p, false);
            CHECK(std::cmp_equal(clean.tp, f.truth.size()));
            CHECK(clean.fp == 0);
        }
    }
    CHECK(score(family_b(), OnsetParams{}, false).tp == 8);
    CHECK(score(family_b(), analysis_onset_params(), false).tp >= 7);
    CHECK(score(family_b(), analysis_onset_params(), false).fp == 0);
    for (const OnsetParams& p : {OnsetParams{}, analysis_onset_params()}) {
        CHECK(score(family_d(), p, true).fp >= 10);
        CHECK(score(family_f(), p, true).fp >= 4);
    }
    CHECK(score(family_g(), analysis_onset_params(), false).tp >
          score(family_g(), OnsetParams{}, false).tp);
}
