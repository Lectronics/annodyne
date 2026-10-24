#include "core/dsp/band_split.h"
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

TEST_CASE("an_unedited_touching_neighbour_makes_the_offset_glide_in_and_out",
          "[rubberband_pitch]") {
    // A steady 220 Hz tone 0.2 .. 1.4 s; the note under edit is 0.6 .. 1.0 s, +300 cents, with
    // unedited notes touching it on both sides. Outside the note the pitch must stay 220 Hz, inside
    // (after the 40 ms glide) 220 x 2^(300/1200).
    const Fixture fx = steady_voice();
    Note note = fx.note;
    note.source_start = SourceTime::unchecked_from_origin(Seconds{0.6});
    note.source_end = SourceTime::unchecked_from_origin(Seconds{1.0});
    const RubberBandPitchEngine engine;
    TouchingNotes touching;
    touching.previous = NeighbourOffset{.offset = Cents{0.0}, .rendered = false};
    touching.next = NeighbourOffset{.offset = Cents{0.0}, .rendered = false};
    const auto out = engine
                         .render_note_in_context(
                             fx.src, note, fx.effective(300.0), fx.map,
                             OutputRange::from_start(OutputTime{}, Seconds{k_total_s}), touching)
                         .value();
    const double shifted = 220.0 * std::pow(2.0, 300.0 / 1200.0);
    CHECK(std::abs(pitch_between(out, 0.40, 0.55) / 220.0 - 1.0) < 0.015);
    CHECK(std::abs(pitch_between(out, 0.72, 0.90) / shifted - 1.0) < 0.015);
    CHECK(std::abs(pitch_between(out, 1.08, 1.25) / 220.0 - 1.0) < 0.015);
}

TEST_CASE("no_touching_neighbour_or_equal_offsets_render_exactly_like_render_note",
          "[rubberband_pitch]") {
    const Fixture fx = steady_voice();
    const RubberBandPitchEngine engine;
    const OutputRange all = OutputRange::from_start(OutputTime{}, Seconds{k_total_s});
    const auto plain = samples_of(fx.render(engine, fx.effective(200.0)).value());
    const auto none = samples_of(
        engine.render_note_in_context(fx.src, fx.note, fx.effective(200.0), fx.map, all, {})
            .value());
    CHECK(std::memcmp(plain.data(), none.data(), plain.size() * sizeof(float)) == 0);
    TouchingNotes same;
    same.previous = NeighbourOffset{.offset = Cents{200.0}, .rendered = true};
    same.next = NeighbourOffset{.offset = Cents{200.0}, .rendered = true};
    const auto equal = samples_of(
        engine.render_note_in_context(fx.src, fx.note, fx.effective(200.0), fx.map, all, same)
            .value());
    CHECK(std::memcmp(plain.data(), equal.data(), plain.size() * sizeof(float)) == 0);
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

namespace {

constexpr double k_mvf_cutoff_hz = 3000.0;
constexpr double k_high_tone_hz = 5000.0;
constexpr double k_high_tone_amp = 0.05;

/// A harmonic voice (f0 220, 10 partials, up to 2.2 kHz) plus a steady 5 kHz tone above the
/// stored maximum voiced frequency (3 kHz), which stands for the noise of a breathy voice.
Fixture breathy_voice() {
    auto x = voice([](double) { return 220.0; }, harmonic_amps(10, 0.5));
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] +=
            static_cast<float>(k_high_tone_amp * std::sin(2.0 * std::numbers::pi * k_high_tone_hz *
                                                          static_cast<double>(i) / k_rate));
    }
    Fixture fx{std::move(x)};
    fx.note.noise.max_voiced_hz.assign(static_cast<std::size_t>(fx.note.grid.n_frames),
                                       Hz{k_mvf_cutoff_hz});
    return fx;
}

RubberBandPitchEngine below_mvf_engine() {
    RubberBandPitchParams p;
    p.scope = ShiftScope::below_mvf;
    return RubberBandPitchEngine{p};
}

/// Amplitude of the tone at `hz` over the middle half of `x` (single-bin correlation).
double tone_amplitude(const std::vector<float>& x, double hz) {
    const std::size_t from = x.size() / 4;
    const std::size_t to = 3 * x.size() / 4;
    double c = 0.0;
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double ph = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / k_rate;
        c += static_cast<double>(x[i]) * std::cos(ph);
        s += static_cast<double>(x[i]) * std::sin(ph);
    }
    return 2.0 * std::hypot(c, s) / static_cast<double>(to - from);
}

double rms_of(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

std::vector<float> high_part(const std::vector<float>& x, double cutoff_hz) {
    const std::vector<double> cut(x.size(), cutoff_hz);
    return split_band(x, Hz{k_rate}, cut, Hz{330.0}).value().high;
}

}  // namespace

TEST_CASE("the_scope_is_part_of_the_engine_key_and_its_version", "[rubberband_pitch]") {
    CHECK(below_mvf_engine().settings_key() != RubberBandPitchEngine{}.settings_key());
}

