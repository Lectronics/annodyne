#include "core/analysis/perceived_pitch/perceived_pitch.h"
#include "support/synth_melody.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace anno;
using namespace anno::test;

namespace {

NoteSpan span_of(double start_s, double end_s) {
    NoteSpan s;
    s.id = NoteId{"n"};
    s.source_start = SourceTime{} + Seconds{start_s};
    s.attack_end = s.source_start;
    s.release_start = SourceTime{} + Seconds{end_s};
    s.source_end = s.release_start;
    return s;
}

/// A finished Note from a decomposition (amp = flat 0 dB unless given).
Note note_from(const NoteSpan& span, const PitchTrack& pitch, const PitchDecomposition& d,
               std::vector<Decibels> amp = {}) {
    Note note;
    note.id = span.id;
    note.track_id = TrackId{"t1"};
    note.source_start = span.source_start;
    note.attack_end = span.attack_end;
    note.release_start = span.release_start;
    note.source_end = span.source_end;
    note.grid = note_grid(span, pitch.grid);
    note.pitch_center = d.center;
    note.pitch_drift = d.drift;
    note.pitch_mod = d.mod;
    note.amp =
        amp.empty() ? std::vector<Decibels>(d.drift.size(), Decibels{-20.0}) : std::move(amp);
    return note;
}

double vibrato(double t, double extent, double hz) {
    return extent * std::sin(2.0 * std::numbers::pi * hz * t);
}

}  // namespace

TEST_CASE("stability_weights_fall_with_speed_and_low_level", "[perceived_pitch]") {
    std::vector<Cents> f0(21, Cents{6900.0});
    for (std::size_t i = 10; i < f0.size(); ++i) {
        f0[i] = Cents{6900.0 + 50.0 * static_cast<double>(i - 9)};  // a steep rise at frame 10
    }
    const std::vector<Decibels> level(21, Decibels{-10.0});
    const auto w = stability_weights(f0, Seconds{0.005}, level, {});
    REQUIRE(w.size() == 21);
    CHECK(w[2] > 0.99);   // flat
    CHECK(w[12] < 0.01);  // 10000 cents per second
    std::vector<Decibels> quiet = level;
    quiet[2] = Decibels{-50.0};  // 40 dB below the loudest: beyond the 30 dB range
    CHECK(stability_weights(f0, Seconds{0.005}, quiet, {})[2] == 0.0);
    CHECK(stability_weights(f0, Seconds{0.005}, {}, {})[2] > 0.99);  // no amp: no amp term
}

TEST_CASE("decomposition_separates_centre_drift_and_modulation_exactly", "[perceived_pitch]") {
    // 6900 centre + one 1.2 s drift cycle (+-30 cents) + 5.5 Hz vibrato (+-50 cents), 1.2 s
    const auto bend = [](double t) { return vibrato(t, 30.0, 1.0 / 1.2) + vibrato(t, 50.0, 5.5); };
    const auto track =
        make_track({{.start_s = 0.2, .end_s = 1.4, .midi = 69.0, .bend_cents = bend}}, 1.8);
    const NoteSpan span = span_of(0.2, 1.4);
    const auto d = decompose_pitch_curve(span, track, {}, {}).value();
    const FrameGrid grid = note_grid(span, track.grid);
    REQUIRE(d.drift.size() == static_cast<std::size_t>(grid.n_frames));
    REQUIRE(d.mod.size() == d.drift.size());
    CHECK(std::abs(d.center.value() - 6900.0) <= 3.0);
    CHECK(d.voiced_fraction == 1.0);
    double mod_peak = 0.0;
    double drift_peak = 0.0;
    for (std::size_t i = 30; i + 30 < d.mod.size(); ++i) {  // away from the filter's edges
        mod_peak = std::max(mod_peak, std::abs(d.mod[i].value()));
        drift_peak = std::max(drift_peak, std::abs(d.drift[i].value()));
    }
    CHECK(std::abs(mod_peak - 50.0) <= 10.0);    // vibrato lands in `mod`
    CHECK(std::abs(drift_peak - 30.0) <= 10.0);  // slow drift lands in `drift`
    // exact reconstruction of the (voiced everywhere) f0
    const std::size_t i0 = 40;  // frame time 0.2 s
    for (std::size_t i = 0; i < d.drift.size(); ++i) {
        const double filled = track.f0[i0 + i].value();
        CHECK(std::abs(d.center.value() + d.drift[i].value() + d.mod[i].value() - filled) < 1e-9);
    }
}

TEST_CASE("unvoiced_gaps_are_filled_and_counted", "[perceived_pitch]") {
    PitchTrack track = make_track({{.start_s = 0.2, .end_s = 1.0, .midi = 60.0}}, 1.4);
    for (std::size_t i = 100; i < 108; ++i) {  // 8 frames unvoiced inside the note
        track.f0[i] = Cents{std::nan("")};
        track.voicing_prob[i] = 0.0;
    }
    const auto d = decompose_pitch_curve(span_of(0.2, 1.0), track, {}, {}).value();
    CHECK(d.voiced_fraction < 1.0);
    CHECK(d.voiced_fraction > 0.9);
    for (const Cents c : d.drift) {
        REQUIRE(std::isfinite(c.value()));
    }
    CHECK(std::abs(d.center.value() - 6000.0) <= 0.5);
}

