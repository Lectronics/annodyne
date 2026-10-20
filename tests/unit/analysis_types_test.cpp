#include "core/types/analysis_result.h"
#include "core/types/frame_grid.h"
#include "core/types/note.h"
#include "core/types/tracks.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace anno;
using namespace anno::test;

namespace {

Note note_with_grid(std::int64_t n) {
    Note note = make_note("a", 1.0, 2.0);
    note.grid = FrameGrid{.hop = Seconds{0.005}, .start = note.source_start, .n_frames = n};
    note.pitch_drift.assign(static_cast<std::size_t>(n), Cents{});
    note.pitch_mod.assign(static_cast<std::size_t>(n), Cents{});
    note.amp.assign(static_cast<std::size_t>(n), Decibels{-20.0});
    return note;
}

}  // namespace

TEST_CASE("frame_time_is_start_plus_index_times_hop", "[analysis_types]") {
    const FrameGrid grid{.hop = Seconds{0.01}, .start = src(2.0), .n_frames = 100};
    CHECK((frame_time(grid, 50) - src(2.0)).value() == 0.5);
}

TEST_CASE("frame_grid_rejects_bad_hop_and_negative_count", "[analysis_types]") {
    CHECK(validate(FrameGrid{Seconds{0.005}, src(0), 0}).has_value());
    CHECK(validate(FrameGrid{Seconds{0.0}, src(0), 1}).error().code == ErrorCode::invalid_argument);
    CHECK_FALSE(validate(FrameGrid{Seconds{-1.0}, src(0), 1}).has_value());
    CHECK_FALSE(validate(FrameGrid{Seconds{std::numeric_limits<double>::quiet_NaN()}, src(0), 1})
                    .has_value());
    CHECK_FALSE(validate(FrameGrid{Seconds{0.005}, src(0), -1}).has_value());
}

TEST_CASE("frame_matrix_size_must_match_shape", "[analysis_types]") {
    CHECK(validate(FrameMatrix{}).has_value());
    CHECK(validate(FrameMatrix{2, 3, std::vector<float>(6)}).has_value());
    CHECK_FALSE(validate(FrameMatrix{2, 3, std::vector<float>(5)}).has_value());
    CHECK_FALSE(validate(FrameMatrix{-1, 3, {}}).has_value());
}

TEST_CASE("pitch_track_arrays_must_match_the_grid", "[analysis_types]") {
    PitchTrack track;
    track.grid = FrameGrid{.hop = Seconds{0.005}, .start = src(0), .n_frames = 3};
    track.f0.assign(3, Cents{6900});
    track.confidence_prob.assign(3, 0.5);
    track.voicing_prob.assign(3, 1.0);
    CHECK(validate(track).has_value());
    track.alternates.emplace_back(2);
    CHECK_FALSE(validate(track).has_value());
    track.alternates.clear();
    track.voicing_prob[1] = 1.5;
    CHECK_FALSE(validate(track).has_value());
}

TEST_CASE("onset_list_arrays_must_have_equal_length", "[analysis_types]") {
    OnsetList onsets;
    onsets.times = {src(1.0)};
    onsets.strength = {1.0};
    onsets.kind = {OnsetKind::attack};
    CHECK(validate(onsets).has_value());
    onsets.kind.push_back(OnsetKind::consonant);
    CHECK_FALSE(validate(onsets).has_value());
}

TEST_CASE("note_with_consistent_curves_is_valid", "[analysis_types]") {
    CHECK(validate(note_with_grid(10)).has_value());
}

TEST_CASE("note_times_must_be_ordered", "[analysis_types]") {
    Note note = note_with_grid(0);
    note.attack_end = src(0.5);  // before source_start
    CHECK(validate(note).error().code == ErrorCode::invalid_argument);
    note = note_with_grid(0);
    note.release_start = src(3.0);  // after source_end
    CHECK_FALSE(validate(note).has_value());
}

