#include "core/analysis/onsets/onsets.h"
#include "core/analysis/perceived_pitch/perceived_pitch.h"
#include "core/analysis/pitch/pitch_estimator.h"
#include "core/analysis/segmentation/segmentation.h"
#include "support/synth_melody.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

using namespace anno;
using namespace anno::test;

namespace {

double sec(SourceTime t) {
    return (t - SourceTime{}).value();
}

std::vector<NoteSpan> segment(const PitchTrack& pitch, const OnsetList& onsets,
                              const SegmentParams& params = {}) {
    return segment_notes(pitch, onsets, params).value();
}

}  // namespace

TEST_CASE("separated_notes_are_found_with_times_and_pitches", "[segmentation]") {
    const std::vector<MelodyNote> melody{{.start_s = 0.30, .end_s = 0.80, .midi = 60.0},
                                         {.start_s = 1.00, .end_s = 1.50, .midi = 64.0},
                                         {.start_s = 1.80, .end_s = 2.40, .midi = 67.0}};
    const auto track = make_track(melody, 2.8);
    const auto notes = segment(track, make_onsets({0.30, 1.00, 1.80}));
    REQUIRE(notes.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(std::abs(sec(notes[i].source_start) - melody[i].start_s) <= 0.02);
        CHECK(std::abs(sec(notes[i].source_end) - melody[i].end_s) <= 0.03);
        CHECK(std::abs(notes[i].rough_pitch.value() - melody[i].midi * 100.0) <= 10.0);
        CHECK(notes[i].separation_in == SeparationIn::hard);
        CHECK(notes[i].confidence_prob > 0.9);
        CHECK(notes[i].source_start <= notes[i].attack_end);
        CHECK(notes[i].attack_end <= notes[i].release_start);
        CHECK(notes[i].release_start <= notes[i].source_end);
    }
    CHECK(notes[0].id.value == "n000300");
    CHECK(notes[1].id.value == "n001000");
    // sorted and non-overlapping
    for (std::size_t i = 1; i < notes.size(); ++i) {
        CHECK(notes[i - 1].source_end <= notes[i].source_start);
    }
}

TEST_CASE("a_repeated_pitch_splits_only_where_an_onset_says_so", "[segmentation]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 1.0, .midi = 62.0}}, 1.4);
    CHECK(segment(track, make_onsets({0.2})).size() == 1);  // no onset in the middle: one note
    const auto split = segment(track, make_onsets({0.2, 0.6}));
    REQUIRE(split.size() == 2);  // an onset splits it at the same pitch
    CHECK(std::abs(sec(split[1].source_start) - 0.6) <= 0.03);
    CHECK(std::abs(split[0].rough_pitch.value() - split[1].rough_pitch.value()) <= 10.0);
}

TEST_CASE("a_vibrato_note_stays_one_note", "[segmentation]") {
    const auto vibrato = [](double t) { return 60.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t); };
    const auto track =
        make_track({{.start_s = 0.2, .end_s = 1.4, .midi = 65.0, .bend_cents = vibrato}}, 1.8);
    const auto notes = segment(track, make_onsets({0.2}));
    REQUIRE(notes.size() == 1);
    CHECK(std::abs(notes[0].rough_pitch.value() - 6500.0) <= 15.0);
}

TEST_CASE("a_legato_step_gives_two_notes_the_second_soft", "[segmentation]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 0.7, .midi = 69.0},
                                   {.start_s = 0.7, .end_s = 1.2, .midi = 71.0}},
                                  1.6);
    OnsetList onsets = make_onsets({0.2});
    onsets.times.push_back(SourceTime{} + Seconds{0.7});
    onsets.strength.push_back(2.5);
    onsets.kind.push_back(OnsetKind::pitch_change);
    const auto notes = segment(track, onsets);
    REQUIRE(notes.size() == 2);
    CHECK(std::abs(sec(notes[1].source_start) - 0.7) <= 0.02);
    CHECK(notes[1].separation_in == SeparationIn::soft);
    CHECK(std::abs(notes[0].rough_pitch.value() - 6900.0) <= 10.0);
    CHECK(std::abs(notes[1].rough_pitch.value() - 7100.0) <= 10.0);
}

