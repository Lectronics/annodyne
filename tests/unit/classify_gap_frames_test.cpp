#include "core/analysis/gaps/classify_gap_frames.h"
#include "core/analysis/gaps/estimate_noise_floor.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;
constexpr double k_hop = 0.005;

enum class Shape { white, low_passed, high_passed };

/// Seeded noise of the given RMS (dB) in a spectral shape: high-passed = first difference (centroid
/// near Nyquist), low-passed = three cascaded 16-sample moving averages (centroid in the low kHz).
std::vector<float> shaped_noise(double seconds, Shape shape, double rms_db, std::uint64_t seed) {
    std::vector<float> x = make_noise(seconds, k_rate, 1.0, seed);
    std::vector<float> y(x.size(), 0.0F);
    for (std::size_t i = 1; i < x.size(); ++i) {
        y[i] = shape == Shape::high_passed ? x[i] - x[i - 1] : x[i];
    }
    if (shape == Shape::low_passed) {
        for (int pass = 0; pass < 3; ++pass) {  // three moving averages: sidelobes well down
            std::vector<float> smoothed(y.size(), 0.0F);
            for (std::size_t i = 16; i < y.size(); ++i) {
                float acc = 0.0F;
                for (std::size_t k = 0; k < 16; ++k) {
                    acc += y[i - k];
                }
                smoothed[i] = acc / 16.0F;
            }
            y = std::move(smoothed);
        }
    }
    double ms = 0.0;
    for (const float v : y) {
        ms += static_cast<double>(v) * v;
    }
    const double scale =
        std::pow(10.0, rms_db / 20.0) / std::sqrt(ms / static_cast<double>(y.size()));
    for (float& v : y) {
        v = static_cast<float>(v * scale);
    }
    return y;
}

void add_into(std::vector<float>& x, const std::vector<float>& burst, double at_s) {
    const auto first = static_cast<std::size_t>(at_s * k_rate);
    for (std::size_t i = 0; i < burst.size() && first + i < x.size(); ++i) {
        x[first + i] += burst[i];
    }
}

struct Scene {
    MonoAudio mono;
    ActivityTrack activity;
    NoiseFloor floor;
};

Scene scene_of(std::vector<float> samples, double rate = k_rate) {
    Scene s;
    s.mono = MonoAudio{.samples = std::move(samples), .sample_rate = Hz{rate}};
    FrameGrid grid;
    grid.hop = Seconds{k_hop};
    grid.start = SourceTime{};
    grid.n_frames = static_cast<std::int64_t>(
                        std::floor(static_cast<double>(s.mono.samples.size()) / rate / k_hop)) +
                    1;
    s.activity = detect_activity(s.mono, grid, ActivityParams{}).value();
    s.floor = estimate_noise_floor(AudioSource::from_buffer(to_buffer(s.mono.samples, rate)),
                                   s.activity, {}, {})
                  .value();
    return s;
}

std::size_t frame_at(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds / k_hop));
}

/// Number of frames in [from_s, to_s) with class c.
std::size_t count_class(const GapEvidence& ev, double from_s, double to_s, FrameClass c) {
    std::size_t n = 0;
    for (std::size_t i = frame_at(from_s); i < frame_at(to_s); ++i) {
        n += ev.frame_class[i] == c ? 1 : 0;
    }
    return n;
}

double min_excess(const GapEvidence& ev, double from_s, double to_s) {
    double lowest = 1e9;
    for (std::size_t i = frame_at(from_s); i < frame_at(to_s); ++i) {
        lowest = std::min(lowest, ev.excess_db[i].value());
    }
    return lowest;
}

double max_excess(const GapEvidence& ev, double from_s, double to_s) {
    double highest = -1e9;
    for (std::size_t i = frame_at(from_s); i < frame_at(to_s); ++i) {
        highest = std::max(highest, ev.excess_db[i].value());
    }
    return highest;
}

