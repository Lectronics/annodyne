#include "core/analysis/gaps/estimate_noise_floor.h"
#include "support/synth_signals.h"
#include "support/test_data.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace anno;
using namespace anno::test;

namespace {

// Tolerances of this file only (statistics of seeded noise over many frames); proposed for
// tests/tolerances.h, which is the owner's.
constexpr double k_level_tol_db = 1.0;  // a floor level against the noise it was made from
constexpr double k_band_tol_db = 1.5;   // a band level against the theory
constexpr double k_sine_tol_db = 0.5;   // a sine in its band
constexpr double k_hop = 0.005;

std::size_t frame_at(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds / k_hop));
}

struct Scene {
    std::vector<float> samples;
    double rate = 44100.0;

    [[nodiscard]] AudioSource source() const {
        return AudioSource::from_buffer(to_buffer(samples, rate));
    }
    [[nodiscard]] FrameGrid grid() const {
        FrameGrid g;
        g.hop = Seconds{k_hop};
        g.start = SourceTime{};
        const double seconds = static_cast<double>(samples.size()) / rate;
        g.n_frames = static_cast<std::int64_t>(std::floor(seconds / k_hop)) + 1;
        return g;
    }
    [[nodiscard]] ActivityTrack activity() const {
        return detect_activity(MonoAudio{.samples = samples, .sample_rate = Hz{rate}}, grid(),
                               ActivityParams{})
            .value();
    }
    [[nodiscard]] Result<NoiseFloor> floor(const std::vector<Note>& notes = {},
                                           const NoiseFloorParams& p = {}) const {
        return estimate_noise_floor(source(), activity(), notes, p);
    }
};

Scene noise_scene(double seconds, double level_db, double rate = 44100.0, std::uint64_t seed = 3) {
    return {.samples = make_noise(seconds, rate, std::pow(10.0, level_db / 20.0), seed),
            .rate = rate};
}

void add_tone(Scene& s, double hz, double amplitude, double from_s, double to_s) {
    const auto first = static_cast<std::size_t>(from_s * s.rate);
    const auto last = std::min(s.samples.size(), static_cast<std::size_t>(to_s * s.rate));
    for (std::size_t i = first; i < last; ++i) {
        s.samples[i] += static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / s.rate));
    }
}

/// The nominal band ratio and centre of band j for the default parameters.
double band_ratio(const NoiseFloorParams& p = {}) {
    return std::pow(p.band_high.value() / p.band_low.value(), 1.0 / p.bands);
}
double nominal_centre(int j, const NoiseFloorParams& p = {}) {
    return p.band_low.value() * std::pow(band_ratio(p), j + 0.5);
}

}  // namespace

TEST_CASE("white_noise_reads_its_own_rms_level", "[noise_floor]") {
    const Scene s = noise_scene(8.0, -50.0);
    const NoiseFloor f = s.floor().value();
    REQUIRE(validate(f).has_value());
    CHECK(f.grid == s.grid());
    for (const double t : {1.0, 4.0, 7.0}) {
        CHECK(f.curve_dbfs[frame_at(t)].value() == Catch::Approx(-50.0).margin(k_level_tol_db));
    }
}

TEST_CASE("white_noise_bands_follow_their_share_of_the_spectrum", "[noise_floor]") {
    const Scene s = noise_scene(8.0, -50.0);
    const NoiseFloor f = s.floor().value();
    REQUIRE(f.band_centre.size() == 24);
    const NoiseFloorParams p;
    const double r = band_ratio(p);
    for (const int j : {6, 12, 18}) {
        const double centre = nominal_centre(j, p);
        CHECK(f.band_centre[static_cast<std::size_t>(j)].value() == Catch::Approx(centre));
        const double width = centre * (std::sqrt(r) - 1.0 / std::sqrt(r));
        const double expected = -50.0 + 10.0 * std::log10(width / (0.5 * s.rate));
        CHECK(f.band_dbfs[static_cast<std::size_t>(j)].value() ==
              Catch::Approx(expected).margin(k_band_tol_db));
    }
}

TEST_CASE("a_sine_reads_minus_3_dB_at_full_scale_in_its_band", "[noise_floor]") {
    // Amplitude 0.5: mean square 0.125, -9.03 dB RMS; the curve and the band both read it.
    const double hz = nominal_centre(12);
    Scene s{.samples = make_sine(hz, 6.0, 44100.0, 0.5), .rate = 44100.0};
    const NoiseFloor f = s.floor().value();
    CHECK(f.band_dbfs[12].value() == Catch::Approx(-9.03).margin(k_sine_tol_db));
    CHECK(f.band_dbfs[4].value() < -60.0);  // far from the sine
    CHECK(f.curve_dbfs[frame_at(3.0)].value() == Catch::Approx(-9.03).margin(k_sine_tol_db));
    Scene full{.samples = make_sine(hz, 6.0, 44100.0, 1.0), .rate = 44100.0};
    CHECK(full.floor().value().band_dbfs[12].value() == Catch::Approx(-3.01).margin(k_sine_tol_db));
}

