#include "core/types/analysis_result.h"
#include "core/types/noise_floor.h"
#include "core/types/segments.h"
#include "support/test_data.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace anno;
using namespace anno::test;

namespace {

/// gap [0,1] | a [1,2] | gap [2,3] | b [3,4] | gap [4,5]
struct Parts {
    std::vector<Segment> segments;
    std::vector<Gap> gaps;
};

Parts standard() {
    return {.segments = {std::size_t{0}, note_seg("a", 1, 2), std::size_t{1}, note_seg("b", 3, 4),
                         std::size_t{2}},
            .gaps = {gap_between(nullptr, "a", 0, 1), gap_between("a", "b", 2, 3),
                     gap_between("b", nullptr, 4, 5)}};
}

Result<SegmentTimeline> make(const Parts& p, double end = 5.0) {
    return SegmentTimeline::make(p.segments, p.gaps, src(end));
}

ErrorCode code(const Result<SegmentTimeline>& r) {
    return r.error().code;
}

}  // namespace

TEST_CASE("a_tiling_of_notes_and_gaps_is_accepted", "[segment_timeline]") {
    const auto t = make(standard()).value();
    CHECK(t.segments().size() == 5);
    CHECK(t.gaps().size() == 3);
    CHECK(t.source_end() == src(5.0));
    CHECK(t.gap_before(NoteId{"a"}) == t.gaps().data());
    CHECK(t.gap_after(NoteId{"a"}) == (t.gaps().data() + 1));
    CHECK(t.gap_before(NoteId{"b"}) == (t.gaps().data() + 1));
    CHECK(t.gap_after(NoteId{"b"}) == (t.gaps().data() + 2));
    CHECK(t.gap_before(NoteId{"zz"}) == nullptr);
}

TEST_CASE("touching_notes_have_no_gap_between_them", "[segment_timeline]") {
    Parts p{.segments = {note_seg("a", 0, 1), note_seg("b", 1, 2)}, .gaps = {}};
    const auto t = make(p, 2.0).value();
    CHECK(t.gap_after(NoteId{"a"}) == nullptr);
    CHECK(t.gap_before(NoteId{"b"}) == nullptr);
}

TEST_CASE("the_empty_timeline_has_a_zero_source_end", "[segment_timeline]") {
    CHECK(SegmentTimeline::make({}, {}, src(0.0)).has_value());
    CHECK(code(SegmentTimeline::make({}, {}, src(1.0))) == ErrorCode::invalid_argument);
}

TEST_CASE("a_recording_with_no_notes_is_one_gap", "[segment_timeline]") {
    Parts p{.segments = {std::size_t{0}}, .gaps = {gap_between(nullptr, nullptr, 0, 3)}};
    const auto t = make(p, 3.0).value();
    CHECK(t.gaps()[0].id == gap_segment_id(std::nullopt, std::nullopt));
}

TEST_CASE("holes_overlaps_and_zero_length_segments_are_refused", "[segment_timeline]") {
    Parts hole = standard();
    hole.gaps[1] = gap_between("a", "b", 2.1, 3);  // hole 2..2.1
    CHECK(code(make(hole)) == ErrorCode::invalid_argument);
    Parts overlap = standard();
    overlap.segments[3] = note_seg("b", 2.9, 4);
    overlap.gaps[1] = gap_between("a", "b", 2, 2.9);
    CHECK(make(overlap).has_value());             // a shorter gap and a longer note still tile
    overlap.segments[3] = note_seg("b", 2.8, 4);  // the gap still ends at 2.9: overlap
    CHECK(code(make(overlap)) == ErrorCode::invalid_argument);
    Parts zero = standard();
    zero.segments[1] = note_seg("a", 1, 1);
    CHECK(code(make(zero)) == ErrorCode::invalid_argument);
    CHECK(code(make(standard(), 5.5)) == ErrorCode::invalid_argument);  // does not reach source_end
    CHECK(code(make(standard(), 4.5)) == ErrorCode::invalid_argument);  // passes source_end
    CHECK(code(make(standard(), std::numeric_limits<double>::quiet_NaN())) ==
          ErrorCode::invalid_argument);
}

TEST_CASE("two_adjacent_gaps_are_refused", "[segment_timeline]") {
    Parts p{.segments = {std::size_t{0}, std::size_t{1}},
            .gaps = {gap_between(nullptr, nullptr, 0, 1), gap_between(nullptr, nullptr, 1, 2)}};
    CHECK(code(make(p, 2.0)) == ErrorCode::invalid_argument);
}