/// The longest run of consecutive frames in [from_s, to_s) whose excess is above `margin_db`.
std::size_t longest_run_above(const GapEvidence& ev, double from_s, double to_s, double margin_db) {
    std::size_t best = 0;
    std::size_t run = 0;
    for (std::size_t i = frame_at(from_s); i < frame_at(to_s); ++i) {
        run = ev.excess_db[i].value() > margin_db ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

/// A low-passed room-noise floor at -65 dB for `seconds`.
std::vector<float> floor_only(double seconds) {
    return shaped_noise(seconds, Shape::low_passed, -65.0, 3);
}

GapEvidence classify(const Scene& s, const GapEvidenceParams& p = {}) {
    return classify_gap_frames(s.mono, s.activity, s.floor, p).value();
}

}  // namespace

TEST_CASE("a_high_passed_noise_burst_is_a_sibilant_well_above_the_floor", "[classify_gap_frames]") {
    std::vector<float> x = floor_only(10.0);
    add_into(x, shaped_noise(0.2, Shape::high_passed, -45.0, 11), 4.0);
    const GapEvidence ev = classify(scene_of(x));
    CHECK(ev.frame_class.size() == static_cast<std::size_t>(ev.grid.n_frames));
    CHECK(count_class(ev, 4.04, 4.16, FrameClass::sibilant) >= 20U);  // of 24 interior frames
    CHECK(min_excess(ev, 4.04, 4.16) > 8.0);
}

TEST_CASE("a_low_passed_noise_stretch_is_a_breath", "[classify_gap_frames]") {
    std::vector<float> x = floor_only(10.0);
    add_into(x, shaped_noise(0.3, Shape::low_passed, -52.0, 12), 5.0);
    const GapEvidence ev = classify(scene_of(x));
    CHECK(count_class(ev, 5.05, 5.25, FrameClass::breath) >= 30U);  // of 40 interior frames
    CHECK(min_excess(ev, 5.05, 5.25) > 5.0);
}

TEST_CASE("a_broadband_click_after_a_quiet_stretch_is_a_plosive", "[classify_gap_frames]") {
    std::vector<float> x = floor_only(10.0);
    add_into(x, shaped_noise(0.010, Shape::white, -20.0, 13), 6.0);
    const GapEvidence ev = classify(scene_of(x));
    CHECK(count_class(ev, 5.99, 6.03, FrameClass::plosive) >= 1U);
}

TEST_CASE("the_floor_itself_rarely_reads_as_evidence", "[classify_gap_frames]") {
    // A flat white floor never reads over the 3 dB event margin for min_audible (four frames).
    // A narrow-band floor with a slow drift (0.5 dB/s; the curve lags a rising floor by about half
    // its window times the slope) can have a stretch of up to a few frames over it: noise of ~1 kHz
    // bandwidth varies by a dB or so between 25 ms levels. A false positive only protects more
    // (a missed mute), never removes sound; a larger event_margin trades these against missed quiet
    // consonants (design note open question 1).
    const GapEvidence flat = classify(scene_of(shaped_noise(10.0, Shape::white, -60.0, 5)));
    CHECK(max_excess(flat, 1.0, 9.0) < 3.0);
    CHECK(longest_run_above(flat, 1.0, 9.0, 3.0) < 2U);
    std::vector<float> drift = shaped_noise(12.0, Shape::low_passed, -70.0, 6);
    for (std::size_t i = 0; i < drift.size(); ++i) {  // -70 -> -64 dB over 12 s
        const double t = static_cast<double>(i) / k_rate;
        drift[i] *= static_cast<float>(std::pow(10.0, (6.0 * t / 12.0) / 20.0));
    }
    const GapEvidence slow = classify(scene_of(drift));
    CHECK(longest_run_above(slow, 2.0, 10.0, 3.0) < 8U);  // under 40 ms
    CHECK(max_excess(slow, 2.0, 10.0) < 6.0);
}

TEST_CASE("the_class_does_not_depend_on_how_loud_the_sibilant_is", "[classify_gap_frames]") {
    for (const double level_db : {-60.0, -45.0, -30.0}) {  // +5, +20, +35 dB over the floor
        std::vector<float> x = floor_only(10.0);
        add_into(x, shaped_noise(0.2, Shape::high_passed, level_db, 11), 4.0);
        const GapEvidence ev = classify(scene_of(x));
        INFO("level " << level_db);
        CHECK(count_class(ev, 4.05, 4.15, FrameClass::sibilant) >= 16U);  // of 20
    }
}

TEST_CASE("the_absolute_silence_level_is_ignored", "[classify_gap_frames]") {
    std::vector<float> x = floor_only(10.0);
    add_into(x, shaped_noise(0.2, Shape::high_passed, -45.0, 11), 4.0);
    GapEvidenceParams strict;
    strict.features.silence_level = Decibels{0.0};  // would call every frame silence
    const GapEvidence ev = classify(scene_of(x), strict);
    CHECK(count_class(ev, 4.04, 4.16, FrameClass::sibilant) >= 20U);
}

TEST_CASE("the_result_is_deterministic_and_matches_the_grid", "[classify_gap_frames]") {
    std::vector<float> x = floor_only(6.0);
    add_into(x, shaped_noise(0.2, Shape::high_passed, -45.0, 11), 3.0);
    const Scene s = scene_of(x);
    const GapEvidence a = classify(s);
    const GapEvidence b = classify(s);
    CHECK(a.frame_class == b.frame_class);
    CHECK(a.excess_db == b.excess_db);
    CHECK(a.excess_db.size() == static_cast<std::size_t>(s.activity.grid.n_frames));
}

TEST_CASE("only_the_four_unvoiced_classes_can_occur", "[classify_gap_frames]") {
    std::vector<float> x = floor_only(6.0);
    add_into(x, shaped_noise(0.2, Shape::high_passed, -45.0, 11), 3.0);
    add_into(x, shaped_noise(0.01, Shape::white, -20.0, 13), 4.5);
    for (const FrameClass c : classify(scene_of(x)).frame_class) {
        CHECK((c == FrameClass::silence || c == FrameClass::plosive || c == FrameClass::sibilant ||
               c == FrameClass::breath));
    }
}

TEST_CASE("tiny_and_empty_grids_are_fine", "[classify_gap_frames]") {
    Scene s = scene_of(floor_only(2.0));
    for (const std::int64_t n : {0, 1, 2}) {
        Scene small = s;
        small.activity.grid.n_frames = n;
        small.activity.level.resize(static_cast<std::size_t>(n));
        small.activity.activity.resize(static_cast<std::size_t>(n));
        small.floor.grid.n_frames = n;
        small.floor.curve_dbfs.resize(static_cast<std::size_t>(n));
        const auto ev = classify_gap_frames(small.mono, small.activity, small.floor, {});
        REQUIRE(ev.has_value());
        CHECK(ev->frame_class.size() == static_cast<std::size_t>(n));
    }
}

TEST_CASE("bad_inputs_are_named_errors", "[classify_gap_frames]") {
    const Scene s = scene_of(floor_only(2.0));
    CHECK(classify_gap_frames(MonoAudio{.samples = {}, .sample_rate = Hz{k_rate}}, s.activity,
                              s.floor, {})
              .error()
              .code == ErrorCode::invalid_argument);
    MonoAudio slow = s.mono;
    slow.sample_rate = Hz{6000.0};
    CHECK(classify_gap_frames(slow, s.activity, s.floor, {}).error().code ==
          ErrorCode::invalid_argument);
    NoiseFloor other = s.floor;
    other.grid.hop = Seconds{0.01};
    CHECK(classify_gap_frames(s.mono, s.activity, other, {}).error().code ==
          ErrorCode::invalid_argument);
    ActivityTrack short_level = s.activity;
    short_level.level.pop_back();
    CHECK(classify_gap_frames(s.mono, short_level, s.floor, {}).error().code ==
          ErrorCode::invalid_argument);
    GapEvidenceParams bad;
    bad.features.n_bands = 0;
    CHECK(classify_gap_frames(s.mono, s.activity, s.floor, bad).error().code ==
          ErrorCode::invalid_argument);
}