TEST_CASE("frames_inside_notes_do_not_count", "[noise_floor]") {
    Scene s = noise_scene(6.0, -60.0);
    add_tone(s, 220.0, 0.3, 2.0, 4.0);  // loud, inside a note
    const std::vector<Note> notes{make_note("a", 2.0, 4.0)};
    const NoiseFloor f = s.floor(notes).value();
    CHECK(f.curve_dbfs[frame_at(3.0)].value() == Catch::Approx(-60.0).margin(k_level_tol_db));
    // Without the note the tone is two thirds of the 3 s window around 3 s: the 10 % quantile still
    // finds the noise, but a window that is mostly tone does not (a 1 s file of tone).
    Scene tone{.samples = make_sine(220.0, 1.0, 44100.0, 0.3), .rate = 44100.0};
    const NoiseFloor loud = tone.floor().value();
    CHECK(loud.curve_dbfs[frame_at(0.5)].value() > -20.0);
}

TEST_CASE("a_window_inside_a_long_note_takes_the_nearest_estimate", "[noise_floor]") {
    Scene s = noise_scene(6.0, -60.0);
    add_tone(s, 220.0, 0.3, 0.6, 5.4);
    const NoiseFloor f = s.floor({make_note("a", 0.6, 5.4)}).value();
    // 3.0 s is 1.5 s+ from any outside frame: the estimate of the nearest supported window.
    CHECK(f.curve_dbfs[frame_at(3.0)].value() == Catch::Approx(-60.0).margin(k_level_tol_db));
    CHECK(f.curve_dbfs[frame_at(2.0)].value() == Catch::Approx(-60.0).margin(k_level_tol_db));
}

TEST_CASE("a_recording_covered_by_notes_takes_the_global_floor", "[noise_floor]") {
    Scene s = noise_scene(4.0, -40.0);
    const ActivityTrack activity = s.activity();
    const NoiseFloor f =
        estimate_noise_floor(s.source(), activity, std::vector{make_note("a", 0.0, 4.0)}, {})
            .value();
    for (const Decibels v : f.curve_dbfs) {
        CHECK(v == activity.noise_floor);
    }
    for (const Decibels v : f.band_dbfs) {  // no floor frame: flat at the global floor
        CHECK(v == activity.noise_floor);
    }
}

TEST_CASE("a_rising_floor_is_followed", "[noise_floor]") {
    // -70 dB to -40 dB over 12 s, linear in dB.
    const double rate = 44100.0;
    Scene s{.samples = make_noise(12.0, rate, 1.0, 5), .rate = rate};
    for (std::size_t i = 0; i < s.samples.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        s.samples[i] *= static_cast<float>(std::pow(10.0, (-70.0 + 30.0 * t / 12.0) / 20.0));
    }
    const NoiseFloor f = s.floor().value();
    const double early = f.curve_dbfs[frame_at(2.0)].value();
    const double middle = f.curve_dbfs[frame_at(6.0)].value();
    const double late = f.curve_dbfs[frame_at(10.0)].value();
    CHECK(early < middle);
    CHECK(middle < late);
    CHECK(late - early > 15.0);
}

TEST_CASE("the_spectrum_ignores_loud_frames_outside_the_notes", "[noise_floor]") {
    Scene s = noise_scene(6.0, -60.0);
    const std::vector<float> burst = make_noise(1.0, 44100.0, 0.1, 9);  // -20 dB, not annotated
    std::ranges::copy(burst, s.samples.begin() + 2 * 44100L);
    const NoiseFloor f = s.floor().value();
    const NoiseFloorParams p;
    const double r = band_ratio(p);
    const double centre = nominal_centre(12, p);
    const double width = centre * (std::sqrt(r) - 1.0 / std::sqrt(r));
    const double expected = -60.0 + 10.0 * std::log10(width / (0.5 * s.rate));
    CHECK(f.band_dbfs[12].value() == Catch::Approx(expected).margin(k_band_tol_db));
}