TEST_CASE("a_gap_id_must_be_the_one_its_neighbours_give", "[segment_timeline]") {
    Parts wrong = standard();
    wrong.gaps[1].id = SegmentId{.kind = SegmentKind::gap, .value = "gap:a|b"};
    CHECK(code(make(wrong)) == ErrorCode::invalid_argument);
    Parts as_note = standard();
    as_note.gaps[1].id = segment_id(NoteId{"x"});
    CHECK(code(make(as_note)) == ErrorCode::invalid_argument);
}

TEST_CASE("a_gap_must_name_its_neighbouring_notes", "[segment_timeline]") {
    Parts p = standard();
    p.gaps[1] = gap_between("b", "a", 2, 3);  // the right id for the wrong pair
    CHECK(code(make(p)) == ErrorCode::invalid_argument);
    Parts leading = standard();
    leading.gaps[0] = gap_between("a", "a", 0, 1);  // a leading gap cannot have a before
    CHECK(code(make(leading)) == ErrorCode::invalid_argument);
    Parts trailing = standard();
    trailing.gaps[2] = gap_between("b", "a", 4, 5);  // a trailing gap cannot have an after
    CHECK(code(make(trailing)) == ErrorCode::invalid_argument);
}

TEST_CASE("the_regions_of_a_gap_must_tile_it", "[segment_timeline]") {
    Parts split = standard();
    split.gaps[1].regions = {region(GapRegionKind::tail, 2.0, 2.4),
                             region(GapRegionKind::silence, 2.4, 3.0)};
    CHECK(make(split).has_value());
    Parts hole = standard();
    hole.gaps[1].regions = {region(GapRegionKind::tail, 2.0, 2.4),
                            region(GapRegionKind::silence, 2.5, 3.0)};
    CHECK(code(make(hole)) == ErrorCode::invalid_argument);
    Parts short_end = standard();
    short_end.gaps[1].regions = {region(GapRegionKind::silence, 2.0, 2.9)};
    CHECK(code(make(short_end)) == ErrorCode::invalid_argument);
    Parts none = standard();
    none.gaps[1].regions.clear();
    CHECK(code(make(none)) == ErrorCode::invalid_argument);
    Parts zero = standard();
    zero.gaps[1].regions = {region(GapRegionKind::tail, 2.0, 2.0),
                            region(GapRegionKind::silence, 2.0, 3.0)};
    CHECK(code(make(zero)) == ErrorCode::invalid_argument);
}

TEST_CASE("gap_indices_and_ids_must_be_consistent", "[segment_timeline]") {
    Parts out_of_range = standard();
    out_of_range.segments[0] = std::size_t{7};
    CHECK(code(make(out_of_range)) == ErrorCode::invalid_argument);
    Parts twice = standard();
    twice.segments[4] = std::size_t{1};  // gap 1 used twice
    CHECK(code(make(twice)) == ErrorCode::invalid_argument);
    Parts unused = standard();
    unused.gaps.push_back(gap_between("b", nullptr, 4, 5));
    CHECK(code(make(unused)) == ErrorCode::invalid_argument);
    Parts dup_notes = standard();
    dup_notes.segments[3] = note_seg("a", 3, 4);
    dup_notes.gaps[1] = gap_between("a", "a", 2, 3);
    dup_notes.gaps[2] = gap_between("a", nullptr, 4, 5);
    CHECK(code(make(dup_notes)) == ErrorCode::invalid_argument);
    Parts floor = standard();
    floor.gaps[1].floor_dbfs = Decibels{std::nan("")};
    CHECK(code(make(floor)) == ErrorCode::invalid_argument);
}

TEST_CASE("at_is_right_continuous_and_the_source_end_is_in_the_last_segment",
          "[segment_timeline]") {
    const auto t = make(standard()).value();
    const auto is_note = [&](double s, const char* id) {
        const auto* n = std::get_if<NoteSegment>(&t.at(src(s)));
        return n != nullptr && n->note.value == id;
    };
    CHECK(std::holds_alternative<std::size_t>(t.at(src(0.0))));
    CHECK(std::holds_alternative<std::size_t>(t.at(src(0.999))));
    CHECK(is_note(1.0, "a"));  // a boundary belongs to the later segment
    CHECK(is_note(1.5, "a"));
    CHECK(std::get<std::size_t>(t.at(src(2.0))) == 1);
    CHECK(is_note(3.0, "b"));
    CHECK(std::get<std::size_t>(t.at(src(5.0))) == 2);  // the end is in the last segment
}