TEST_CASE("a_glide_is_absorbed_into_the_note_it_reaches", "[segmentation]") {
    const auto glide = [](double t) {
        return t < 0.3 ? 1200.0 * t / 0.3 : 1200.0;
    };  // up an octave in 0.3 s
    const auto track =
        make_track({{.start_s = 0.2, .end_s = 0.8, .midi = 57.0},
                    {.start_s = 0.8, .end_s = 1.7, .midi = 57.0, .bend_cents = glide}},
                   2.0);
    const auto notes = segment(track, make_onsets({0.2}));
    REQUIRE(notes.size() == 2);
    CHECK(std::abs(notes[0].rough_pitch.value() - 5700.0) <= 10.0);
    CHECK(std::abs(notes[1].rough_pitch.value() - 6900.0) <= 10.0);
    CHECK(sec(notes[1].source_start) <= 0.95);  // the glide belongs to the second note
}

TEST_CASE("a_short_blip_is_dropped_and_gaps_keep_notes_apart", "[segmentation]") {
    const auto track =
        make_track({{.start_s = 0.2, .end_s = 0.6, .midi = 60.0},
                    {.start_s = 0.9, .end_s = 0.93, .midi = 72.0},  // 30 ms: too short
                    {.start_s = 1.2, .end_s = 1.7, .midi = 62.0}},
                   2.0);
    const auto notes = segment(track, make_onsets({0.2, 0.9, 1.2}));
    REQUIRE(notes.size() == 2);
    CHECK(std::abs(notes[0].rough_pitch.value() - 6000.0) <= 10.0);
    CHECK(std::abs(notes[1].rough_pitch.value() - 6200.0) <= 10.0);
}

TEST_CASE("a_consonant_before_the_voiced_part_belongs_to_the_note", "[segmentation]") {
    PitchTrack track = make_track({{.start_s = 0.45, .end_s = 1.0, .midi = 64.0}}, 1.4);
    for (std::size_t i = 76; i < 90; ++i) {  // 0.38 .. 0.45: noisy, unvoiced, but audible
        track.voicing_prob[i] = 0.1;
    }
    SegmentParams params;
    params.preroll_max = Seconds{
        0.120};  // the default is 30 ms (ADR-0011 tuning); this note's consonant is 70 ms long
    const auto notes = segment(track, make_onsets({0.38}, OnsetKind::consonant), params);
    REQUIRE(notes.size() == 1);
    CHECK(std::abs(sec(notes[0].source_start) - 0.38) <= 0.01);
    CHECK(sec(notes[0].attack_end) >=
          0.45 - 0.01);  // the attack covers the consonant and ends in the voiced part
}

TEST_CASE("the_default_preroll_pulls_a_start_back_by_at_most_30_ms", "[segmentation]") {
    PitchTrack track = make_track({{.start_s = 0.45, .end_s = 1.0, .midi = 64.0}}, 1.4);
    for (std::size_t i = 76; i < 90; ++i) {  // a 70 ms unvoiced consonant before the voiced part
        track.voicing_prob[i] = 0.1;
    }
    const auto notes = segment(track, make_onsets({0.38}, OnsetKind::consonant));
    REQUIRE(notes.size() == 1);
    CHECK(sec(notes[0].source_start) >= 0.45 - 0.030 - 0.006);
    CHECK(sec(notes[0].source_start) <= 0.45);
}

TEST_CASE("a_preroll_never_reaches_into_the_previous_note", "[segmentation]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 0.6, .midi = 60.0},
                                   {.start_s = 0.62, .end_s = 1.1, .midi = 67.0}},
                                  1.5);
    // a consonant onset in the 20 ms gap pulls the second start back, but only as far as the first
    // end
    const auto notes = segment(track, make_onsets({0.2, 0.605}, OnsetKind::consonant));
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].source_end <= notes[1].source_start);
    CHECK(sec(notes[1].source_start) >= 0.6 - 1e-9);
    CHECK(sec(notes[1].source_start) <= 0.62);
}

TEST_CASE("nothing_voiced_means_no_notes", "[segmentation]") {
    CHECK(segment(make_track({}, 1.0), make_onsets({})).empty());
    PitchTrack empty;
    empty.grid = FrameGrid{.hop = Seconds{0.005}, .start = SourceTime{}, .n_frames = 0};
    CHECK(segment(empty, OnsetList{}).empty());
}

TEST_CASE("note_grid_covers_the_pitch_frames_inside_the_note", "[segmentation]") {
    const auto track = make_track({{.start_s = 0.302, .end_s = 0.8, .midi = 60.0}}, 1.2);
    NoteSpan span;
    span.source_start = SourceTime{} + Seconds{0.302};
    span.source_end = SourceTime{} + Seconds{0.8};
    const FrameGrid grid = note_grid(span, track.grid);
    CHECK(sec(grid.start) == 0.305);  // first pitch frame at or after the start
    CHECK(grid.hop == track.grid.hop);
    CHECK(grid.n_frames == 99);  // frames at 0.305 ... 0.795
}

