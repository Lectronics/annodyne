#include "core/analysis/envelope/envelope.h"
#include "core/synthesis/psola/psola_engine.h"
#include "support/engine_fixture.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

using namespace anno;
using namespace anno::test;
using namespace anno::test::engine;

TEST_CASE("the_psola_engine_reports_its_identity_and_a_key_that_follows_its_settings",
          "[psola_engine]") {
    const PsolaEngine a;
    PsolaEngineParams p;
    p.max_shift = Cents{500.0};
    const PsolaEngine b{p};
    p = {};
    p.class_edge_fade = Seconds{0.003};
    const PsolaEngine c{p};
    p = {};
    p.shifted_classes = FrameClassSet{FrameClass::voiced};
    const PsolaEngine d{p};
    CHECK(a.name() == "psola");
    CHECK(a.version() == 1);
    CHECK(a.settings_key() == PsolaEngine{}.settings_key());
    CHECK(a.settings_key() != b.settings_key());
    CHECK(a.settings_key() != c.settings_key());
    CHECK(a.settings_key() != d.settings_key());
    CHECK(PsolaEngine{{.shifted_classes = {FrameClass::breathy, FrameClass::voiced}}}
              .settings_key() == a.settings_key());
}

TEST_CASE("bad_psola_params_are_invalid_arguments", "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const auto code = [&](PsolaEngineParams p) {
        return fx.render(PsolaEngine{p}, fx.effective(100.0)).error().code;
    };
    PsolaEngineParams p;
    p.class_edge_fade = Seconds{-0.001};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.class_edge_fade = Seconds{std::nan("")};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.max_shift = Cents{0.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.max_shift = Cents{std::nan("")};
    CHECK(code(p) == ErrorCode::invalid_argument);
}

TEST_CASE("psola_returns_the_original_for_the_empty_class_set_a_cancelled_edit_and_zero_shift",
          "[psola_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const auto same = [&](const OutputAudio& o) {
        return std::memcmp(o.audio.channel(0).data(), fx.mono.data(),
                           fx.mono.size() * sizeof(float)) == 0;
    };
    CHECK(same(fx.render(PsolaEngine{{.shifted_classes = {}}}, fx.effective(300.0)).value()));
    CHECK(same(fx.render(PsolaEngine{}, fx.effective(1e-12)).value()));
    CHECK(same(fx.render(PsolaEngine{}, fx.effective(0.0)).value()));
}

TEST_CASE("a_negative_zero_sample_survives_an_unshifted_psola_zone", "[psola_engine]") {
    std::vector<float> x = voice([](double) { return 330.0; }, harmonic_amps(8, 0.5));
    x[100] = -0.0F;  // far from the note
    const Fixture fx{x};
    const auto out = fx.render(PsolaEngine{}, fx.effective(300.0)).value();
    CHECK(std::signbit(out.audio.channel(0)[100]));
}

TEST_CASE("a_shift_above_the_limit_is_not_implemented_and_the_limit_itself_is_rendered",
          "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    CHECK(fx.render(PsolaEngine{}, fx.effective(701.0)).error().code == ErrorCode::not_implemented);
    CHECK(fx.render(PsolaEngine{}, fx.effective(-701.0)).error().code ==
          ErrorCode::not_implemented);
    CHECK(fx.render(PsolaEngine{}, fx.effective(700.0)).has_value());
    CHECK(fx.render(PsolaEngine{{.max_shift = Cents{100.0}}}, fx.effective(200.0)).error().code ==
          ErrorCode::not_implemented);
}

