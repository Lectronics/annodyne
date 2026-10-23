#include "core/render/level.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 1000.0;  // 1 sample = 1 ms keeps the arithmetic readable

// A note over output samples [100, 300) (source 0.1 .. 0.3 s), zone [80, 320).
LevelZone zone() {
    return {.first = 80,
            .count = 240,
            .note_first = 100,
            .note_last = 300,
            .rate = Hz{k_rate},
            .shift_samples = 0};
}

Note flat_note() {
    return make_note("a", 0.1, 0.3);
}

EffectiveNote eff_of() {
    EffectiveNote e;
    e.note_id = NoteId{"a"};
    return e;
}

double at(const std::vector<float>& g, std::int64_t sample) {
    return static_cast<double>(g[static_cast<std::size_t>(sample - 80)]);
}

bool near(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

}  // namespace

TEST_CASE("a_neutral_state_has_no_level_edit_and_without_level_clears_the_level_fields",
          "[level]") {
    EffectiveNote e = eff_of();
    CHECK_FALSE(has_level_edit(e));
    e.gain = Decibels{3.0};
    CHECK(has_level_edit(e));
    e.fade_in = Seconds{0.01};
    e.within_note_flatten = Ratio{0.5};
    e.center = Cents{100.0};
    const EffectiveNote stripped = without_level(e);
    CHECK_FALSE(has_level_edit(stripped));
    CHECK(stripped.center.value() == 100.0);  // the pitch part is kept
}

TEST_CASE("a_gain_is_constant_over_the_zone_and_a_mute_is_zero", "[level]") {
    EffectiveNote e = eff_of();
    e.gain = Decibels{6.0};
    const auto g = level_gain(flat_note(), e, zone()).value();
    REQUIRE(g.size() == 240);
    for (const std::int64_t n : {80, 100, 200, 299, 319}) {
        CHECK(near(at(g, n), std::pow(10.0, 6.0 / 20.0)));
    }
    e.gain = Decibels{-120.0};
    const auto muted = level_gain(flat_note(), e, zone()).value();
    for (const float v : muted) {
        CHECK(v == 0.0F);
    }
}

TEST_CASE("fades_run_from_the_note_edges_with_their_shape", "[level]") {
    EffectiveNote e = eff_of();
    e.fade_in = Seconds{0.04};
    e.fade_out = Seconds{0.02};
    e.fade_shape = FadeShape::linear;
    auto g = level_gain(flat_note(), e, zone()).value();
    CHECK(near(at(g, 100), 0.0));
    CHECK(near(at(g, 120), 0.5));
    CHECK(near(at(g, 140), 1.0));
    CHECK(near(at(g, 200), 1.0));
    CHECK(near(at(g, 290), 0.5));
    CHECK(near(at(g, 299), 0.05, 1e-3));
    CHECK(near(at(g, 85), 0.0));  // before the note: held at the first sample's gain
    e.fade_shape = FadeShape::equal_power;
    g = level_gain(flat_note(), e, zone()).value();
    CHECK(near(at(g, 120), std::sqrt(0.5)));
    e.fade_shape = FadeShape::log;
    g = level_gain(flat_note(), e, zone()).value();
    CHECK(near(at(g, 120), std::pow(10.0, -30.0 / 20.0), 1e-4));
}

TEST_CASE("fades_longer_than_the_note_shrink_in_proportion", "[level]") {
    EffectiveNote e = eff_of();
    e.fade_in = Seconds{0.3};
    e.fade_out = Seconds{0.1};  // 0.4 s of fades on a 0.2 s note: 0.15 + 0.05
    e.fade_shape = FadeShape::linear;
    const auto g = level_gain(flat_note(), e, zone()).value();
    CHECK(near(at(g, 175), 0.5, 1e-2));  // halfway up the 0.15 s fade in
    CHECK(near(at(g, 250), 1.0, 1e-2));  // the fade in ends where the 0.05 s fade out starts
    CHECK(near(at(g, 275), 0.5, 1e-2));
}

TEST_CASE("flattening_moves_each_frame_toward_the_notes_mean_level", "[level]") {
    Note note = flat_note();
    note.grid = FrameGrid{.hop = Seconds{0.05}, .start = src(0.1), .n_frames = 5};
    note.amp = {Decibels{-20.0}, Decibels{-10.0}, Decibels{-20.0}, Decibels{-10.0},
                Decibels{-15.0}};  // mean -15
    EffectiveNote e = eff_of();
    e.within_note_flatten = Ratio{1.0};
    auto g = level_gain(note, e, zone()).value();
    // frame 0 sits at 100 ms: -20 dB is 5 dB under the mean -> +5 dB
    CHECK(near(20.0 * std::log10(at(g, 100)), 5.0, 1e-3));
    // frame 1 at 150 ms: -10 dB -> -5 dB
    CHECK(near(20.0 * std::log10(at(g, 150)), -5.0, 1e-3));
    e.within_note_flatten = Ratio{0.5};
    g = level_gain(note, e, zone()).value();
    CHECK(near(20.0 * std::log10(at(g, 100)), 2.5, 1e-3));
    e.within_note_flatten = Ratio{0.0};
    g = level_gain(note, e, zone()).value();
    CHECK(near(at(g, 100), 1.0));
}