TEST_CASE("bands_above_0_45_of_the_rate_are_dropped", "[noise_floor]") {
    const NoiseFloor low = noise_scene(3.0, -50.0, 22050.0).floor().value();
    std::size_t expected = 0;  // nominal bands whose upper edge is at most 0.45 x the rate
    while (80.0 * std::pow(band_ratio(), static_cast<double>(expected + 1)) <= 0.45 * 22050.0) {
        ++expected;
    }
    CHECK(expected == 23);
    CHECK(low.band_centre.size() == expected);
    CHECK(low.band_centre.back().value() < 0.45 * 22050.0);
    CHECK(low.band_dbfs.size() == low.band_centre.size());
    const NoiseFloor high = noise_scene(3.0, -50.0, 44100.0).floor().value();
    CHECK(high.band_centre.size() == 24);
    NoiseFloorParams few;
    few.bands = 1;
    const NoiseFloor one = noise_scene(3.0, -50.0).floor({}, few).value();
    REQUIRE(one.band_centre.size() == 1);
    CHECK(one.band_centre[0].value() == Catch::Approx(std::sqrt(80.0 * 12000.0)));
}

TEST_CASE("digital_silence_reads_the_level_floor", "[noise_floor]") {
    Scene s{.samples = std::vector<float>(3 * 44100UL, 0.0F), .rate = 44100.0};
    const NoiseFloor f = s.floor().value();
    for (const Decibels v : f.curve_dbfs) {
        CHECK(v == k_level_floor_db);
    }
    for (const Decibels v : f.band_dbfs) {
        CHECK(v.value() <= -119.0);
    }
}

TEST_CASE("a_recording_shorter_than_the_window_is_fine", "[noise_floor]") {
    const Scene s = noise_scene(0.5, -50.0);
    const NoiseFloor f = s.floor().value();
    CHECK(f.curve_dbfs.size() == static_cast<std::size_t>(s.grid().n_frames));
    CHECK(f.curve_dbfs[frame_at(0.25)].value() == Catch::Approx(-50.0).margin(k_level_tol_db));
}

TEST_CASE("the_estimate_is_deterministic", "[noise_floor]") {
    const Scene s = noise_scene(4.0, -50.0);
    const NoiseFloor a = s.floor().value();
    const NoiseFloor b = s.floor().value();
    CHECK(a.curve_dbfs == b.curve_dbfs);
    CHECK(a.band_dbfs == b.band_dbfs);
    CHECK(a.band_centre == b.band_centre);
}

TEST_CASE("bad_inputs_are_named_errors", "[noise_floor]") {
    const Scene s = noise_scene(2.0, -50.0);
    const auto code = [&](const NoiseFloorParams& p) { return s.floor({}, p).error().code; };
    NoiseFloorParams p;
    p.window = Seconds{0.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.percentile_prob = 1.0;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.percentile_prob = 0.0;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.bands = 0;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.band_low = Hz{500.0};
    p.band_high = Hz{400.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.band_low = Hz{0.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.spectrum_window = Seconds{-1.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.spectrum_margin = Decibels{-1.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.note_guard = Seconds{-0.01};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.min_support = Seconds{std::nan("")};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.max_spectrum_frames = 0;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.window = Seconds{std::nan("")};
    CHECK(code(p) == ErrorCode::invalid_argument);

    ActivityTrack bad = s.activity();
    bad.grid.n_frames += 1;  // does not match its arrays
    CHECK(estimate_noise_floor(s.source(), bad, {}, {}).error().code ==
          ErrorCode::invalid_argument);
    ActivityTrack short_level = s.activity();
    short_level.level.pop_back();
    CHECK(estimate_noise_floor(s.source(), short_level, {}, {}).error().code ==
          ErrorCode::invalid_argument);

    const Scene empty{.samples = {}, .rate = 44100.0};
    FrameGrid grid;
    grid.n_frames = 0;
    ActivityTrack none;
    none.grid = grid;
    CHECK(estimate_noise_floor(empty.source(), none, {}, {}).error().code ==
          ErrorCode::out_of_range);

    // Every band above 0.45 x the rate: nothing to report.
    NoiseFloorParams tiny;
    tiny.band_low = Hz{9000.0};
    tiny.band_high = Hz{12000.0};
    CHECK(noise_scene(2.0, -50.0, 16000.0).floor({}, tiny).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("a_window_with_too_little_outside_time_gives_no_estimate", "[noise_floor]") {
    // 0.4 s of noise before a note that runs to the end: with min_support 1 s no window has
    // enough support, so every frame takes the global floor.
    Scene s = noise_scene(3.0, -60.0);
    add_tone(s, 220.0, 0.3, 0.4, 3.0);
    const ActivityTrack activity = s.activity();
    NoiseFloorParams p;
    p.min_support = Seconds{1.0};
    const NoiseFloor f =
        estimate_noise_floor(s.source(), activity, std::vector{make_note("a", 0.4, 3.0)}, p)
            .value();
    for (const Decibels v : f.curve_dbfs) {
        CHECK(v == activity.noise_floor);
    }
}