TEST_CASE("note_curve_length_must_equal_grid_frames", "[analysis_types]") {
    Note note = note_with_grid(10);
    note.pitch_mod.pop_back();
    CHECK_FALSE(validate(note).has_value());
}

TEST_CASE("note_nested_grid_must_be_empty_or_equal", "[analysis_types]") {
    Note note = note_with_grid(10);
    note.noise.grid = FrameGrid{.hop = Seconds{0.005}, .start = note.source_start, .n_frames = 10};
    note.noise.band_energy_linear =
        FrameMatrix{.rows = 10, .cols = 2, .data = std::vector<float>(20)};
    note.noise.frame_class.assign(10, FrameClass::voiced);
    CHECK(validate(note).has_value());
    note.noise.grid.hop = Seconds{0.01};
    CHECK_FALSE(validate(note).has_value());
}

TEST_CASE("note_needs_an_id_and_a_probability", "[analysis_types]") {
    Note note = note_with_grid(0);
    note.id = NoteId{};
    CHECK_FALSE(validate(note).has_value());
    note = note_with_grid(0);
    note.confidence_prob = 2.0;
    CHECK_FALSE(validate(note).has_value());
    note = note_with_grid(0);
    note.voice = -1;
    CHECK_FALSE(validate(note).has_value());
}

TEST_CASE("analysis_requires_sorted_unique_valid_notes", "[analysis_types]") {
    AnalysisResult analysis;
    analysis.notes = {make_note("a", 0.0, 1.0), make_note("b", 1.0, 2.0)};
    CHECK(validate(analysis).has_value());

    auto unsorted = analysis;
    std::swap(unsorted.notes[0], unsorted.notes[1]);
    CHECK_FALSE(validate(unsorted).has_value());

    auto duplicate = analysis;
    duplicate.notes[1].id = NoteId{"a"};
    CHECK_FALSE(validate(duplicate).has_value());

    auto bad_tuning = analysis;
    bad_tuning.tuning_ref = Hz{0.0};
    CHECK_FALSE(validate(bad_tuning).has_value());
}

TEST_CASE("find_note_returns_null_when_absent", "[analysis_types]") {
    AnalysisResult analysis;
    analysis.notes = {make_note("a", 0.0, 1.0)};
    REQUIRE(find_note(analysis, NoteId{"a"}) != nullptr);
    CHECK(find_note(analysis, NoteId{"a"})->id == NoteId{"a"});
    CHECK(find_note(analysis, NoteId{"zzz"}) == nullptr);
}

TEST_CASE("note_rejects_non_finite_or_negative_times_and_values", "[analysis_types]") {
    Note note = note_with_grid(0);
    note.source_start = src(-1.0);
    CHECK_FALSE(validate(note).has_value());
    note = note_with_grid(0);
    note.source_end = src(std::numeric_limits<double>::infinity());
    CHECK_FALSE(validate(note).has_value());
    note = note_with_grid(0);
    note.pitch_center = Cents{std::numeric_limits<double>::quiet_NaN()};
    CHECK_FALSE(validate(note).has_value());
    note = note_with_grid(2);
    note.amp[0] = Decibels{std::numeric_limits<double>::quiet_NaN()};
    CHECK_FALSE(validate(note).has_value());
}

