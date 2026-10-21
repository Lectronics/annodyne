#include "core/analysis/envelope/envelope.h"
#include "core/analysis/orchestrator/analyse.h"
#include "core/analysis/partials/partials.h"
#include "core/synthesis/harmonic_noise/harmonic_noise_engine.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;
constexpr double k_total_s = 1.6;

/// 0.2 .. 1.4 s of a harmonic voice with amplitudes `amps` and pitch f0_hz(t); silence elsewhere.
template <class F0Hz>
std::vector<float> voice(const F0Hz& f0_hz, const std::vector<double>& amps) {
    auto x = make_harmonic(f0_hz, k_total_s, k_rate, amps);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / k_rate;
        const double fade = std::clamp(std::min(t - 0.2, 1.4 - t) / 0.02, 0.0, 1.0);
        x[i] *= static_cast<float>(fade);
    }
    return x;
}

struct Fixture {
    std::vector<float> mono;
    AudioSource src;
    std::shared_ptr<const AnalysisResult> analysis;
    Note note;
    TimeMap map;

    explicit Fixture(std::vector<float> samples)
        : mono(std::move(samples)), src(AudioSource::from_buffer(to_buffer(mono, k_rate))),
          analysis(analyse(src, AnalyseParams{}).value()),
          map(TimeMap::identity(Seconds{k_total_s})) {
        REQUIRE_FALSE(analysis->notes.empty());
        note = *std::max_element(
            analysis->notes.begin(), analysis->notes.end(),
            [](const Note& a, const Note& b) { return a.grid.n_frames < b.grid.n_frames; });
    }

    [[nodiscard]] EffectiveNote effective(double shift_cents) const {
        EffectiveNote e;
        e.note_id = note.id;
        e.center = note.pitch_center + Cents{shift_cents};
        e.is_edited = shift_cents != 0.0;
        return e;
    }

    /// The whole recording rendered with the note shifted (identity map: output axis = source
    /// axis).
    [[nodiscard]] Result<OutputAudio> render(const HarmonicNoiseEngine& engine,
                                             const EffectiveNote& eff) const {
        return engine.render_note(src, note, eff, map,
                                  OutputRange::from_start(OutputTime{}, Seconds{k_total_s}));
    }
};

/// f1..fK of the rendered note re-measured with the shifted pitch curves.
HarmonicTrack remeasure(const Fixture& fx, const OutputAudio& out, double shift_cents) {
    std::vector<float> mono(out.audio.channel(0).begin(), out.audio.channel(0).end());
    const NoteCurves curves{.grid = fx.note.grid,
                            .pitch_center = fx.note.pitch_center + Cents{shift_cents},
                            .pitch_drift = fx.note.pitch_drift,
                            .pitch_mod = fx.note.pitch_mod};
    return analyse_partials(mono, Hz{k_rate}, curves, {}).value();
}

double f1_cents(const HarmonicTrack& h, std::size_t frame) {
    const auto cols = static_cast<std::size_t>(h.freq_hz.cols);
    return to_cents(Hz{static_cast<double>(h.freq_hz.data[frame * cols])}, k_a4_standard_hz)
        .value();
}

}  // namespace

TEST_CASE("the_engine_reports_its_identity_and_a_key_that_follows_its_settings",
          "[harmonic_noise_engine]") {
    const HarmonicNoiseEngine a;
    HarmonicNoiseEngineParams p;
    p.class_edge_fade = Seconds{0.003};
    const HarmonicNoiseEngine b{p};
    p = {};
    p.shifted_classes = FrameClassSet{FrameClass::voiced};
    const HarmonicNoiseEngine c{p};
    p = {};
    p.taper_end_of_rate = 0.46;
    const HarmonicNoiseEngine d{p};
    CHECK(a.name() == "harmonic_noise");
    CHECK(a.version() == 1);
    CHECK(a.settings_key() == HarmonicNoiseEngine{}.settings_key());
    CHECK(a.settings_key() != b.settings_key());
    CHECK(a.settings_key() != c.settings_key());
    CHECK(a.settings_key() != d.settings_key());
    // Class sets are canonical: the order of the listed classes cannot change the key.
    CHECK(HarmonicNoiseEngine{{.shifted_classes = {FrameClass::breathy, FrameClass::voiced}}}
              .settings_key() == a.settings_key());
}

