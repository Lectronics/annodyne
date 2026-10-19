#include "core/timemap/build_time_map.h"

#include "support/test_data.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

const WallTime k_t0{};

EditSpec timing(const char* id, EditParam p, EditOp op, EditValue v) {
    const EditCategory c = p == EditParam::attack_time_scale ? EditCategory::attack : EditCategory::time;
    return make_spec(id, c, p, op, std::move(v));
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

TimeMapBuild build(const std::vector<Note>& notes, const EditList& edits) {
    return build_time_map(notes, edits, TimeMapParams{}).value();
}

double out_of(const TimeMap& m, double s) { return m.to_output(src(s)).unchecked_since_origin().value(); }
double raw_s(SourceTime t) { return t.unchecked_since_origin().value(); }
double raw_o(OutputTime t) { return t.unchecked_since_origin().value(); }

// Notes: [1,2] attack 0.1 s; [3,4]; [5,6].
std::vector<Note> notes3() {
    return {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0), make_note("c", 5.0, 6.0)};
}

}  // namespace

TEST_CASE("no_timing_edits_gives_the_exact_identity", "[build_time_map]") {
    const auto built = build(notes3(), EditList{TrackId{"t1"}});
    CHECK(built.map.is_identity());
    CHECK(built.repaired_notes == 0);
    // Pitch edits are not timing edits.
    CHECK(build(notes3(), edits_of({pitch_add("a", 30)})).map.is_identity());
}

TEST_CASE("identity_holds_for_touching_notes_a_note_at_zero_and_a_zero_length_attack", "[build_time_map]") {
    std::vector<Note> notes{make_note("a", 0.0, 1.0), make_note("b", 1.0, 2.0)};
    notes[1].attack_end = notes[1].source_start;  // zero-length attack
    notes[0].release_start = notes[0].source_end; // zero-length release
    const auto built = build(notes, EditList{TrackId{"t1"}});
    CHECK(built.map.is_identity());
    CHECK(built.repaired_notes == 0);
}

TEST_CASE("onset_shift_moves_the_note_and_stretches_the_gaps_around_it", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("b", EditParam::onset_shift, EditOp::add, Seconds{0.5})}));
    const TimeMap& m = built.map;
    CHECK(out_of(m, 3.0) == 3.5);
    CHECK(out_of(m, 4.0) == 4.5);
    CHECK(out_of(m, 2.0) == 2.0);   // neighbours stay
    CHECK(out_of(m, 5.0) == 5.0);
    CHECK(out_of(m, 2.5) == 2.75);  // gap before b is stretched 1 s -> 1.5 s
    CHECK(out_of(m, 4.5) == 4.75);  // gap after b is compressed to 0.5 s
    CHECK(built.repaired_notes == 0);
}

TEST_CASE("length_scale_stretches_the_body_and_protects_the_attack", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("a", EditParam::length_scale, EditOp::set, Ratio{2.0})}));
    const TimeMap& m = built.map;
    // note a: attack 1.0..1.1 keeps slope 1, body 1.1..2.0 doubles (0.9 s -> 1.8 s)
    CHECK(out_of(m, 1.0) == 1.0);
    CHECK(std::abs(out_of(m, 1.1) - 1.1) < 1e-12);
    CHECK(m.stretch_ratio(src(1.05)) == Ratio{1.0});
    CHECK(std::abs(m.stretch_ratio(src(1.5)).value() - 2.0) < 1e-12);
    CHECK(std::abs(out_of(m, 2.0) - 2.9) < 1e-12);
    // b is unedited and the gap absorbs it: b still starts at 3.0 (gap 2.9 -> 3.0)
    CHECK(out_of(m, 3.0) == 3.0);
}

TEST_CASE("attack_time_scale_changes_only_the_attack_segment", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("a", EditParam::attack_time_scale, EditOp::set, Ratio{3.0})}));
    const TimeMap& m = built.map;
    CHECK(std::abs(m.stretch_ratio(src(1.05)).value() - 3.0) < 1e-12);
    CHECK(std::abs(out_of(m, 1.1) - 1.3) < 1e-12);
    CHECK(std::abs(m.stretch_ratio(src(1.5)).value() - 1.0) < 1e-12);  // body unchanged
}

TEST_CASE("a_time_handle_inside_the_body_is_an_absolute_anchor", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("a", EditParam::time_handle, EditOp::set,
                                                        TimePair{src(1.5), out(1.8)})}));
    CHECK(std::abs(out_of(built.map, 1.5) - 1.8) < 1e-12);
    CHECK(out_of(built.map, 2.0) == 2.0);  // note end unchanged
    CHECK(built.repaired_notes == 0);
}

TEST_CASE("a_handle_outside_the_note_body_is_ignored", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("a", EditParam::time_handle, EditOp::set,
                                                        TimePair{src(2.5), out(9.0)})}));
    CHECK(built.map.is_identity());
}

