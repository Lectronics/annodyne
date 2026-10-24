#include "core/synthesis/rubberband_pitch/rubberband_pitch_engine.h"
#include "support/engine_fixture.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace anno;
using namespace anno::test;
using namespace anno::test::engine;

namespace {

std::vector<float> samples_of(const OutputAudio& a, int channel = 0) {
    return {a.audio.channel(channel).begin(), a.audio.channel(channel).end()};
}

/// Fundamental from the autocorrelation peak between 80 and 700 Hz, parabolic refinement.
double autocorrelation_hz(const std::vector<float>& x, double rate_hz) {
    const auto lo = static_cast<std::size_t>(rate_hz / 700.0);
    const auto hi = static_cast<std::size_t>(rate_hz / 80.0);
    const auto corr = [&](std::size_t lag) {
        double dot = 0.0;
        for (std::size_t i = 0; i + lag < x.size(); ++i) {
            dot += static_cast<double>(x[i]) * x[i + lag];
        }
        return dot;
    };
    std::size_t best = lo;
    double best_value = -1e30;
    for (std::size_t lag = lo; lag <= hi; ++lag) {
        const double v = corr(lag);
        if (v > best_value) {
            best_value = v;
            best = lag;
        }
    }
    const double left = corr(best - 1);
    const double right = corr(best + 1);
    const double curve = left - 2.0 * best_value + right;
    const double offset = curve < 0.0 ? 0.5 * (left - right) / curve : 0.0;
    return rate_hz / (static_cast<double>(best) + offset);
}

Fixture steady_voice() {
    return Fixture{voice([](double) { return 220.0; }, harmonic_amps(10, 0.5))};
}

}  // namespace

TEST_CASE("the_rubber_band_pitch_engine_reports_its_identity_and_a_key_that_follows_its_settings",
          "[rubberband_pitch]") {
    const RubberBandPitchEngine a;
    RubberBandPitchParams p;
    p.formants = PitchFormants::shifted;
    const RubberBandPitchEngine b{p};
    p = {};
    p.max_shift = Cents{700.0};
    const RubberBandPitchEngine c{p};
    CHECK(a.name() == "rubberband");
    CHECK(a.settings_key() == RubberBandPitchEngine{}.settings_key());
    CHECK(a.settings_key() != b.settings_key());
    CHECK(a.settings_key() != c.settings_key());
}

TEST_CASE("a_neutral_offset_returns_the_original_samples_exactly", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    const RubberBandPitchEngine engine;
    const auto out = fx.render(engine, fx.effective(0.0)).value();
    const auto original = fx.render(engine, fx.effective(0.0)).value();
    const auto a = samples_of(out);
    REQUIRE(a.size() == fx.mono.size());
    CHECK(std::memcmp(a.data(), fx.mono.data(), a.size() * sizeof(float)) == 0);
    (void)original;
}

TEST_CASE("a_shift_moves_the_pitch_by_the_requested_interval_and_keeps_the_length",
          "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    const RubberBandPitchEngine engine;
    for (const double cents : {-400.0, 200.0, 700.0}) {
        const auto out = fx.render(engine, fx.effective(cents)).value();
        const auto samples = samples_of(out);
        REQUIRE(samples.size() == fx.mono.size());
        const auto quarter = static_cast<std::ptrdiff_t>(samples.size() / 4);
        const std::vector<float> middle(samples.begin() + quarter, samples.begin() + 3 * quarter);
        const double expected = 220.0 * std::pow(2.0, cents / 1200.0);
        INFO("cents " << cents);
        CHECK(std::abs(autocorrelation_hz(middle, k_rate) / expected - 1.0) < 0.01);
    }
}

TEST_CASE("the_rubber_band_pitch_engine_is_deterministic", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    const RubberBandPitchEngine engine;
    const auto a = samples_of(fx.render(engine, fx.effective(300.0)).value());
    const auto b = samples_of(fx.render(engine, fx.effective(300.0)).value());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

TEST_CASE("the_rubber_band_pitch_engine_refuses_what_it_does_not_realise", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    const RubberBandPitchEngine engine;
    EffectiveNote gain = fx.effective(100.0);
    gain.gain = Decibels{3.0};
    CHECK(fx.render(engine, gain).error().code == ErrorCode::not_implemented);
    CHECK(fx.render(engine, fx.effective(3000.0)).error().code == ErrorCode::not_implemented);
    Note inactive = fx.note;
    inactive.is_active = false;
    CHECK(engine
              .render_note(fx.src, inactive, fx.effective(100.0), fx.map,
                           OutputRange::from_start(OutputTime{}, Seconds{k_total_s}))
              .error()
              .code == ErrorCode::not_implemented);
    RubberBandPitchParams bad;
    bad.context = Seconds{-1.0};
    CHECK(RubberBandPitchEngine{bad}
              .render_note(fx.src, fx.note, fx.effective(100.0), fx.map,
                           OutputRange::from_start(OutputTime{}, Seconds{1.0}))
              .error()
              .code == ErrorCode::invalid_argument);
}

TEST_CASE("the_rubber_band_pitch_engine_shifts_every_channel", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    auto stereo =
        AudioBuffer::create(2, Samples{static_cast<std::int64_t>(fx.mono.size())}, Hz{k_rate})
            .value();
    for (std::size_t i = 0; i < fx.mono.size(); ++i) {
        stereo.channel(0)[i] = fx.mono[i];
        stereo.channel(1)[i] = -0.5F * fx.mono[i];
    }
    const AudioSource src = AudioSource::from_buffer(std::move(stereo));
    const RubberBandPitchEngine engine;
    const auto out = engine
                         .render_note(src, fx.note, fx.effective(200.0), fx.map,
                                      OutputRange::from_start(OutputTime{}, Seconds{k_total_s}))
                         .value();
    REQUIRE(out.audio.channel_count() == 2);
    // channel 1 is -0.5 x channel 0: the shifted channels stay coherent (correlation near -1)
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t i = 5000; i < 40000; ++i) {
        const auto a = static_cast<double>(out.audio.channel(0)[i]);
        const auto b = static_cast<double>(out.audio.channel(1)[i]);
        ab += a * b;
        aa += a * a;
        bb += b * b;
    }
    INFO("correlation " << ab / std::sqrt(aa * bb));
    CHECK(ab / std::sqrt(aa * bb) < -0.98);
}

