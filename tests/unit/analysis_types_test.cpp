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
    note.grid = FrameGrid{Seconds{0.005}, note.source_start, n};
    note.pitch_drift.assign(static_cast<std::size_t>(n), Cents{});
    note.pitch_mod.assign(static_cast<std::size_t>(n), Cents{});
    note.amp.assign(static_cast<std::size_t>(n), Decibels{-20.0});
    return note;
}

}  // namespace

TEST_CASE("frame_time_is_start_plus_index_times_hop", "[analysis_types]") {
    const FrameGrid grid{Seconds{0.01}, src(2.0), 100};
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
    track.grid = FrameGrid{Seconds{0.005}, src(0), 3};
    track.f0.assign(3, Cents{6900});
    track.confidence_prob.assign(3, 0.5);
    track.voicing_prob.assign(3, 1.0);
    CHECK(validate(track).has_value());
    track.alternates.push_back(std::vector<Cents>(2));
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
    note.noise.grid = FrameGrid{Seconds{0.005}, note.source_start, 10};
    note.noise.band_energy_linear = FrameMatrix{10, 2, std::vector<float>(20)};
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
