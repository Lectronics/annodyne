#include "core/timemap/layout.h"
#include "core/timemap/map_diff.h"
#include "support/test_data.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

EditSpec shift_of(const char* id, double seconds) {
    return make_spec(id, EditCategory::time, EditParam::onset_shift, EditOp::set, Seconds{seconds});
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

// a 1..2, b 3..4, c 5..6, d 7..8 in 9 s.
std::vector<Note> notes4() {
    return {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0), make_note("c", 5.0, 6.0),
            make_note("d", 7.0, 8.0)};
}

TimeMap map_of(const EditList& edits, LayoutMode mode = LayoutMode::absorb_then_stretch) {
    const auto notes = notes4();
    LayoutParams params;
    params.mode = mode;
    return build_layout_time_map(notes, timeline_of(notes, 9.0), edits, params, {}).value().map;
}

double lo_of(const SourceRange& r) {
    return (r.start() - SourceTime{}).value();
}
double hi_of(const SourceRange& r) {
    return (r.end() - SourceTime{}).value();
}

}  // namespace

TEST_CASE("identical_maps_have_no_changed_ranges", "[map_diff]") {
    const TimeMap m = map_of(edits_of({shift_of("b", 0.25)}));
    CHECK(changed_source_ranges(m, m, Seconds{0.02}).value().empty());
}

TEST_CASE("a_second_move_leaves_everything_before_the_first_markers_neighbour_alone",
          "[map_diff]") {
    const TimeMap before = map_of(edits_of({shift_of("a", 0.25)}));
    const TimeMap after = map_of(edits_of({shift_of("a", 0.25), shift_of("d", -0.3)}));
    const auto ranges = changed_source_ranges(before, after, Seconds{0.02}).value();
    REQUIRE(!ranges.empty());
    for (const SourceRange& r : ranges) {
        // a (1..2) is already placed; the new interval a.end..d.start starts in the gap after a
        CHECK(lo_of(r) > 2.0);
    }
    CHECK(hi_of(ranges.back()) <= 9.0);
}

TEST_CASE("a_move_under_flex_dirties_up_to_the_end_but_the_layout_only_its_neighbours",
          "[map_diff]") {
    const TimeMap identity = map_of(EditList{TrackId{"t1"}});
    const auto layout =
        changed_source_ranges(identity, map_of(edits_of({shift_of("b", 0.25)})), Seconds{0.0})
            .value();
    const auto flex =
        changed_source_ranges(
            identity, map_of(edits_of({shift_of("b", 0.25)}), LayoutMode::stretch), Seconds{0.0})
            .value();
    double layout_span = 0.0;
    for (const SourceRange& r : layout) {
        layout_span += hi_of(r) - lo_of(r);
    }
    double flex_span = 0.0;
    for (const SourceRange& r : flex) {
        flex_span += hi_of(r) - lo_of(r);
    }
    CHECK(layout_span < flex_span);
}

TEST_CASE("the_margin_widens_and_merges_ranges_inside_the_recording", "[map_diff]") {
    const TimeMap before = map_of(EditList{TrackId{"t1"}});
    const TimeMap after = map_of(edits_of({shift_of("b", 0.25)}));
    const auto tight = changed_source_ranges(before, after, Seconds{0.0}).value();
    const auto wide = changed_source_ranges(before, after, Seconds{0.5}).value();
    REQUIRE(!tight.empty());
    REQUIRE(!wide.empty());
    CHECK(lo_of(wide.front()) <= lo_of(tight.front()));
    CHECK(lo_of(wide.front()) >= 0.0);
    CHECK(hi_of(wide.back()) <= 9.0);
}

TEST_CASE("bad_inputs_are_an_invalid_argument", "[map_diff]") {
    const TimeMap m = map_of(EditList{TrackId{"t1"}});
    CHECK(changed_source_ranges(m, m, Seconds{-1.0}).error().code == ErrorCode::invalid_argument);
    CHECK(changed_source_ranges(m, TimeMap::identity(Seconds{3.0}), Seconds{0.0}).error().code ==
          ErrorCode::invalid_argument);
}
