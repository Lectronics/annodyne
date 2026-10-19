#include "core/edits/dirty_regions.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

std::vector<Note> three_notes() {
    return {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0), make_note("c", 5.0, 6.0)};
}

DirtyParams params() {
    return DirtyParams{.crossfade_margin = Seconds{0.05}, .source_end = src(10.0)};
}

EditList list_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

double start_s(const SourceRange& r) {
    return r.start().unchecked_since_origin().value();
}
double end_s(const SourceRange& r) {
    return r.end().unchecked_since_origin().value();
}

}  // namespace

TEST_CASE("identical_lists_have_no_dirty_regions", "[dirty_regions]") {
    const EditList list = list_of({pitch_add("a", 10)});
    CHECK(dirty_regions(three_notes(), list, list, params()).value().empty());
}

TEST_CASE("a_pitch_edit_dirties_that_note_plus_the_margin_only", "[dirty_regions]") {
    const EditList before{TrackId{"t1"}};
    const auto regions =
        dirty_regions(three_notes(), before, list_of({pitch_add("b", 10)}), params()).value();
    REQUIRE(regions.size() == 1);
    CHECK(start_s(regions[0]) == 2.95);
    CHECK(end_s(regions[0]) == 4.05);
}

TEST_CASE("the_region_is_clamped_at_the_origin", "[dirty_regions]") {
    const std::vector<Note> notes{make_note("a", 0.01, 1.0)};
    const auto regions =
        dirty_regions(notes, EditList{TrackId{"t1"}}, list_of({pitch_add("a", 5)}), params())
            .value();
    REQUIRE(regions.size() == 1);
    CHECK(start_s(regions[0]) == 0.0);
}

TEST_CASE("adjacent_and_overlapping_regions_are_merged", "[dirty_regions]") {
    const std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 2.02, 3.0)};
    const auto regions = dirty_regions(notes, EditList{TrackId{"t1"}},
                                       list_of({pitch_add("a", 5), pitch_add("b", 5)}), params())
                             .value();
    REQUIRE(regions.size() == 1);
    CHECK(start_s(regions[0]) == 0.95);
    CHECK(end_s(regions[0]) == 3.05);
}

TEST_CASE("a_change_that_cancels_out_is_not_dirty", "[dirty_regions]") {
    const EditList before{TrackId{"t1"}};
    const EditList after =
        list_of({pitch_add("a", 10)}).add(std::vector{pitch_add("a", -10)}, k_t0).value();
    CHECK(dirty_regions(three_notes(), before, after, params()).value().empty());
}

TEST_CASE("undoing_an_edit_dirties_the_note_again", "[dirty_regions]") {
    const EditList after = list_of({pitch_add("a", 10)});
    const EditList undone = after.undo().value();
    CHECK(dirty_regions(three_notes(), after, undone, params()).value().size() == 1);
}

TEST_CASE("a_timing_change_dirties_from_the_previous_notes_start_to_the_end_of_the_source",
          "[dirty_regions]") {
    const auto shift =
        make_spec("b", EditCategory::time, EditParam::onset_shift, EditOp::add, Seconds{0.2});
    const auto regions =
        dirty_regions(three_notes(), EditList{TrackId{"t1"}}, list_of({shift}), params()).value();
    REQUIRE(regions.size() == 1);
    CHECK(start_s(regions[0]) == 0.95);  // previous note starts at 1.0, minus margin
    CHECK(end_s(regions[0]) == 10.0);    // includes the tail after the last note
}

TEST_CASE("a_timing_change_on_the_first_note_starts_at_the_origin", "[dirty_regions]") {
    const auto shift =
        make_spec("a", EditCategory::time, EditParam::length_scale, EditOp::set, Ratio{2.0});
    const auto regions =
        dirty_regions(three_notes(), EditList{TrackId{"t1"}}, list_of({shift}), params()).value();
    REQUIRE(regions.size() == 1);
    CHECK(start_s(regions[0]) == 0.0);
}

TEST_CASE("inactive_notes_are_ignored", "[dirty_regions]") {
    auto notes = three_notes();
    notes[1].is_active = false;
    CHECK(dirty_regions(notes, EditList{TrackId{"t1"}}, list_of({pitch_add("b", 10)}), params())
              .value()
              .empty());
}

TEST_CASE("mismatched_tracks_and_unsorted_notes_are_errors", "[dirty_regions]") {
    const EditList other{TrackId{"other"}};
    CHECK(dirty_regions(three_notes(), EditList{TrackId{"t1"}}, other, params()).error().code ==
          ErrorCode::invalid_argument);
    auto notes = three_notes();
    std::swap(notes[0], notes[2]);
    CHECK_FALSE(dirty_regions(notes, EditList{TrackId{"t1"}}, EditList{TrackId{"t1"}}, params())
                    .has_value());
    auto wrong_track = three_notes();
    wrong_track[0].track_id = TrackId{"zzz"};
    CHECK_FALSE(
        dirty_regions(wrong_track, EditList{TrackId{"t1"}}, EditList{TrackId{"t1"}}, params())
            .has_value());
}