TEST_CASE("crossing_handles_are_clamped_and_counted", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("a", EditParam::time_handle, EditOp::set, TimePair{src(1.4), out(1.8)}),
                                                 timing("a", EditParam::time_handle, EditOp::set, TimePair{src(1.6), out(1.5)})}));
    CHECK(built.repaired_notes == 1);
    const double h1 = out_of(built.map, 1.4);
    const double h2 = out_of(built.map, 1.6);
    CHECK(h2 > h1);
}

TEST_CASE("moving_a_note_onto_its_neighbour_pushes_the_neighbour_rigidly", "[build_time_map]") {
    // a [1,2] shifted +2.5 s lands at 3.5..4.5, over b [3,4].
    const auto built = build(notes3(), edits_of({timing("a", EditParam::onset_shift, EditOp::add, Seconds{2.5})}));
    const TimeMap& m = built.map;
    CHECK(built.repaired_notes == 2);  // b is pushed over a, and c is pushed over b
    CHECK(out_of(m, 1.0) == 3.5);
    const double b_start = out_of(m, 3.0);
    CHECK(b_start >= out_of(m, 2.0) + 0.001 - 1e-12);
    // b keeps its length and attack shape (rigid shift)
    CHECK(std::abs((out_of(m, 4.0) - b_start) - 1.0) < 1e-12);
    CHECK(std::abs((out_of(m, 3.1) - b_start) - 0.1) < 1e-12);
    // c is pushed only if needed to stay after b; strictly increasing overall
    CHECK(out_of(m, 5.0) > out_of(m, 4.0));
}

TEST_CASE("a_negative_shift_before_the_origin_is_repaired_not_an_error", "[build_time_map]") {
    std::vector<Note> notes{make_note("a", 0.5, 1.5)};
    const auto built = build(notes, edits_of({timing("a", EditParam::onset_shift, EditOp::add, Seconds{-2.0})}));
    CHECK(built.repaired_notes >= 1);
    CHECK(out_of(built.map, 0.5) >= 0.0);
}

TEST_CASE("abutting_notes_share_one_boundary_anchor", "[build_time_map]") {
    std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 2.0, 3.0)};
    // lengthen a: its end moves to 3.0; b (abutting) is shifted rigidly by 1.0 s
    const auto built = build(notes, edits_of({timing("a", EditParam::length_scale, EditOp::set, Ratio{2.0})}));
    const TimeMap& m = built.map;
    CHECK(std::abs(out_of(m, 2.0) - 2.9) < 1e-12);  // a: 0.1 attack + 0.9 s doubled
    CHECK(std::abs((out_of(m, 3.0) - out_of(m, 2.0)) - 1.0) < 1e-12);  // b keeps its length
}

TEST_CASE("unedited_notes_with_sub_millisecond_gaps_stay_identity_next_to_an_edit", "[build_time_map]") {
    std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 2.0004, 3.0),
                            make_note("c", 3.0002, 4.0)};
    notes[2].source_start = src(3.0002);
    const auto built = build(notes, edits_of({pitch_add("a", 5)}));  // pitch only
    CHECK(built.map.is_identity());
    // a timing edit on the LAST note must not disturb a, b, c's earlier boundaries
    const auto edited = build(notes, edits_of({timing("c", EditParam::onset_shift, EditOp::add, Seconds{0.2})}));
    CHECK(out_of(edited.map, 2.0004) == 2.0004);
    CHECK(out_of(edited.map, 3.0) == 3.0);
}

TEST_CASE("inactive_notes_are_ignored", "[build_time_map]") {
    auto notes = notes3();
    notes[1].is_active = false;
    notes.push_back(make_note("overlap", 1.5, 1.8));  // ghost overlapping a
    notes.back().is_active = false;
    std::ranges::sort(notes, {}, &Note::source_start);
    CHECK(build_time_map(notes, EditList{TrackId{"t1"}}, TimeMapParams{}).has_value());
}

TEST_CASE("overlapping_active_notes_are_an_error", "[build_time_map]") {
    std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 1.5, 2.5)};
    CHECK(build_time_map(notes, EditList{TrackId{"t1"}}, TimeMapParams{}).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("unsorted_notes_and_a_foreign_track_are_errors", "[build_time_map]") {
    auto notes = notes3();
    std::swap(notes[0], notes[2]);
    CHECK_FALSE(build_time_map(notes, EditList{TrackId{"t1"}}, TimeMapParams{}).has_value());
    CHECK_FALSE(build_time_map(notes3(), EditList{TrackId{"other"}}, TimeMapParams{}).has_value());
}

TEST_CASE("the_built_map_inverts_within_tolerance", "[build_time_map]") {
    const auto built = build(notes3(), edits_of({timing("a", EditParam::length_scale, EditOp::set, Ratio{1.5}),
                                                 timing("b", EditParam::onset_shift, EditOp::add, Seconds{0.3})}));
    for (double s = -1.0; s < 8.0; s += 0.137) {
        const double back = raw_s(built.map.to_source(built.map.to_output(src(s))));
        CHECK(std::abs(back - s) <= tolerance::k_timemap_roundtrip.value());
    }
    (void)raw_o;
}