TEST_CASE("bad_engine_params_are_invalid_arguments", "[harmonic_noise_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const auto code = [&](HarmonicNoiseEngineParams p) {
        return fx.render(HarmonicNoiseEngine{p}, fx.effective(100.0)).error().code;
    };
    HarmonicNoiseEngineParams p;
    p.class_edge_fade = Seconds{-0.001};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.taper_start_of_rate = 0.46;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.taper_end_of_rate = 0.6;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.class_edge_fade = Seconds{std::nan("")};
    CHECK(code(p) == ErrorCode::invalid_argument);
}

TEST_CASE("the_empty_class_set_and_a_cancelled_edit_return_the_original",
          "[harmonic_noise_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const HarmonicNoiseEngine none{{.shifted_classes = {}}};
    const auto a = fx.render(none, fx.effective(300.0)).value();
    CHECK(std::memcmp(a.audio.channel(0).data(), fx.mono.data(), fx.mono.size() * sizeof(float)) ==
          0);
    const auto b = fx.render(HarmonicNoiseEngine{}, fx.effective(1e-12)).value();
    CHECK(std::memcmp(b.audio.channel(0).data(), fx.mono.data(), fx.mono.size() * sizeof(float)) ==
          0);
}

TEST_CASE("a_negative_zero_sample_survives_an_unshifted_zone", "[harmonic_noise_engine]") {
    std::vector<float> x = voice([](double) { return 330.0; }, harmonic_amps(8, 0.5));
    x[100] = -0.0F;  // far from the note
    const Fixture fx{x};
    const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(300.0)).value();
    CHECK(std::signbit(out.audio.channel(0)[100]));
}

TEST_CASE("a_note_without_measured_harmonics_passes_through", "[harmonic_noise_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    std::ranges::fill(fx.note.harmonics.amp_linear.data, 0.0F);
    const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(300.0)).value();
    CHECK(std::equal(out.audio.channel(0).begin(), out.audio.channel(0).end(), fx.mono.begin()));
}

TEST_CASE("a_note_ending_in_an_unshifted_class_leaves_its_margin_original",
          "[harmonic_noise_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const std::int64_t n = fx.note.grid.n_frames;
    for (std::int64_t i = n - 30; i < n; ++i) {
        fx.note.noise.frame_class[static_cast<std::size_t>(i)] = FrameClass::breath;
    }
    const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(300.0)).value();
    const double t_end = (fx.note.grid.start - SourceTime{}).value() +
                         static_cast<double>(n) * fx.note.grid.hop.value();
    // From before the end of the shifted run's fade into the margin, the samples are the original.
    for (auto i = static_cast<std::size_t>(std::llround((t_end - 0.14) * k_rate));
         i < fx.mono.size(); ++i) {
        REQUIRE(out.audio.channel(0)[i] == fx.mono[i]);
    }
}

TEST_CASE("a_zero_shift_returns_the_original_samples_bit_for_bit", "[harmonic_noise_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(0.0)).value();
    REQUIRE(out.audio.frame_count().value() == static_cast<std::int64_t>(fx.mono.size()));
    CHECK(std::equal(out.audio.channel(0).begin(), out.audio.channel(0).end(), fx.mono.begin()));
}

TEST_CASE("a_shifted_note_comes_out_at_the_requested_pitch_within_three_cents",
          "[harmonic_noise_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    for (const double shift : {200.0, -300.0, 37.0}) {
        const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(shift)).value();
        const auto h = remeasure(fx, out, shift);
        // AUDIO_VERIFICATION §1: re-detect the note's pitch (median over the note) within 3 cents
        // of the requested one. The worst single frame is bounded more loosely: it beats against
        // the residual of the original partial (about 1 % of its level).
        std::vector<double> errors;
        const double want = to_cents(Hz{330.0}, k_a4_standard_hz).value() + shift;
        for (std::size_t i = 8; i + 8 < static_cast<std::size_t>(h.freq_hz.rows); ++i) {
            errors.push_back(f1_cents(h, i) - want);
        }
        std::ranges::sort(errors);
        INFO("shift " << shift);
        CHECK(std::abs(errors[errors.size() / 2]) < 3.0);
        CHECK(std::max(std::abs(errors.front()), std::abs(errors.back())) < 6.0);
    }
}