TEST_CASE("a_psola_shifted_note_comes_out_at_the_requested_pitch_within_three_cents",
          "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    for (const double shift : {200.0, -300.0, 37.0}) {
        const auto out = fx.render(PsolaEngine{}, fx.effective(shift)).value();
        const auto h = remeasure(fx, out, shift);
        // AUDIO_VERIFICATION §1: the note's re-detected pitch (median) within 3 cents.
        std::vector<double> errors;
        const double want = to_cents(Hz{330.0}, k_a4_standard_hz).value() + shift;
        for (std::size_t i = 8; i + 8 < static_cast<std::size_t>(h.freq_hz.rows); ++i) {
            errors.push_back(f1_cents(h, i) - want);
        }
        std::ranges::sort(errors);
        INFO("shift " << shift << ", worst frame " << errors.front() << " / " << errors.back());
        CHECK(std::abs(errors[errors.size() / 2]) < 3.0);
    }
}

TEST_CASE("psola_keeps_the_level_across_a_shift", "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const std::size_t a = static_cast<std::size_t>(0.5 * k_rate);
    const std::size_t b = static_cast<std::size_t>(1.1 * k_rate);
    const auto level = [&](const OutputAudio& o) {
        const std::vector<float> v(o.audio.channel(0).begin() + static_cast<std::ptrdiff_t>(a),
                                   o.audio.channel(0).begin() + static_cast<std::ptrdiff_t>(b));
        return rms(v);
    };
    const double before = level(fx.render(PsolaEngine{}, fx.effective(0.0)).value());
    for (const double shift : {300.0, -300.0}) {
        const double after = level(fx.render(PsolaEngine{}, fx.effective(shift)).value());
        INFO("shift " << shift);
        CHECK(std::abs(20.0 * std::log10(after / before)) < 1.5);
    }
}

TEST_CASE("psola_vibrato_extent_survives_a_shift", "[psola_engine]") {
    const auto f0 = [](double t) {
        return 262.0 * std::pow(2.0, 40.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t) / 1200.0);
    };
    const Fixture fx{voice(f0, harmonic_amps(8, 0.5))};
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
    const auto before = remeasure(fx, fx.render(PsolaEngine{}, fx.effective(0.0)).value(), 0.0);
    const auto after = remeasure(fx, fx.render(PsolaEngine{}, fx.effective(400.0)).value(), 400.0);
    CHECK(std::abs(extent(after) / extent(before) - 1.0) < 0.10);
}

TEST_CASE("psola_keeps_the_first_formant_where_it_was", "[psola_engine]") {
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
    const double shift = 700.0;
    const auto out = fx.render(PsolaEngine{}, fx.effective(shift)).value();
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
    CHECK(std::abs(best_f - 700.0) / 700.0 < 0.05);
}

TEST_CASE("psola_frames_of_an_unshifted_class_keep_the_original_samples", "[psola_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const std::int64_t n = fx.note.grid.n_frames;
    const std::int64_t lo = n / 2 - 20;
    const std::int64_t hi = n / 2 + 20;
    for (std::int64_t i = lo; i < hi; ++i) {
        fx.note.noise.frame_class[static_cast<std::size_t>(i)] = FrameClass::sibilant;
    }
    const auto out = fx.render(PsolaEngine{}, fx.effective(300.0)).value();
    const double hop = fx.note.grid.hop.value();
    const double t0 = (fx.note.grid.start - SourceTime{}).value();
    const auto first = static_cast<std::size_t>(
        std::llround((t0 + static_cast<double>(lo) * hop + 0.006) * k_rate));
    const auto last = static_cast<std::size_t>(
        std::llround((t0 + static_cast<double>(hi) * hop - 0.006) * k_rate));
    for (std::size_t i = first; i < last; ++i) {
        REQUIRE(out.audio.channel(0)[i] == fx.mono[i]);
    }
    const auto vowel = static_cast<std::size_t>(std::llround((t0 + 5.0 * hop) * k_rate)) + 400;
    CHECK(out.audio.channel(0)[vowel] != fx.mono[vowel]);
}