TEST_CASE("decomposition_rejects_degenerate_notes_and_bad_arguments", "[perceived_pitch]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 0.209, .midi = 60.0}}, 0.6);
    CHECK(decompose_pitch_curve(span_of(0.2, 0.209), track, {}, {}).error().code ==
          ErrorCode::invalid_argument);  // 2 frames
    const auto ok_track = make_track({{.start_s = 0.2, .end_s = 0.8, .midi = 60.0}}, 1.0);
    const std::vector<Decibels> wrong_size(5, Decibels{-20.0});
    CHECK(decompose_pitch_curve(span_of(0.2, 0.8), ok_track, wrong_size, {}).error().code ==
          ErrorCode::invalid_argument);
    PitchDecompositionParams bad;
    bad.drift_cutoff = Hz{0.0};
    CHECK(decompose_pitch_curve(span_of(0.2, 0.8), ok_track, {}, bad).error().code ==
          ErrorCode::invalid_argument);
    const auto unvoiced = make_track({}, 1.0);
    CHECK(decompose_pitch_curve(span_of(0.2, 0.8), unvoiced, {}, {}).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("a_stable_note_has_its_centre_as_perceived_pitch", "[perceived_pitch]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 0.9, .midi = 64.0}}, 1.2);
    const NoteSpan span = span_of(0.2, 0.9);
    const auto d = decompose_pitch_curve(span, track, {}, {}).value();
    const Note note = note_from(span, track, d);
    CHECK(std::abs(perceived_pitch(note, {}).value().value() - 6400.0) <= 0.5);
}

TEST_CASE("a_vibrato_note_is_heard_at_the_mean_over_whole_cycles", "[perceived_pitch]") {
    // 1.0 s of 5.5 Hz = 5.5 cycles: averaging every frame would bias towards the partial cycle.
    const auto bend = [](double t) { return vibrato(t, 50.0, 5.5); };
    const auto track =
        make_track({{.start_s = 0.2, .end_s = 1.2, .midi = 69.0, .bend_cents = bend}}, 1.6);
    const NoteSpan span = span_of(0.2, 1.2);
    const auto d = decompose_pitch_curve(span, track, {}, {}).value();
    const Note note = note_from(span, track, d);
    const double whole = perceived_pitch(note, PerceivedPitchParams{})
                             .value()
                             .value();  // whole_cycles is the default
    CHECK(std::abs(whole - 6900.0) <= 2.0);
}

TEST_CASE("a_scoop_into_the_pitch_does_not_pull_the_perceived_pitch", "[perceived_pitch]") {
    // 60 ms scoop from 300 cents below, then steady
    const auto bend = [](double t) { return t < 0.06 ? -300.0 * (1.0 - t / 0.06) : 0.0; };
    const auto track =
        make_track({{.start_s = 0.2, .end_s = 1.0, .midi = 67.0, .bend_cents = bend}}, 1.4);
    const NoteSpan span = span_of(0.2, 1.0);
    const auto d = decompose_pitch_curve(span, track, {}, {}).value();
    const Note note = note_from(span, track, d);
    CHECK(std::abs(perceived_pitch(note, {}).value().value() - 6700.0) <= 5.0);
}

TEST_CASE("short_notes_use_scaled_trims_and_degenerate_ones_fail", "[perceived_pitch]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 0.28, .midi = 62.0}}, 0.6);  // 80 ms
    const NoteSpan span = span_of(0.2, 0.28);
    const auto d = decompose_pitch_curve(span, track, {}, {}).value();
    const Note note = note_from(span, track, d);
    CHECK(std::abs(perceived_pitch(note, {}).value().value() - 6200.0) <= 1.0);
    Note empty = note;
    empty.grid.n_frames = 2;
    empty.pitch_drift.resize(2);
    empty.pitch_mod.resize(2);
    empty.amp.resize(2);
    CHECK(perceived_pitch(empty, {}).error().code == ErrorCode::invalid_argument);
    Note inconsistent = note;
    inconsistent.pitch_mod.pop_back();
    CHECK(perceived_pitch(inconsistent, {}).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("weighted_median_picks_the_value_that_balances_the_weight", "[perceived_pitch]") {
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0};
    CHECK(weighted_median(values, std::vector<double>{1.0, 1.0, 1.0, 1.0}) == 2.0);  // lower median
    CHECK(weighted_median(values, std::vector<double>{1.0, 1.0, 1.0, 10.0}) == 4.0);
    CHECK(weighted_median(values, std::vector<double>{0.0, 0.0, 5.0, 1.0}) == 3.0);
    CHECK(weighted_median(std::vector<double>{3.0, 1.0, 2.0}, std::vector<double>{1.0, 1.0, 1.0}) ==
          2.0);  // unsorted input
    CHECK(std::isnan(weighted_median(values, std::vector<double>{0.0, 0.0, 0.0, -1.0})));
}

TEST_CASE("vibrato_period_finds_a_modulation_and_ignores_a_flat_curve", "[perceived_pitch]") {
    constexpr double k_hop_s = 0.005;
    std::vector<double> mod(200);
    for (std::size_t i = 0; i < mod.size(); ++i) {
        mod[i] = 50.0 * std::sin(2.0 * std::numbers::pi * 5.5 * static_cast<double>(i) * k_hop_s);
    }
    const auto period = vibrato_period(mod, Seconds{k_hop_s});
    REQUIRE(period.has_value());
    CHECK(std::abs(period->value() - 1.0 / 5.5) <= 0.01);
    CHECK(!vibrato_period(std::vector<double>(200, 0.0), Seconds{k_hop_s}).has_value());
    CHECK(
        !vibrato_period(std::vector<double>(5, 30.0), Seconds{k_hop_s}).has_value());  // too short
}