TEST_CASE("vibrato_rate_and_extent_survive_a_shift", "[harmonic_noise_engine]") {
    const auto f0 = [](double t) {
        return 262.0 * std::pow(2.0, 40.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t) / 1200.0);
    };
    const Fixture fx{voice(f0, harmonic_amps(8, 0.5))};
    const auto before =
        remeasure(fx, fx.render(HarmonicNoiseEngine{}, fx.effective(0.0)).value(), 0.0);
    const auto after =
        remeasure(fx, fx.render(HarmonicNoiseEngine{}, fx.effective(400.0)).value(), 400.0);
    const auto extent = [](const HarmonicTrack& h) {
        double lo = 1e9;
        double hi = -1e9;
        for (std::size_t i = 8; i + 8 < static_cast<std::size_t>(h.freq_hz.rows); ++i) {
            const auto cols = static_cast<std::size_t>(h.freq_hz.cols);
            const double c = 1200.0 * std::log2(static_cast<double>(h.freq_hz.data[i * cols]));
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
        return 0.5 * (hi - lo);
    };
    CHECK(std::abs(extent(after) / extent(before) - 1.0) < 0.10);
}

TEST_CASE("formants_stay_put_when_the_pitch_moves", "[harmonic_noise_engine]") {
    // Source-filter voice: harmonics follow a two-formant envelope, first formant at 700 Hz.
    const auto env_db = [](double f) {
        const auto bump = [&](double c, double w, double h) {
            const double x = (f - c) / w;
            return h * std::exp(-0.5 * x * x);
        };
        return -6.0 * std::log2(1.0 + f / 500.0) + bump(700.0, 150.0, 18.0) +
               bump(1800.0, 250.0, 12.0);
    };
    std::vector<double> amps;
    for (int k = 1; k <= 40; ++k) {
        amps.push_back(0.05 * std::pow(10.0, env_db(165.0 * k) / 20.0));
    }
    const Fixture fx{voice([](double) { return 165.0; }, amps)};
    const double shift = 700.0;  // a bit over a fifth up
    const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(shift)).value();
    const auto h = remeasure(fx, out, shift);
    const NoteCurves curves{.grid = fx.note.grid,
                            .pitch_center = fx.note.pitch_center + Cents{shift},
                            .pitch_drift = fx.note.pitch_drift,
                            .pitch_mod = fx.note.pitch_mod};
    const auto env = estimate_envelope(curves, h, Hz{k_rate}, {}).value();
    const std::size_t frame = static_cast<std::size_t>(h.freq_hz.rows) / 2;
    double best_f = 0.0;
    double best_db = -1e9;
    for (std::size_t b = 0; b < env.freqs.size(); ++b) {
        const double f = env.freqs[b].value();
        const auto v = static_cast<double>(env.log_mag_db.data[frame * env.freqs.size() + b]);
        if (f > 300.0 && f < 1300.0 && v > best_db) {
            best_db = v;
            best_f = f;
        }
    }
    CHECK(std::abs(best_f - 700.0) / 700.0 < 0.05);  // formant preservation, AUDIO_VERIFICATION §1
}

TEST_CASE("frames_of_an_unshifted_class_keep_the_original_samples", "[harmonic_noise_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    // Mark the middle 200 ms as sibilant (a consonant inside the note).
    const std::int64_t n = fx.note.grid.n_frames;
    const std::int64_t lo = n / 2 - 20;
    const std::int64_t hi = n / 2 + 20;
    for (std::int64_t i = lo; i < hi; ++i) {
        fx.note.noise.frame_class[static_cast<std::size_t>(i)] = FrameClass::sibilant;
    }
    const auto out = fx.render(HarmonicNoiseEngine{}, fx.effective(300.0)).value();
    const double hop = fx.note.grid.hop.value();
    const double t0 = (fx.note.grid.start - SourceTime{}).value();
    const auto first = static_cast<std::size_t>(
        std::llround((t0 + static_cast<double>(lo) * hop + 0.006) * k_rate));
    const auto last = static_cast<std::size_t>(
        std::llround((t0 + static_cast<double>(hi) * hop - 0.006) * k_rate));
    for (std::size_t i = first; i < last; ++i) {
        REQUIRE(out.audio.channel(0)[i] == fx.mono[i]);
    }
    // And the vowel around it did change.
    const auto vowel = static_cast<std::size_t>(std::llround((t0 + 5.0 * hop) * k_rate)) + 400;
    CHECK(out.audio.channel(0)[vowel] != fx.mono[vowel]);
}