TEST_CASE("the_psola_margin_continues_the_edit_without_a_jump_at_the_first_frame",
          "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const double t_first = (fx.note.grid.start - SourceTime{}).value();
    const auto out = fx.render(PsolaEngine{}, fx.effective(300.0)).value();
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
    // The margin carries the edit on (it differs from the original); just inside the note the
    // level matches the original's (it fades in over 20 ms, so only the ratio is meaningful).
    CHECK(before > 1e-5);
    CHECK(before < 3.0 * after);
    CHECK(step < 0.5);
    const auto first = static_cast<std::size_t>(std::llround(t_first * k_rate));
    const auto band_rms = [&](const std::vector<float>& x) { return rms(x, first + 400, 800); };
    const std::vector<float> shifted_out(out.audio.channel(0).begin(), out.audio.channel(0).end());
    const double ratio = band_rms(shifted_out) / band_rms(fx.mono);
    INFO("margin level ratio " << ratio);
    CHECK(ratio > 0.5);
    CHECK(ratio < 2.0);
}

TEST_CASE("psola_edits_it_cannot_realise_are_not_implemented", "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const PsolaEngine engine;
    const auto code = [&](const EffectiveNote& e) { return fx.render(engine, e).error().code; };
    const auto base = fx.effective(100.0);
    auto e = base;
    e.gain = Decibels{3.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.mod_scale = Ratio{0.5};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.formant_offset = Cents{100.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.noise_gain.sibilant = Decibels{-3.0};
    CHECK(code(e) == ErrorCode::not_implemented);
    e = base;
    e.length_scale = Ratio{2.0};
    CHECK(code(e) == ErrorCode::not_implemented);
}

TEST_CASE("psola_sources_notes_and_maps_outside_the_supported_set_are_not_implemented",
          "[psola_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const PsolaEngine engine;
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
    bare.pitch_drift.clear();
    bare.pitch_mod.clear();
    bare.grid.n_frames = 0;
    CHECK(engine.render_note(fx.src, bare, eff, fx.map, zone).error().code ==
          ErrorCode::not_implemented);
    Note ragged = fx.note;
    ragged.pitch_mod.pop_back();
    CHECK(engine.render_note(fx.src, ragged, eff, fx.map, zone).error().code ==
          ErrorCode::invalid_argument);
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

TEST_CASE("psola_rendering_is_deterministic_and_stays_finite_and_bounded", "[psola_engine]") {
    const Fixture fx{voice([](double) { return 220.0; }, harmonic_amps(12, 0.5))};
    const PsolaEngine engine;
    for (const double shift : {700.0, -700.0}) {
        const auto a = fx.render(engine, fx.effective(shift)).value();
        const auto b = fx.render(engine, fx.effective(shift)).value();
        CHECK(std::equal(a.audio.channel(0).begin(), a.audio.channel(0).end(),
                         b.audio.channel(0).begin()));
        for (const float v : a.audio.channel(0)) {
            REQUIRE(std::isfinite(v));
            REQUIRE(std::abs(v) < 5.0F);
        }
    }
}

TEST_CASE("a_psola_zone_inside_the_recording_edge_still_renders", "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const auto zone = OutputRange::from_start(OutputTime{} + Seconds{0.15}, Seconds{0.3});
    const auto out = PsolaEngine{}.render_note(fx.src, fx.note, fx.effective(200.0), fx.map, zone);
    REQUIRE(out.has_value());
    CHECK(out->audio.frame_count().value() ==
          static_cast<std::int64_t>(std::llround(0.3 * k_rate)));
}

TEST_CASE("psola_zones_touching_the_recording_edges_render_with_zero_extension", "[psola_engine]") {
    // A voice that runs to both ends of the recording: the grains at the edges read past it.
    std::vector<float> x =
        make_harmonic([](double) { return 330.0; }, k_total_s, k_rate, harmonic_amps(8, 0.5));
    const Fixture fx{x};
    const PsolaEngine engine;
    const auto whole = OutputRange::from_start(OutputTime{}, Seconds{k_total_s});
    const auto out = engine.render_note(fx.src, fx.note, fx.effective(300.0), fx.map, whole);
    REQUIRE(out.has_value());
    CHECK(out->audio.frame_count().value() == static_cast<std::int64_t>(x.size()));
    for (const float v : out->audio.channel(0)) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) < 5.0F);
    }
    const auto head = OutputRange::from_start(OutputTime{}, Seconds{0.2});
    CHECK(engine.render_note(fx.src, fx.note, fx.effective(300.0), fx.map, head).has_value());
    const auto tail =
        OutputRange::from_start(OutputTime{} + Seconds{k_total_s - 0.2}, Seconds{0.2});
    CHECK(engine.render_note(fx.src, fx.note, fx.effective(300.0), fx.map, tail).has_value());
}