TEST_CASE("flattening_is_clamped_and_leaves_silent_frames_alone", "[level]") {
    Note note = flat_note();
    note.grid = FrameGrid{.hop = Seconds{0.05}, .start = src(0.1), .n_frames = 4};
    note.amp = {Decibels{-10.0}, Decibels{-10.0}, Decibels{-10.0}, Decibels{-80.0}};
    EffectiveNote e = eff_of();
    e.within_note_flatten = Ratio{1.0};
    const auto g = level_gain(note, e, zone()).value();
    CHECK(near(at(g, 250), 1.0, 1e-3));  // the -80 dB frame is below the floor: untouched
}

TEST_CASE("a_source_to_output_shift_moves_the_flatten_curve_with_the_note", "[level]") {
    Note note = flat_note();
    note.grid = FrameGrid{.hop = Seconds{0.05}, .start = src(0.1), .n_frames = 3};
    note.amp = {Decibels{-20.0}, Decibels{-10.0}, Decibels{-15.0}};
    EffectiveNote e = eff_of();
    e.within_note_flatten = Ratio{1.0};
    LevelZone shifted = zone();
    shifted.shift_samples = 20;  // the note plays 20 ms later: frame 0 sits at output 120
    const auto g = level_gain(note, e, shifted).value();
    // mean is -15: frame 0 (-20) -> +5 dB at output 120, frame 1 (-10) -> -5 dB at output 170
    CHECK(near(20.0 * std::log10(at(g, 120)), 5.0, 1e-3));
    CHECK(near(20.0 * std::log10(at(g, 170)), -5.0, 1e-3));
}

TEST_CASE("bad_zones_are_invalid_arguments_and_apply_gain_multiplies_every_channel", "[level]") {
    EffectiveNote e = eff_of();
    LevelZone bad = zone();
    bad.count = -1;
    CHECK(level_gain(flat_note(), e, bad).error().code == ErrorCode::invalid_argument);
    bad = zone();
    bad.rate = Hz{0.0};
    CHECK(level_gain(flat_note(), e, bad).error().code == ErrorCode::invalid_argument);
    e.gain = Decibels{std::nan("")};
    CHECK(level_gain(flat_note(), e, zone()).error().code == ErrorCode::invalid_argument);

    auto audio = AudioBuffer::create(2, Samples{4}, Hz{k_rate}).value();
    for (std::size_t i = 0; i < 4; ++i) {
        audio.channel(0)[i] = 1.0F;
        audio.channel(1)[i] = -2.0F;
    }
    const std::vector<float> gain{0.5F, 1.0F, 2.0F, 0.0F};
    REQUIRE(apply_gain(audio, gain).has_value());
    CHECK(audio.channel(0)[0] == 0.5F);
    CHECK(audio.channel(1)[2] == -4.0F);
    CHECK(audio.channel(1)[3] == 0.0F);
    CHECK(apply_gain(audio, std::vector<float>(3, 1.0F)).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("sibilant_breath_and_plosive_frames_take_their_class_gain_with_short_ramps", "[level]") {
    Note note = flat_note();
    // five frames 50 ms apart from 100 ms: voiced, sibilant, sibilant, breath, voiced
    note.noise.grid = FrameGrid{.hop = Seconds{0.05}, .start = src(0.1), .n_frames = 5};
    note.noise.frame_class = {FrameClass::voiced, FrameClass::sibilant, FrameClass::sibilant,
                              FrameClass::breath, FrameClass::voiced};
    EffectiveNote e = eff_of();
    e.noise_gain.sibilant = Decibels{-12.0};
    e.noise_gain.breath = Decibels{6.0};
    const auto g = level_gain(note, e, zone()).value();
    CHECK(near(at(g, 100), 1.0));                             // a voiced frame: untouched
    CHECK(near(20.0 * std::log10(at(g, 150)), -12.0, 1e-3));  // a sibilant frame
    CHECK(near(20.0 * std::log10(at(g, 200)), -12.0, 1e-3));
    CHECK(near(20.0 * std::log10(at(g, 250)), 6.0, 1e-3));   // a breath frame
    CHECK(near(20.0 * std::log10(at(g, 125)), -6.0, 1e-3));  // halfway into the ramp
    CHECK(near(at(g, 300), 1.0));
    // the voiced noise gain is the engine's, not the gain curve's
    EffectiveNote voiced = eff_of();
    voiced.noise_gain.voiced = Decibels{-6.0};
    CHECK_FALSE(has_level_edit(voiced));
    const auto flat = level_gain(note, voiced, zone()).value();
    for (const float v : flat) {
        CHECK(v == 1.0F);
    }
}

TEST_CASE("class_gains_count_as_a_level_edit_and_without_level_keeps_the_voiced_noise_gain",
          "[level]") {
    EffectiveNote e = eff_of();
    e.noise_gain.plosive = Decibels{-3.0};
    CHECK(has_level_edit(e));
    e.noise_gain.voiced = Decibels{2.0};
    const EffectiveNote stripped = without_level(e);
    CHECK(stripped.noise_gain.plosive == Decibels{});
    CHECK(stripped.noise_gain.voiced.value() == 2.0);
}