TEST_CASE("the_zone_margin_continues_the_edit_without_a_jump_at_the_first_frame",
          "[harmonic_noise_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const double t_first = (fx.note.grid.start - SourceTime{}).value();
    const HarmonicNoiseEngine engine;
    const auto out = fx.render(engine, fx.effective(300.0)).value();
    const auto delta_at = [&](double t) {
        const auto i = static_cast<std::size_t>(std::llround(t * k_rate));
        return static_cast<double>(out.audio.channel(0)[i]) - static_cast<double>(fx.mono[i]);
    };
    double before = 0.0;
    double after = 0.0;
    double step = 0.0;
    for (int k = 1; k <= 400; ++k) {
        const double dt = static_cast<double>(k) / k_rate;
        before = std::max(before, std::abs(delta_at(t_first - dt)));
        after = std::max(after, std::abs(delta_at(t_first + dt)));
        step = std::max(step,
                        std::abs(delta_at(t_first + dt) - delta_at(t_first + dt - 1.0 / k_rate)));
    }
    CHECK(before > 0.005);        // the margin carries the edit on (the note fades in over 20 ms)
    CHECK(before < 3.0 * after);  // and does not start louder than the note itself
    CHECK(step < 0.5);            // no discontinuity at the frame boundary
}

TEST_CASE("edits_the_engine_cannot_realise_are_not_implemented", "[harmonic_noise_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const HarmonicNoiseEngine engine;
    const auto code = [&](const EffectiveNote& e) { return fx.render(engine, e).error().code; };
    auto base = fx.effective(100.0);
    auto e = base;
    e.gain = Decibels{3.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.mod_scale = Ratio{0.5};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.drift_scale = Ratio{0.5};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.formant_offset = Cents{100.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.fade_in = Seconds{0.01};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.noise_gain.sibilant = Decibels{-3.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.length_scale = Ratio{2.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.within_note_flatten = Ratio{0.5};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.attack_time_scale = Ratio{2.0};
    CHECK(code(e) == ErrorCode::not_implemented);
}

TEST_CASE("sources_notes_and_maps_outside_the_supported_set_are_not_implemented",
          "[harmonic_noise_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const HarmonicNoiseEngine engine;
    const auto eff = fx.effective(100.0);
    const OutputRange zone = OutputRange::from_start(OutputTime{}, Seconds{k_total_s});
    const AudioSource stereo = AudioSource::from_buffer(to_buffer(fx.mono, k_rate, 2));
    CHECK(engine.render_note(stereo, fx.note, eff, fx.map, zone).error().code ==
          ErrorCode::not_implemented);
    Note inactive = fx.note;
    inactive.is_active = false;
    CHECK(engine.render_note(fx.src, inactive, eff, fx.map, zone).error().code ==
          ErrorCode::not_implemented);
    Note bare = fx.note;
    bare.harmonics = {};
    CHECK(engine.render_note(fx.src, bare, eff, fx.map, zone).error().code ==
          ErrorCode::not_implemented);
    const TimeSpan stretched{.kind = SpanKind::warp,
                             .source_start = SourceTime{},
                             .source_end = SourceTime{} + Seconds{k_total_s},
                             .output_start = OutputTime{},
                             .output_end = OutputTime{} + Seconds{2.0 * k_total_s},
                             .segment = std::nullopt};
    const auto slow = TimeMap::make({stretched}).value();
    CHECK(engine.render_note(fx.src, fx.note, eff, slow, zone).error().code ==
          ErrorCode::not_implemented);
}

TEST_CASE("rendering_is_deterministic_and_extreme_shifts_stay_finite", "[harmonic_noise_engine]") {
    const Fixture fx{voice([](double) { return 880.0; }, harmonic_amps(12, 0.5))};
    const HarmonicNoiseEngine engine;
    const auto a = fx.render(engine, fx.effective(2400.0)).value();
    const auto b = fx.render(engine, fx.effective(2400.0)).value();
    CHECK(std::equal(a.audio.channel(0).begin(), a.audio.channel(0).end(),
                     b.audio.channel(0).begin()));
    for (const float v : a.audio.channel(0)) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) < 5.0F);
    }
    const auto down = fx.render(engine, fx.effective(-2400.0)).value();
    for (const float v : down.audio.channel(0)) {
        REQUIRE(std::isfinite(v));
    }
}