TEST_CASE("segmentation_parameters_are_validated", "[segmentation]") {
    const auto track = make_track({{.start_s = 0.2, .end_s = 0.6, .midi = 60.0}}, 1.0);
    SegmentParams p;
    p.min_note = Seconds{0.0};
    CHECK(segment_notes(track, OnsetList{}, p).error().code == ErrorCode::invalid_argument);
    p = SegmentParams{};
    p.stay_stable = 1.5;
    CHECK(segment_notes(track, OnsetList{}, p).error().code == ErrorCode::invalid_argument);
    p = SegmentParams{};
    p.onset_new_note_prob = 1.0;
    CHECK(segment_notes(track, OnsetList{}, p).error().code == ErrorCode::invalid_argument);
    p = SegmentParams{};
    p.note_bin_cents = 0.0;
    CHECK(segment_notes(track, OnsetList{}, p).error().code == ErrorCode::invalid_argument);
    OnsetList bad = make_onsets({0.2});
    bad.kind.pop_back();  // arrays of unequal length
    CHECK(segment_notes(track, bad, SegmentParams{}).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("estimated_pitch_and_onsets_segment_a_sung_melody", "[segmentation]") {
    // Three tones with 100 ms gaps, from audio through the whole chain.
    const double rate = 44100.0;
    const std::vector<std::pair<double, double>> notes_hz{{262.0, 0.5}, {330.0, 0.5}, {392.0, 0.6}};
    std::vector<float> x(static_cast<std::size_t>(2.4 * rate), 0.0F);
    double at = 0.3;
    std::vector<double> starts;
    for (const auto& [f, len] : notes_hz) {
        const auto tone =
            make_harmonic([f](double) { return f; }, len, rate, harmonic_amps(6, 0.4));
        const auto first = static_cast<std::size_t>(std::llround(at * rate));
        for (std::size_t i = 0; i < tone.size(); ++i) {
            const double g = std::min(
                {1.0, static_cast<double>(i) / 44.0, static_cast<double>(tone.size() - i) / 220.0});
            x[first + i] += static_cast<float>(g * tone[i]);
        }
        starts.push_back(at);
        at += len + 0.1;
    }
    const AudioSource src = AudioSource::from_buffer(to_buffer(x, rate));
    const auto pitch = detect_pitch(src, PitchParams{}).value();
    const auto onsets = detect_onsets(src, pitch, OnsetParams{}).value();
    const auto notes = segment_notes(pitch, onsets, SegmentParams{}).value();
    REQUIRE(notes.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(std::abs(sec(notes[i].source_start) - starts[i]) <= 0.02);
        CHECK(std::abs(notes[i].rough_pitch.value() -
                       to_cents(Hz{notes_hz[i].first}, Hz{440.0}).value()) <= 10.0);
    }
}

TEST_CASE("every_segmented_note_can_be_described_on_messy_tracks", "[segmentation]") {
    // Random voicing patches, jittery and jumping f0, random onsets: whatever segment_notes
    // returns, the description stages must accept (the orchestrator aborts the whole analysis on
    // one failure).
    for (int seed = 0; seed < 150; ++seed) {
        INFO("seed " << seed);
        std::mt19937_64 rng{static_cast<std::uint64_t>(seed)};
        std::uniform_real_distribution<double> unit{0.0, 1.0};
        PitchTrack track = make_track({}, 4.0);
        bool voiced = false;
        double midi = 60.0;
        for (std::size_t i = 0; i < track.f0.size(); ++i) {
            if (unit(rng) < 0.03) {
                voiced = !voiced;
            }
            if (unit(rng) < 0.02) {
                midi = 50.0 + 20.0 * unit(rng);
            }
            track.voicing_prob[i] = voiced ? 0.9 : 0.1;
            track.confidence_prob[i] = 0.9;
            track.f0[i] = (voiced && unit(rng) > 0.1)
                              ? Cents{midi * 100.0 + 40.0 * (unit(rng) - 0.5)}
                              : Cents{std::nan("")};
        }
        std::vector<double> onset_times;
        onset_times.reserve(8);
        for (int k = 0; k < 8; ++k) {
            onset_times.push_back(4.0 * unit(rng));
        }
        std::ranges::sort(onset_times);
        const auto spans = segment_notes(track, make_onsets(onset_times), SegmentParams{}).value();
        for (const NoteSpan& span : spans) {
            REQUIRE(note_grid(span, track.grid).n_frames >= 10);
            REQUIRE(decompose_pitch_curve(span, track, {}, {}).has_value());
        }
    }
}