TEST_CASE("the_psola_margin_after_the_last_frame_continues_the_edit_for_a_bounded_time",
          "[psola_engine]") {
    const Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const double t_end = (fx.note.grid.start - SourceTime{}).value() +
                         static_cast<double>(fx.note.grid.n_frames) * fx.note.grid.hop.value();
    const auto out = fx.render(PsolaEngine{}, fx.effective(300.0)).value();
    const auto delta_at = [&](std::size_t i) {
        return static_cast<double>(out.audio.channel(0)[i]) - static_cast<double>(fx.mono[i]);
    };
    const auto end = static_cast<std::size_t>(std::llround(t_end * k_rate));
    double near = 0.0;
    for (std::size_t i = end - 600; i < end; ++i) {
        near = std::max(near, std::abs(delta_at(i)));
    }
    CHECK(near > 1e-4);  // the edit runs through the note's end (the voice fades out there)
    // Well beyond the margin (0.1 s plus the fades) the samples are the original.
    for (auto i = end + static_cast<std::size_t>(0.15 * k_rate); i < fx.mono.size(); ++i) {
        REQUIRE(out.audio.channel(0)[i] == fx.mono[i]);
    }
}

TEST_CASE("psola_moves_the_noise_inside_a_shifted_frame_and_keeps_the_timing", "[psola_engine]") {
    auto x = voice([](double) { return 330.0; }, harmonic_amps(8, 0.5));
    add_white_noise(x, 25.0, 3);
    for (std::size_t i = 0; i < x.size(); ++i) {  // keep the leading and trailing silence silent
        const double t = static_cast<double>(i) / k_rate;
        if (t < 0.15 || t > 1.45) {
            x[i] = 0.0F;
        }
    }
    const Fixture fx{x};
    const auto out = fx.render(PsolaEngine{}, fx.effective(300.0)).value();
    // The noise differs from the original (PSOLA re-spaces everything), the energy stays.
    const std::size_t a = static_cast<std::size_t>(0.6 * k_rate);
    const std::vector<float> o(out.audio.channel(0).begin() + static_cast<std::ptrdiff_t>(a),
                               out.audio.channel(0).begin() + static_cast<std::ptrdiff_t>(a) +
                                   8000);
    const std::vector<float> s(x.begin() + static_cast<std::ptrdiff_t>(a),
                               x.begin() + static_cast<std::ptrdiff_t>(a) + 8000);
    CHECK(std::abs(20.0 * std::log10(rms(o) / rms(s))) < 1.5);
    CHECK(std::memcmp(o.data(), s.data(), o.size() * sizeof(float)) != 0);
    // Timing: the note's onset (first sample above 5 % of the peak) moves by less than a period.
    const auto onset = [](std::span<const float> v) {
        float peak = 0.0F;
        for (const float e : v) {
            peak = std::max(peak, std::abs(e));
        }
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (std::abs(v[i]) > 0.05F * peak) {
                return static_cast<double>(i);
            }
        }
        return 0.0;
    };
    CHECK(std::abs(onset(out.audio.channel(0)) - onset(x)) < k_rate / 330.0);
}