namespace {

Fixture vibrato_voice(double rate_hz, double depth_cents) {
    return Fixture{voice(
        [=](double t) {
            return 220.0 *
                   std::pow(2.0,
                            depth_cents * std::sin(2.0 * std::numbers::pi * rate_hz * t) / 1200.0);
        },
        harmonic_amps(10, 0.5))};
}

/// Depth (cents) of the strongest 3-9 Hz component of the analysed fast pitch of the longest note.
double vibrato_depth(const OutputAudio& out) {
    const std::vector<float> mono = samples_of(out);
    const auto src = AudioSource::from_buffer(to_buffer(mono, k_rate));
    const auto analysis = analyse(src, engine_analysis_params()).value();
    if (analysis->notes.empty()) {
        return 0.0;
    }
    const Note& n = *std::max_element(
        analysis->notes.begin(), analysis->notes.end(),
        [](const Note& a, const Note& b) { return a.grid.n_frames < b.grid.n_frames; });
    const double hop = n.grid.hop.value();
    double best = 0.0;
    for (int k = 60; k <= 180; ++k) {
        const double f = k * 0.05;
        double re = 0.0;
        double im = 0.0;
        for (std::size_t i = 0; i < n.pitch_mod.size(); ++i) {
            const double v = n.pitch_mod[i].value();
            re += v * std::cos(2.0 * std::numbers::pi * f * static_cast<double>(i) * hop);
            im += v * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) * hop);
        }
        best = std::max(best, 2.0 * std::hypot(re, im) / static_cast<double>(n.pitch_mod.size()));
    }
    return best;
}

OutputAudio render_scaled(const Fixture& fx, const RubberBandPitchEngine& engine,
                          double mod_scale) {
    EffectiveNote e = fx.effective(0.0);
    e.mod_scale = Ratio{mod_scale};
    e.is_edited = true;
    return fx.render(engine, e).value();
}

}  // namespace

TEST_CASE("the_modulation_scale_changes_the_vibrato_depth_in_proportion", "[rubberband_pitch]") {
    const Fixture fx = vibrato_voice(5.5, 40.0);
    const RubberBandPitchEngine engine;
    const double source =
        vibrato_depth(OutputAudio{.audio = to_buffer(fx.mono, k_rate), .start = OutputTime{}});
    REQUIRE(std::abs(source - 40.0) < 6.0);
    // measured depth = source x (1 + (scale - 1) x response): flat at 0, doubled at 2
    CHECK(vibrato_depth(render_scaled(fx, engine, 0.0)) < 0.25 * source);
    CHECK(std::abs(vibrato_depth(render_scaled(fx, engine, 0.5)) / source - 0.5) < 0.2);
    CHECK(std::abs(vibrato_depth(render_scaled(fx, engine, 1.5)) / source - 1.5) < 0.25);
    CHECK(std::abs(vibrato_depth(render_scaled(fx, engine, 2.0)) / source - 2.0) < 0.35);
}

TEST_CASE("a_drift_scale_and_bad_curve_parameters", "[rubberband_pitch]") {
    const Fixture fx = vibrato_voice(5.5, 40.0);
    const RubberBandPitchEngine engine;
    EffectiveNote e = fx.effective(0.0);
    e.drift_scale = Ratio{0.0};
    e.is_edited = true;
    const auto out = fx.render(engine, e).value();
    CHECK(out.audio.frame_count().value() == static_cast<std::int64_t>(fx.mono.size()));
    RubberBandPitchParams bad;
    bad.curve_gain = Ratio{0.0};
    EffectiveNote m = fx.effective(0.0);
    m.mod_scale = Ratio{0.0};
    CHECK(RubberBandPitchEngine{bad}
              .render_note(fx.src, fx.note, m, fx.map,
                           OutputRange::from_start(OutputTime{}, Seconds{1.0}))
              .error()
              .code == ErrorCode::invalid_argument);
}

