#include "core/timemap/layout.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

EditSpec timing(const char* id, EditParam p, EditOp op, EditValue v) {
    const EditCategory c =
        p == EditParam::attack_time_scale ? EditCategory::attack : EditCategory::time;
    return make_spec(id, c, p, op, v);
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

// Notes: a [1,2] (attack to 1.1), b [3,4], c [5,6]; recording 7 s.
std::vector<Note> notes3() {
    return {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0), make_note("c", 5.0, 6.0)};
}

LayoutMarkers markers(const std::vector<Note>& notes, const EditList& edits, double end = 7.0) {
    return layout_markers(notes, edits, src(end)).value();
}

double out_s(const LayoutMarker& m) {
    return (m.output - OutputTime{}).value();
}
double src_s(const LayoutMarker& m) {
    return (m.source - SourceTime{}).value();
}

}  // namespace

TEST_CASE("no_timing_edits_gives_the_origin_and_the_fixed_end", "[layout_markers]") {
    const auto m = markers(notes3(), EditList{TrackId{"t1"}});
    REQUIRE(m.markers.size() == 2);
    CHECK(m.markers[0].kind == MarkerKind::origin);
    CHECK(out_s(m.markers[0]) == 0.0);
    CHECK(m.markers[1].kind == MarkerKind::end);
    CHECK(src_s(m.markers[1]) == 7.0);
    CHECK(out_s(m.markers[1]) == 7.0);
    CHECK(m.markers[0].note == k_no_marker_note);
}

TEST_CASE("an_onset_shift_pins_start_attack_end_and_end_at_their_shifted_times",
          "[layout_markers]") {
    const auto m = markers(
        notes3(), edits_of({timing("b", EditParam::onset_shift, EditOp::set, Seconds{0.25})}));
    REQUIRE(m.markers.size() == 5);  // origin, b.start, b.attack_end, b.end, end
    CHECK(m.markers[1].kind == MarkerKind::note_start);
    CHECK(m.markers[1].note == 1);
    CHECK(std::abs(out_s(m.markers[1]) - 3.25) < 1e-12);
    CHECK(m.markers[2].kind == MarkerKind::attack_end);
    CHECK(std::abs(out_s(m.markers[2]) - 3.35) < 1e-12);
    CHECK(m.markers[3].kind == MarkerKind::note_end);
    CHECK(std::abs(out_s(m.markers[3]) - 4.25) < 1e-12);
    CHECK(m.markers[4].kind == MarkerKind::end);
}

TEST_CASE("length_scale_moves_only_the_end_marker_and_attack_scale_the_attack_end",
          "[layout_markers]") {
    const auto length = markers(
        notes3(), edits_of({timing("a", EditParam::length_scale, EditOp::set, Ratio{2.0})}));
    // body 1.1 .. 2.0 doubles: the end plays at 2.9
    CHECK(std::abs(out_s(length.markers[3]) - 2.9) < 1e-12);
    CHECK(std::abs(out_s(length.markers[2]) - 1.1) < 1e-12);
    const auto attack = markers(
        notes3(), edits_of({timing("a", EditParam::attack_time_scale, EditOp::set, Ratio{3.0})}));
    CHECK(std::abs(out_s(attack.markers[2]) - 1.3) < 1e-12);  // the 0.1 s attack plays 0.3 s
    CHECK(std::abs(out_s(attack.markers[3]) - 2.2) < 1e-12);  // and the end follows it
}

TEST_CASE("a_handle_is_an_absolute_marker_inside_the_body_only", "[layout_markers]") {
    const TimePair inside{.source = src(1.5), .output = out(1.8)};
    const auto m =
        markers(notes3(), edits_of({timing("a", EditParam::time_handle, EditOp::set, inside)}));
    bool found = false;
    for (const LayoutMarker& k : m.markers) {
        if (k.kind == MarkerKind::handle) {
            found = true;
            CHECK(src_s(k) == 1.5);
            CHECK(out_s(k) == 1.8);
            CHECK(k.note == 0);
        }
    }
    CHECK(found);
    const TimePair outside{.source = src(2.5), .output = out(2.6)};
    const auto ignored =
        markers(notes3(), edits_of({timing("a", EditParam::time_handle, EditOp::set, outside)}));
    for (const LayoutMarker& k : ignored.markers) {
        CHECK(k.kind != MarkerKind::handle);
    }
}

TEST_CASE("markers_are_sorted_by_source_time_with_the_origin_first", "[layout_markers]") {
    const auto m = markers(
        notes3(), edits_of({timing("c", EditParam::onset_shift, EditOp::set, Seconds{-0.1}),
                            timing("a", EditParam::onset_shift, EditOp::set, Seconds{0.2})}));
    for (std::size_t i = 1; i < m.markers.size(); ++i) {
        CHECK(src_s(m.markers[i - 1]) < src_s(m.markers[i]));
    }
    CHECK(m.markers.front().kind == MarkerKind::origin);
    CHECK(m.markers.back().kind == MarkerKind::end);
}