TEST_CASE("range_of_gives_the_range_of_notes_and_gaps", "[segment_timeline]") {
    const auto t = make(standard()).value();
    const SourceRange a = t.range_of(t.segments()[1]);
    CHECK(a.start() == src(1.0));
    CHECK(a.end() == src(2.0));
    const SourceRange g = t.range_of(t.segments()[2]);
    CHECK(g.start() == src(2.0));
    CHECK(g.end() == src(3.0));
}

TEST_CASE("gap_ids_cannot_collide_whatever_the_note_ids_say", "[segment_timeline]") {
    const auto id = [](const char* a, const char* b) {
        return gap_segment_id(NoteId{a}, NoteId{b});
    };
    CHECK(id("ab", "c") != id("a", "bc"));
    CHECK(id("a|b", "c") != id("a", "b|c"));
    CHECK(id("a:1", "x") != id("a", "1:x"));
    CHECK(gap_segment_id(std::nullopt, NoteId{"n1"}) != gap_segment_id(NoteId{"n1"}, std::nullopt));
    CHECK(id("a", "b").kind == SegmentKind::gap);
    CHECK(id("a", "b") != segment_id(NoteId{"gap:1:a1:b"}));  // a gap id never equals a note id
}

// ---- NoiseFloor and AnalysisResult

namespace {

NoiseFloor floor_of(std::int64_t frames) {
    NoiseFloor f;
    f.grid = FrameGrid{.hop = Seconds{0.02}, .start = src(0.0), .n_frames = frames};
    f.curve_dbfs.assign(static_cast<std::size_t>(frames), Decibels{-60.0});
    f.band_centre = {Hz{100.0}, Hz{1000.0}, Hz{10000.0}};
    f.band_dbfs = {Decibels{-70.0}, Decibels{-75.0}, Decibels{-80.0}};
    return f;
}

AnalysisResult analysis_with_notes() {
    AnalysisResult a;
    a.notes = {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)};
    return a;
}

}  // namespace

TEST_CASE("a_noise_floor_must_be_consistent", "[segment_timeline]") {
    CHECK(validate(floor_of(100)).has_value());
    NoiseFloor curve = floor_of(100);
    curve.curve_dbfs.pop_back();
    CHECK_FALSE(validate(curve).has_value());
    NoiseFloor bands = floor_of(100);
    bands.band_dbfs.pop_back();
    CHECK_FALSE(validate(bands).has_value());
    NoiseFloor descending = floor_of(100);
    descending.band_centre = {Hz{1000.0}, Hz{100.0}, Hz{10000.0}};
    CHECK_FALSE(validate(descending).has_value());
    NoiseFloor not_finite = floor_of(100);
    not_finite.curve_dbfs[3] = Decibels{std::nan("")};
    CHECK_FALSE(validate(not_finite).has_value());
    NoiseFloor grid = floor_of(100);
    grid.grid.hop = Seconds{0.0};
    CHECK_FALSE(validate(grid).has_value());
}

TEST_CASE("an_analysis_validates_with_a_matching_timeline_and_floor_or_without_either",
          "[segment_timeline]") {
    AnalysisResult a = analysis_with_notes();
    CHECK(validate(a).has_value());  // no timeline, no floor: as before
    a.timeline = make(standard()).value();
    a.noise_floor = floor_of(250);
    CHECK(validate(a).has_value());
}

TEST_CASE("a_stale_or_incomplete_timeline_fails_validation", "[segment_timeline]") {
    AnalysisResult a = analysis_with_notes();
    a.timeline = make(standard()).value();
    a.notes[0] = make_note("a", 1.0, 2.1);  // the note changed, the timeline did not
    CHECK(validate(a).error().code == ErrorCode::invalid_argument);
    AnalysisResult missing = analysis_with_notes();
    missing.notes.push_back(make_note("c", 4.5, 4.9));
    missing.timeline = make(standard()).value();  // c is an active note the timeline lacks
    CHECK_FALSE(validate(missing).has_value());
    AnalysisResult extra = analysis_with_notes();
    extra.notes.pop_back();  // the timeline holds a note that no longer exists
    extra.timeline = make(standard()).value();
    CHECK_FALSE(validate(extra).has_value());
}

TEST_CASE("ghost_notes_are_not_in_the_timeline", "[segment_timeline]") {
    AnalysisResult a = analysis_with_notes();
    a.notes.push_back(make_note("ghost", 4.2, 4.6));
    a.notes.back().is_active = false;
    a.timeline = make(standard()).value();
    CHECK(validate(a).has_value());
}

TEST_CASE("an_invalid_floor_fails_the_analysis", "[segment_timeline]") {
    AnalysisResult a = analysis_with_notes();
    a.noise_floor = floor_of(10);
    a.noise_floor->band_dbfs.pop_back();
    CHECK_FALSE(validate(a).has_value());
}
