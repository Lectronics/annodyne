#include "core/synthesis/rubberband_pitch/rubberband_pitch_engine.h"
#include "support/engine_fixture.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
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
            return 220.0 * std::pow(2.0, depth_cents * std::sin(2.0 * M_PI * rate_hz * t) / 1200.0);
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
            re += v * std::cos(2.0 * M_PI * f * static_cast<double>(i) * hop);
            im += v * std::sin(2.0 * M_PI * f * static_cast<double>(i) * hop);
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