TEST_CASE("a_note_at_source_zero_moved_later_takes_the_origin", "[layout_markers]") {
    std::vector<Note> notes{make_note("a", 0.0, 1.0)};
    const auto m = markers(
        notes, edits_of({timing("a", EditParam::onset_shift, EditOp::set, Seconds{0.3})}), 3.0);
    CHECK(m.markers.front().kind == MarkerKind::note_start);  // merged with the origin
    CHECK(std::abs(out_s(m.markers.front()) - 0.3) < 1e-12);
    CHECK(m.repaired[0] == false);
}

TEST_CASE("a_zero_length_attack_merges_into_the_start_marker", "[layout_markers]") {
    std::vector<Note> notes{make_note("a", 1.0, 2.0)};
    notes[0].attack_end = notes[0].source_start;
    const auto m =
        markers(notes, edits_of({timing("a", EditParam::onset_shift, EditOp::set, Seconds{0.1})}));
    CHECK(m.markers[1].kind == MarkerKind::note_start);
    CHECK(m.markers[2].kind == MarkerKind::note_end);
}

TEST_CASE("touching_notes_share_one_boundary_marker_and_a_conflict_is_repaired",
          "[layout_markers]") {
    std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 2.0, 3.0)};
    const auto m =
        markers(notes, edits_of({timing("a", EditParam::onset_shift, EditOp::set, Seconds{0.2}),
                                 timing("b", EditParam::onset_shift, EditOp::set, Seconds{0.5})}));
    std::size_t at_two = 0;
    for (const LayoutMarker& k : m.markers) {
        if (src_s(k) == 2.0) {
            ++at_two;
            CHECK(k.kind == MarkerKind::note_start);  // the later note's start
            CHECK(k.note == 1);
            CHECK(std::abs(out_s(k) - 2.5) < 1e-12);  // the later output wins
        }
    }
    CHECK(at_two == 1);
    CHECK(m.repaired[1]);  // b's request differs from a's end: counted on the later note
}

TEST_CASE("a_last_note_that_ends_at_the_recording_end_leaves_no_separate_end_marker",
          "[layout_markers]") {
    std::vector<Note> notes{make_note("a", 1.0, 3.0)};
    const auto m = markers(
        notes, edits_of({timing("a", EditParam::length_scale, EditOp::set, Ratio{0.5})}), 3.0);
    CHECK(m.markers.back().kind == MarkerKind::note_end);
    CHECK(src_s(m.markers.back()) == 3.0);
    CHECK(out_s(m.markers.back()) < 3.0);
}

TEST_CASE("inactive_notes_make_no_markers", "[layout_markers]") {
    auto notes = notes3();
    notes[1].is_active = false;
    const auto m =
        markers(notes, edits_of({timing("b", EditParam::onset_shift, EditOp::set, Seconds{0.4})}));
    CHECK(m.markers.size() == 2);
}

TEST_CASE("the_markers_do_not_depend_on_the_order_the_edits_were_made_in", "[layout_markers]") {
    const auto shift = timing("b", EditParam::onset_shift, EditOp::set, Seconds{0.3});
    const auto length = timing("b", EditParam::length_scale, EditOp::set, Ratio{1.5});
    const auto attack = timing("b", EditParam::attack_time_scale, EditOp::set, Ratio{2.0});
    const auto one = markers(notes3(), edits_of({shift, length, attack}));
    const auto two = markers(notes3(), edits_of({attack, shift, length}));
    const auto three =
        markers(notes3(), edits_of({length}).add(std::vector{attack, shift}, k_t0).value());
    REQUIRE(one.markers.size() == two.markers.size());
    REQUIRE(one.markers.size() == three.markers.size());
    for (std::size_t i = 0; i < one.markers.size(); ++i) {
        CHECK(one.markers[i].source == two.markers[i].source);
        CHECK(one.markers[i].output == two.markers[i].output);
        CHECK(one.markers[i].output == three.markers[i].output);
        CHECK(one.markers[i].kind == three.markers[i].kind);
    }
    // two adds of one parameter equal one add of the sum
    const auto add_a = markers(
        notes3(),
        edits_of({timing("b", EditParam::onset_shift, EditOp::add, Seconds{0.1})})
            .add(std::vector{timing("b", EditParam::onset_shift, EditOp::add, Seconds{0.2})}, k_t0)
            .value());
    const auto add_b = markers(
        notes3(), edits_of({timing("b", EditParam::onset_shift, EditOp::add, Seconds{0.3})}));
    for (std::size_t i = 0; i < add_a.markers.size(); ++i) {
        CHECK(std::abs(out_s(add_a.markers[i]) - out_s(add_b.markers[i])) < 1e-12);
    }
}

TEST_CASE("layout_markers_refuses_what_build_time_map_refuses", "[layout_markers]") {
    const EditList none{TrackId{"t1"}};
    CHECK(layout_markers(notes3(), none, src(0.0)).error().code == ErrorCode::invalid_argument);
    CHECK(layout_markers(notes3(), none, src(5.5)).error().code ==
          ErrorCode::invalid_argument);  // c ends after
    std::vector<Note> overlap{make_note("a", 1.0, 2.5), make_note("b", 2.0, 3.0)};
    CHECK(layout_markers(overlap, none, src(4.0)).error().code == ErrorCode::invalid_argument);
    std::vector<Note> foreign{make_note("a", 1.0, 2.0)};
    foreign[0].track_id = TrackId{"other"};
    CHECK(layout_markers(foreign, none, src(4.0)).error().code == ErrorCode::invalid_argument);
}