namespace {

/// Autocorrelation pitch of samples [from_s, to_s) of a rendered buffer.
double pitch_between(const OutputAudio& out, double from_s, double to_s) {
    const auto samples = samples_of(out);
    return autocorrelation_hz(
        std::vector<float>(samples.begin() + static_cast<std::ptrdiff_t>(from_s * k_rate),
                           samples.begin() + static_cast<std::ptrdiff_t>(to_s * k_rate)),
        k_rate);
}

}  // namespace

TEST_CASE("only_the_notes_core_is_corrected_and_the_ends_stay_as_sung", "[rubberband_pitch]") {
    // A steady 220 Hz tone 0.2 .. 1.4 s; the note under edit is 0.5 .. 1.1 s with its settled core
    // 0.7 .. 0.9 s, +300 cents. Outside the core the pitch stays 220 Hz (the approach and release
    // are not corrected); inside, after the 40 ms ramp, it is 220 x 2^(300/1200).
    const Fixture fx = steady_voice();
    Note note = fx.note;
    note.source_start = SourceTime::unchecked_from_origin(Seconds{0.5});
    note.source_end = SourceTime::unchecked_from_origin(Seconds{1.1});
    note.pitch_target.core = SourceRange::make(SourceTime::unchecked_from_origin(Seconds{0.7}),
                                               SourceTime::unchecked_from_origin(Seconds{0.9}))
                                 .value();
    note.noise.frame_class.clear();  // every frame counts as shifted
    note.noise.grid = FrameGrid{};
    const RubberBandPitchEngine engine;
    const auto out = engine
                         .render_note(fx.src, note, fx.effective(300.0), fx.map,
                                      OutputRange::from_start(OutputTime{}, Seconds{k_total_s}))
                         .value();
    const double shifted = 220.0 * std::pow(2.0, 300.0 / 1200.0);
    CHECK(std::abs(pitch_between(out, 0.40, 0.66) / 220.0 - 1.0) < 0.015);
    CHECK(std::abs(pitch_between(out, 0.76, 0.84) / shifted - 1.0) < 0.02);
    CHECK(std::abs(pitch_between(out, 0.95, 1.30) / 220.0 - 1.0) < 0.015);
}

TEST_CASE("frames_of_a_class_that_is_not_shifted_stay_where_they_were", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    const RubberBandPitchEngine voiced_only;  // default: voiced and breathy frames are shifted
    RubberBandPitchParams none_shifted;
    none_shifted.shifted_classes = FrameClassSet{};
    const RubberBandPitchEngine nothing{none_shifted};
    const OutputRange all = OutputRange::from_start(OutputTime{}, Seconds{k_total_s});
    EffectiveNote e = fx.effective(300.0);
    const auto shifted_out = nothing.render_note(fx.src, fx.note, e, fx.map, all).value();
    const auto default_out = voiced_only.render_note(fx.src, fx.note, e, fx.map, all).value();
    CHECK(std::abs(pitch_between(shifted_out, 0.5, 1.1) / 220.0 - 1.0) < 0.015);
    CHECK(std::abs(pitch_between(default_out, 0.5, 1.1) / (220.0 * std::pow(2.0, 0.25)) - 1.0) <
          0.02);
}

TEST_CASE("core_only_off_corrects_the_whole_zone_by_a_constant_offset", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    RubberBandPitchParams whole;
    whole.core_only = false;
    const RubberBandPitchEngine engine{whole};
    const auto out = fx.render(engine, fx.effective(300.0)).value();
    CHECK(std::abs(pitch_between(out, 0.25, 0.4) / (220.0 * std::pow(2.0, 0.25)) - 1.0) < 0.015);
}

TEST_CASE("a_negative_glide_time_is_an_invalid_argument", "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    RubberBandPitchParams bad;
    bad.glide_time = Seconds{-0.01};
    CHECK(RubberBandPitchEngine{bad}
              .render_note(fx.src, fx.note, fx.effective(100.0), fx.map,
                           OutputRange::from_start(OutputTime{}, Seconds{1.0}))
              .error()
              .code == ErrorCode::invalid_argument);
}

TEST_CASE("a_formant_offset_without_preserved_formants_is_refused_not_ignored",
          "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    RubberBandPitchParams shifted;
    shifted.formants = PitchFormants::shifted;
    EffectiveNote e = fx.effective(0.0);
    e.formant_offset = Cents{200.0};
    e.is_edited = true;
    const OutputRange all = OutputRange::from_start(OutputTime{}, Seconds{k_total_s});
    CHECK(
        RubberBandPitchEngine{shifted}.render_note(fx.src, fx.note, e, fx.map, all).error().code ==
        ErrorCode::invalid_argument);
    CHECK(RubberBandPitchEngine{}.render_note(fx.src, fx.note, e, fx.map, all).has_value());
}