TEST_CASE("harmonic_noise_and_envelope_tracks_check_their_shapes", "[analysis_types]") {
    const FrameGrid grid{.hop = Seconds{0.01}, .start = src(0), .n_frames = 2};
    HarmonicTrack h;
    h.grid = grid;
    h.freq_hz = FrameMatrix{.rows = 2, .cols = 3, .data = std::vector<float>(6)};
    h.amp_linear = FrameMatrix{.rows = 2, .cols = 3, .data = std::vector<float>(6)};
    h.phase_rad = FrameMatrix{.rows = 2, .cols = 3, .data = std::vector<float>(6)};
    CHECK(validate(h).has_value());
    h.amp_linear = FrameMatrix{
        .rows = 2, .cols = 4, .data = std::vector<float>(8)};  // different harmonic count
    CHECK_FALSE(validate(h).has_value());
    h.amp_linear =
        FrameMatrix{.rows = 3, .cols = 3, .data = std::vector<float>(9)};  // wrong frame count
    CHECK_FALSE(validate(h).has_value());

    NoiseTrack n;
    n.grid = grid;
    n.band_energy_linear = FrameMatrix{.rows = 2, .cols = 4, .data = std::vector<float>(8)};
    n.frame_class.assign(2, FrameClass::breath);
    CHECK(validate(n).has_value());
    n.frame_class.assign(3, FrameClass::breath);
    CHECK_FALSE(validate(n).has_value());

    EnvelopeTrack e;
    e.grid = grid;
    e.log_mag_db = FrameMatrix{.rows = 2, .cols = 3, .data = std::vector<float>(6)};
    e.freqs.assign(3, Hz{100});
    CHECK(validate(e).has_value());
    e.freqs.pop_back();
    CHECK_FALSE(validate(e).has_value());
}

TEST_CASE("a_default_pitch_target_is_valid_and_unpitched", "[analysis_types][pitch_target]") {
    Note note = note_with_grid(10);
    note.pitch_target = PitchTarget{};
    note.pitch_center = Cents{};
    CHECK(note.pitch_target.kind == PitchTargetKind::unpitched);
    CHECK(validate(note).has_value());
}

TEST_CASE("pitch_target_rules_are_validated", "[analysis_types][pitch_target]") {
    const auto invalid = [](const auto& edit) {
        Note note = note_with_grid(10);
        edit(note);
        return !validate(note).has_value();
    };
    CHECK(invalid(
        [](Note& n) { n.pitch_target.target = Cents{std::numeric_limits<double>::quiet_NaN()}; }));
    CHECK(invalid([](Note& n) { n.pitch_target.confidence_prob = 1.5; }));
    CHECK(invalid([](Note& n) { n.pitch_target.kind = PitchTargetKind::glide; }));  // no secondary
    CHECK(
        invalid([](Note& n) { n.pitch_target.kind = PitchTargetKind::ornament; }));  // no secondary
    CHECK(invalid([](Note& n) {  // unpitched must equal pitch_center
        n.pitch_target.kind = PitchTargetKind::unpitched;
        n.pitch_center = Cents{6900.0};
        n.pitch_target.target = Cents{6800.0};
    }));
    CHECK(invalid([](Note& n) {  // core outside the note
        n.pitch_target = make_whole_target(n.pitch_center);
        n.pitch_target.core = SourceRange::from_start(n.source_end, Seconds{1.0});
    }));
    CHECK(invalid([](Note& n) {  // segments overlap
        n.pitch_target = make_whole_target(n.pitch_center);
        const SourceRange a = SourceRange::from_start(n.source_start, Seconds{0.5});
        const TonalSegment seg{
            .range = a, .shape = TonalShape::level, .start_pitch = Cents{}, .end_pitch = Cents{}};
        n.pitch_target.segments = {seg, seg};
    }));
    Note ok = note_with_grid(10);
    ok.pitch_target = make_whole_target(ok.pitch_center);
    ok.pitch_target.core = SourceRange::from_start(ok.source_start, Seconds{0.5});
    CHECK(validate(ok).has_value());
    ok.pitch_target.kind = PitchTargetKind::glide;
    ok.pitch_target.secondary = Cents{100.0};
    CHECK(validate(ok).has_value());
}

TEST_CASE("pitch_flags_set_clear_and_test_bits", "[analysis_types][pitch_target]") {
    PitchFlags flags;
    CHECK_FALSE(flags.has(PitchFlag::creak));
    flags.set(PitchFlag::creak);
    flags.set(PitchFlag::short_note);
    CHECK(flags.has(PitchFlag::creak));
    CHECK(flags.has(PitchFlag::short_note));
    flags.clear(PitchFlag::creak);
    CHECK_FALSE(flags.has(PitchFlag::creak));
    CHECK(flags.has(PitchFlag::short_note));
}