TEST_CASE("below_the_voiced_limit_only_the_periodic_part_is_shifted", "[rubberband_pitch]") {
    const Fixture fx = breathy_voice();
    const auto full = samples_of(fx.render(RubberBandPitchEngine{}, fx.effective(300.0)).value());
    const auto scoped = samples_of(fx.render(below_mvf_engine(), fx.effective(300.0)).value());
    REQUIRE(scoped.size() == fx.mono.size());
    // The tone above the limit stays where it was; a full-band shift moves it away.
    CHECK(tone_amplitude(scoped, k_high_tone_hz) > 0.9 * k_high_tone_amp);
    CHECK(tone_amplitude(full, k_high_tone_hz) < 0.3 * k_high_tone_amp);
    // The periodic part lands on the requested pitch.
    const auto q = static_cast<std::ptrdiff_t>(scoped.size() / 4);
    const std::vector<float> middle(scoped.begin() + q, scoped.begin() + 3 * q);
    CHECK(std::abs(autocorrelation_hz(middle, k_rate) / (220.0 * std::pow(2.0, 300.0 / 1200.0)) -
                   1.0) < 0.01);
}

TEST_CASE("shifted_harmonics_do_not_cross_the_voiced_limit", "[rubberband_pitch]") {
    // +/-100, +/-300 and +/-1200 cents: the energy above the limit (plus the 5 kHz tone) is
    // that of the original, whichever way the harmonics moved.
    const Fixture fx = breathy_voice();
    const auto original_high = high_part(fx.mono, 3500.0);
    const auto n = original_high.size();
    const double reference = rms_of(original_high, n / 4, 3 * n / 4);
    for (const double cents : {-1200.0, -300.0, -100.0, 100.0, 300.0, 1200.0}) {
        const auto out = samples_of(fx.render(below_mvf_engine(), fx.effective(cents)).value());
        const auto high = high_part(out, 3500.0);
        const double ratio = rms_of(high, n / 4, 3 * n / 4) / reference;
        INFO("cents " << cents << " ratio " << ratio);
        CHECK(ratio < 1.10);
        CHECK(ratio > 0.90);
    }
}

TEST_CASE("a_neutral_offset_is_bit_exact_in_the_scoped_mode_too", "[rubberband_pitch]") {
    const Fixture fx = breathy_voice();
    const auto out = samples_of(fx.render(below_mvf_engine(), fx.effective(0.0)).value());
    CHECK(std::memcmp(out.data(), fx.mono.data(), out.size() * sizeof(float)) == 0);
}

/// Index where the 5 ms-smoothed rectified signal first reaches half of its maximum.
std::size_t half_level_index(const std::vector<float>& x) {
    const auto taps = static_cast<std::size_t>(0.005 * k_rate);
    std::vector<double> env(x.size(), 0.0);
    double acc = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        acc += std::abs(static_cast<double>(x[i]));
        if (i >= taps) {
            acc -= std::abs(static_cast<double>(x[i - taps]));
        }
        env[i] = acc / static_cast<double>(taps);
    }
    const double peak = *std::ranges::max_element(env);
    return static_cast<std::size_t>(
        std::ranges::find_if(env, [&](double v) { return v >= 0.5 * peak; }) - env.begin());
}

TEST_CASE("the_scoped_low_band_is_aligned_in_time_with_the_full_band_shift", "[rubberband_pitch]") {
    // The onset of the shifted periodic part must land where the full-band shift puts it (same
    // shifter, same delay handling), or the sum with the untouched rest would comb. Waveform
    // equality is not expected: a phase vocoder's output phases depend on its whole input.
    const Fixture fx = breathy_voice();
    const auto scoped = samples_of(fx.render(below_mvf_engine(), fx.effective(300.0)).value());
    const auto full = samples_of(fx.render(RubberBandPitchEngine{}, fx.effective(300.0)).value());
    const auto low = [](const std::vector<float>& x) {
        return split_band(x, Hz{k_rate}, std::vector<double>(x.size(), 2500.0), Hz{330.0})
            .value()
            .low;
    };
    const auto a = static_cast<std::ptrdiff_t>(half_level_index(low(scoped)));
    const auto b = static_cast<std::ptrdiff_t>(half_level_index(low(full)));
    INFO("onset " << a << " vs " << b << " samples");
    CHECK(std::abs(a - b) <= static_cast<std::ptrdiff_t>(0.002 * k_rate));
}

TEST_CASE("the_scoped_mode_refuses_a_formant_offset_and_falls_back_without_a_stored_limit",
          "[rubberband_pitch]") {
    Fixture fx = breathy_voice();
    EffectiveNote with_formant = fx.effective(200.0);
    with_formant.formant_offset = Cents{100.0};
    CHECK(fx.render(below_mvf_engine(), with_formant).error().code == ErrorCode::invalid_argument);
    fx.note.noise.max_voiced_hz.clear();
    const auto scoped = samples_of(fx.render(below_mvf_engine(), fx.effective(200.0)).value());
    const auto full = samples_of(fx.render(RubberBandPitchEngine{}, fx.effective(200.0)).value());
    CHECK(std::memcmp(scoped.data(), full.data(), scoped.size() * sizeof(float)) == 0);
}

TEST_CASE("the_scoped_mode_works_with_a_modulation_scale", "[rubberband_pitch]") {
    const Fixture fx = breathy_voice();
    EffectiveNote eff = fx.effective(200.0);
    eff.mod_scale = Ratio{1.5};
    const auto out = samples_of(fx.render(below_mvf_engine(), eff).value());
    REQUIRE(out.size() == fx.mono.size());
    for (const float v : out) {
        REQUIRE(std::isfinite(v));
    }
    CHECK(tone_amplitude(out, k_high_tone_hz) > 0.9 * k_high_tone_amp);
}