TEST_CASE("psola_rejects_a_non_finite_center_a_ragged_class_track_and_a_reversed_range",
          "[psola_engine]") {
    Fixture fx{voice([](double) { return 330.0; }, harmonic_amps(8, 0.5))};
    const PsolaEngine engine;
    const OutputRange zone = OutputRange::from_start(OutputTime{}, Seconds{k_total_s});
    auto nan_center = fx.effective(100.0);
    nan_center.center = Cents{std::nan("")};
    CHECK(engine.render_note(fx.src, fx.note, nan_center, fx.map, zone).error().code ==
          ErrorCode::invalid_argument);
    Note ragged = fx.note;
    ragged.noise.frame_class.pop_back();
    CHECK(engine.render_note(fx.src, ragged, fx.effective(100.0), fx.map, zone).error().code ==
          ErrorCode::invalid_argument);
    Note drifting = fx.note;
    drifting.pitch_drift.pop_back();
    CHECK(engine.render_note(fx.src, drifting, fx.effective(100.0), fx.map, zone).error().code ==
          ErrorCode::invalid_argument);
    Note no_classes = fx.note;
    no_classes.noise.frame_class.clear();  // absent: every frame counts as modal
    CHECK(engine.render_note(fx.src, no_classes, fx.effective(100.0), fx.map, zone).has_value());
}

TEST_CASE("a_large_down_shift_leaves_no_dc_offset", "[psola_engine]") {
    // Before the one-period local mean was taken out, -700 cents on a rich voice left -58.5 dBFS
    // of DC in the audition scenario (limit -60). Random harmonic phases: a zero-phase sum is a
    // pulse train.
    std::mt19937 rng{7};
    std::uniform_real_distribution<double> phase{0.0, 2.0 * std::numbers::pi};
    std::vector<double> phases(30);
    for (double& p : phases) {
        p = phase(rng);
    }
    std::vector<float> x(static_cast<std::size_t>(k_total_s * k_rate), 0.0F);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / k_rate;
        double v = 0.0;
        for (std::size_t k = 1; k <= 30; ++k) {
            v += std::cos(2.0 * std::numbers::pi * 196.0 * static_cast<double>(k) * t +
                          phases[k - 1]) /
                 static_cast<double>(k);
        }
        x[i] =
            static_cast<float>(0.05 * v * std::clamp(std::min(t - 0.2, 1.4 - t) / 0.02, 0.0, 1.0));
    }
    const Fixture fx{x};
    const auto out = fx.render(PsolaEngine{}, fx.effective(-700.0)).value();
    double mean = 0.0;
    for (const float v : out.audio.channel(0)) {
        mean += static_cast<double>(v);
    }
    mean /= static_cast<double>(out.audio.channel(0).size());
    const double dc_dbfs = 20.0 * std::log10(std::max(std::abs(mean), 1e-12));
    const auto dc_of = [](std::span<const float> v) {
        double m = 0.0;
        for (const float e : v) {
            m += static_cast<double>(e);
        }
        return 20.0 * std::log10(std::max(std::abs(m / static_cast<double>(v.size())), 1e-12));
    };
    INFO("DC " << dc_dbfs << " dBFS; source " << dc_of(x) << "; -300: "
               << dc_of(fx.render(PsolaEngine{}, fx.effective(-300.0)).value().audio.channel(0))
               << "; interior(0.4-1.1s) " << dc_of(out.audio.channel(0).subspan(17640, 30870))
               << "; edge(0.2-0.4s) " << dc_of(out.audio.channel(0).subspan(8820, 8820))
               << "; +300: "
               << dc_of(fx.render(PsolaEngine{}, fx.effective(300.0)).value().audio.channel(0)));
    CHECK(dc_dbfs < tolerance::k_dc_offset_max_dbfs.value());
    // The fundamental keeps its level: the one-period mean does not touch harmonics.
    const auto level = [&](const OutputAudio& o) {
        const std::vector<float> v(o.audio.channel(0).begin() + 22050,
                                   o.audio.channel(0).begin() + 44100);
        return rms(v);
    };
    const double before = level(fx.render(PsolaEngine{}, fx.effective(0.0)).value());
    CHECK(std::abs(20.0 * std::log10(level(out) / before)) < 1.5);
}
