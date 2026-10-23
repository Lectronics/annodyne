#include "core/timemap/layout.h"
#include "support/test_data.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
constexpr double k_eps = 1e-9;

EditSpec timing(const char* id, EditParam p, EditOp op, EditValue v) {
    const EditCategory c =
        p == EditParam::attack_time_scale ? EditCategory::attack : EditCategory::time;
    return make_spec(id, c, p, op, v);
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

EditSpec shift_of(const char* id, double seconds) {
    return timing(id, EditParam::onset_shift, EditOp::set, Seconds{seconds});
}

EditSpec scale_of(const char* id, double ratio) {
    return timing(id, EditParam::length_scale, EditOp::set, Ratio{ratio});
}

// Notes a [1,2], b [3,4], c [5,6]; recording 7 s; gaps g0 [0,1], gA [2,3], gB [4,5], gC [6,7].
std::vector<Note> notes3() {
    return {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0), make_note("c", 5.0, 6.0)};
}

struct Fixture {
    std::vector<Note> notes;
    SegmentTimeline timeline;
    double end;
};

Fixture fixture_of(std::vector<Note> notes, double end) {
    SegmentTimeline timeline = timeline_of(notes, end);
    return {.notes = std::move(notes), .timeline = std::move(timeline), .end = end};
}

Fixture fixture3() {
    return fixture_of(notes3(), 7.0);
}

/// The same timeline with the regions of gap `index` replaced.
Fixture with_regions(Fixture f, std::size_t index, std::vector<GapRegion> regions) {
    std::vector<Gap> gaps(f.timeline.gaps().begin(), f.timeline.gaps().end());
    gaps[index].regions = std::move(regions);
    f.timeline = SegmentTimeline::make({f.timeline.segments().begin(), f.timeline.segments().end()},
                                       gaps, src(f.end))
                     .value();
    return f;
}

Layout solve(const Fixture& f, const EditList& edits, const LayoutParams& params = {},
             double rate_hz = 0.0) {
    const LayoutMarkers markers = layout_markers(f.notes, edits, src(f.end)).value();
    TimeMapParams tm;
    tm.sample_rate = Hz{rate_hz};
    return solve_layout(f.timeline, markers, params, tm).value();
}

double s_of(SourceTime t) {
    return (t - SourceTime{}).value();
}
double o_of(OutputTime t) {
    return (t - OutputTime{}).value();
}
double delta_of(const IntervalPlan& i) {
    return (o_of(i.out_to) - o_of(i.out_from)) - (s_of(i.to) - s_of(i.from));
}

bool near(double a, double b) {
    return std::abs(a - b) < k_eps;
}

/// Gap edit length plus the stretch of the other audio gives the interval's change (conservation).
void check_conserved(const Layout& layout, const SegmentTimeline& timeline) {
    double total = 0.0;
    for (const IntervalPlan& i : layout.intervals) {
        const double s = s_of(i.to) - s_of(i.from);
        double taken = 0.0;
        double other = s;
        if (i.gap) {
            const Gap& g = timeline.gaps()[i.gap->gap];
            taken = i.gap->cut ? -i.gap->length.value() : i.gap->length.value();
            other = s - (s_of(g.end) - s_of(g.start));
        }
        CHECK(near(delta_of(i), taken + (i.residual_slope - 1.0) * other));
        total += delta_of(i);
    }
    CHECK(near(total, 0.0));  // both ends are fixed
}

}  // namespace

TEST_CASE("no_timing_edits_is_one_interval_with_slope_one", "[solve_layout]") {
    const auto f = fixture3();
    const Layout layout = solve(f, EditList{TrackId{"t1"}});
    REQUIRE(layout.intervals.size() == 1);
    CHECK(near(s_of(layout.intervals[0].from), 0.0));
    CHECK(near(s_of(layout.intervals[0].to), 7.0));
    CHECK(!layout.intervals[0].gap);
    CHECK(layout.intervals[0].residual_slope == 1.0);
    CHECK(!layout.fell_back);
    CHECK(layout.repaired == std::vector<bool>(3, false));
}

TEST_CASE("a_note_moved_later_inserts_in_the_leading_gap_and_cuts_the_gap_after_it",
          "[solve_layout]") {
    const auto f = fixture3();
    const Layout layout = solve(f, edits_of({shift_of("a", 0.25)}));
    REQUIRE(layout.intervals.size() == 4);  // origin|start, start|attack, attack|end, end|end
    const IntervalPlan& lead = layout.intervals[0];
    REQUIRE(lead.gap);
    CHECK(!lead.gap->cut);
    CHECK(lead.gap->gap == 0);
    CHECK(near(lead.gap->length.value(), 0.25));
    CHECK(near(s_of(lead.gap->at), 0.5));  // centred in [0, 1]
    CHECK(near(lead.residual_slope, 1.0));
    CHECK(near(o_of(lead.out_to), 1.25));
    CHECK(!layout.intervals[1].gap);
    CHECK(!layout.intervals[2].gap);
    const IntervalPlan& tail = layout.intervals[3];
    REQUIRE(tail.gap);
    CHECK(tail.gap->cut);
    CHECK(tail.gap->gap == 1);  // gA, the gap after a's end
    CHECK(near(tail.gap->length.value(), 0.25));
    CHECK(near(s_of(tail.gap->at), 2.5 - 0.125));
    CHECK(near(tail.residual_slope, 1.0));
    CHECK(near(o_of(tail.out_from), 2.25));
    CHECK(near(o_of(tail.out_to), 7.0));
    check_conserved(layout, f.timeline);
}

TEST_CASE("a_gap_that_is_too_short_takes_what_it_can_and_the_rest_is_stretched", "[solve_layout]") {
    // a [1,2], b [2.2,3]: gA is 0.2 s, so room = 0.2 - min_gap 0.02 - 2 * (0.01 + 0.005) = 0.15.
    const auto f = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)}, 4.0);
    const Layout layout = solve(f, edits_of({scale_of("a", 2.0)}));  // a's end plays at 3.8 - 1.0
    const IntervalPlan& last = layout.intervals.back();
    REQUIRE(last.gap);
    CHECK(last.gap->cut);
    CHECK(near(last.gap->length.value(), 0.15));
    // delta -0.9; the gap took 0.15; the other 1.8 s of audio gives 0.75
    CHECK(near(last.residual_slope, (1.8 - 0.75) / 1.8));
    check_conserved(layout, f.timeline);
}

TEST_CASE("the_cut_goes_in_the_longest_silence_region_not_in_other_kinds", "[solve_layout]") {
    const auto f = with_regions(fixture3(), 1,
                                {region(GapRegionKind::silence, 2.0, 2.3),
                                 region(GapRegionKind::sibilant, 2.3, 2.5),
                                 region(GapRegionKind::silence, 2.5, 3.0)});
    const Layout layout = solve(f, edits_of({shift_of("a", 0.25)}));
    const IntervalPlan& tail = layout.intervals.back();
    REQUIRE(tail.gap);
    CHECK(near(s_of(tail.gap->at), 2.75 - 0.125));  // centred in [2.5, 3.0]
    check_conserved(layout, f.timeline);
}

TEST_CASE("the_room_of_a_cut_leaves_min_gap_and_the_margins_around_it", "[solve_layout]") {
    // gA's silence is 0.2 s; a request to cut 0.9 can take only room = 0.15.
    const auto f = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)}, 4.0);
    LayoutParams params;
    params.min_gap = Seconds{0.04};
    const Layout layout = solve(f, edits_of({scale_of("a", 2.0)}), params);
    const IntervalPlan& last = layout.intervals.back();
    REQUIRE(last.gap);
    CHECK(near(last.gap->length.value(), 0.2 - 0.04 - 0.03));
    const double left = s_of(last.gap->at) - 2.0;
    const double right = 2.2 - (s_of(last.gap->at) + last.gap->length.value());
    CHECK(near(left, right));
    CHECK(left >= 0.015 - k_eps);  // note_margin + splice_fade
    // a silence with no room at all: no cut, the whole interval is squished
    const auto narrow = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.04, 3.0)}, 4.0);
    const Layout none = solve(narrow, edits_of({scale_of("a", 1.5)}));
    CHECK(!none.intervals.back().gap);
    check_conserved(none, narrow.timeline);
}

TEST_CASE("a_gap_without_silence_absorbs_nothing_and_the_interval_is_stretched", "[solve_layout]") {
    const auto f = with_regions(fixture3(), 1,
                                {region(GapRegionKind::tail, 2.0, 2.3),
                                 region(GapRegionKind::protected_preroll, 2.3, 3.0)});
    const Layout layout = solve(f, edits_of({shift_of("a", 0.25)}));
    const IntervalPlan& tail = layout.intervals.back();
    CHECK(!tail.gap);
    CHECK(near(tail.residual_slope, (5.0 - 0.25) / 5.0));
    CHECK(layout.intervals.front().gap);  // g0 is still silence
    check_conserved(layout, f.timeline);
}

TEST_CASE("a_legato_end_has_no_gap_after_it_and_the_trailing_gap_absorbs", "[solve_layout]") {
    const auto f = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.0, 4.0)}, 5.0);
    const Layout layout = solve(f, edits_of({scale_of("a", 1.2)}));  // a ends 0.18 later
    const IntervalPlan& last = layout.intervals.back();
    REQUIRE(last.gap);
    CHECK(last.gap->cut);
    CHECK(last.gap->gap == 1);  // the trailing gap [4, 5]
    CHECK(near(last.gap->length.value(), 0.18));
    CHECK(near(last.residual_slope, 1.0));
    // the body of a, attack_end..end, has no gap and takes +0.18 over 0.9 s
    CHECK(near(layout.intervals[2].residual_slope, (0.9 + 0.18) / 0.9));
    check_conserved(layout, f.timeline);
}

TEST_CASE("with_no_gap_anywhere_the_interval_is_stretched", "[solve_layout]") {
    const auto f = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.0, 4.0)}, 4.0);
    const Layout layout = solve(f, edits_of({scale_of("a", 1.2)}));
    const IntervalPlan& last = layout.intervals.back();
    CHECK(!last.gap);
    CHECK(near(last.residual_slope, (2.0 - 0.18) / 2.0));
    check_conserved(layout, f.timeline);
}

TEST_CASE("the_gap_before_the_right_marker_absorbs_when_the_left_note_end_has_none",
          "[solve_layout]") {
    // a [1,2] legato b [2,3], gap gB [3,4], c [4,5]; a ends 0.09 early, c starts 0.3 early.
    const auto f = fixture_of(
        {make_note("a", 1.0, 2.0), make_note("b", 2.0, 3.0), make_note("c", 4.0, 5.0)}, 6.0);
    const Layout layout = solve(f, edits_of({scale_of("a", 0.9), shift_of("c", -0.3)}));
    const IntervalPlan* between = nullptr;
    for (const IntervalPlan& i : layout.intervals) {
        if (near(s_of(i.from), 2.0) && near(s_of(i.to), 4.0)) {
            between = &i;
        }
    }
    REQUIRE(between != nullptr);
    REQUIRE(between->gap);
    CHECK(between->gap->cut);
    CHECK(f.timeline.gaps()[between->gap->gap].before == NoteId{"b"});
    CHECK(near(between->gap->length.value(), 0.21));
    CHECK(near(s_of(between->gap->at), 3.5 - 0.105));
    check_conserved(layout, f.timeline);
}

TEST_CASE("growth_stretches_the_audio_and_holds_the_silence_at_slope_one", "[solve_layout]") {
    // b moves 0.25 later with a unedited: the interval origin..b.start (3 s) holds g0 (1 s) as
    // silence at slope 1 and stretches the other 2 s (a and gA) by 0.25; nothing is inserted.
    const auto f = fixture3();
    const Layout layout = solve(f, edits_of({shift_of("b", 0.25)}));
    const IntervalPlan& first = layout.intervals.front();
    REQUIRE(first.gap);
    CHECK(first.gap->gap == 0);
    CHECK(!first.gap->cut);
    CHECK(first.gap->length.value() == 0.0);
    CHECK(near(first.residual_slope, 2.25 / 2.0));
    check_conserved(layout, f.timeline);
}

TEST_CASE("growth_the_audio_cannot_take_lengthens_the_silence", "[solve_layout]") {
    // Only 0.1 s of audio beside a 1 s gap: the audio takes 3 x 0.1 (slope 4), the gap the rest.
    const auto f = fixture_of({make_note("a", 1.0, 1.1), make_note("b", 2.0, 3.0)}, 4.0);
    const Layout layout = solve(f, edits_of({shift_of("a", 0.5)}));
    const IntervalPlan& lead = layout.intervals.front();  // origin..a.start: a gap only
    REQUIRE(lead.gap);
    CHECK(near(lead.gap->length.value(), 0.5));
    CHECK(near(lead.residual_slope, 1.0));
    check_conserved(layout, f.timeline);
    const auto tight = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)}, 4.0);
    const Layout spill = solve(tight, edits_of({shift_of("b", 1.0)}));
    // the interval a.. b.start has 0.2 s of gap and no other audio before it
    for (const IntervalPlan& i : spill.intervals) {
        CHECK(i.residual_slope <= k_max_layout_slope + 1e-9);
    }
    check_conserved(spill, tight.timeline);
}

TEST_CASE("stretch_mode_never_uses_a_gap", "[solve_layout]") {
    const auto f = fixture3();
    LayoutParams params;
    params.mode = LayoutMode::stretch;
    const Layout layout = solve(f, edits_of({shift_of("a", 0.25)}), params);
    REQUIRE(layout.intervals.size() == 4);
    CHECK(!layout.intervals[0].gap);
    CHECK(near(layout.intervals[0].residual_slope, 1.25));
    CHECK(!layout.intervals[3].gap);
    CHECK(near(layout.intervals[3].residual_slope, 4.75 / 5.0));
    check_conserved(layout, f.timeline);
}

TEST_CASE("a_request_beyond_the_slope_limit_moves_the_right_marker_and_marks_its_note",
          "[solve_layout]") {
    // Only note b [1,2] in a 3 s recording; b starts 0.98 early, but the leading gap can cut 0.95.
    const auto f = fixture_of({make_note("b", 1.0, 2.0)}, 3.0);
    const Layout layout = solve(f, edits_of({shift_of("b", -0.98)}));
    const IntervalPlan& first = layout.intervals.front();
    REQUIRE(first.gap);
    CHECK(near(first.gap->length.value(), 0.95));
    CHECK(near(o_of(first.out_to), 0.05));  // moved from 0.02 to what the gap can take
    CHECK(layout.repaired == std::vector<bool>{true});
    // the next interval sees the changed marker: b.start..attack_end now squishes 0.03 less
    CHECK(!layout.intervals[1].gap);
    CHECK(near(layout.intervals[1].residual_slope, (0.1 - 0.03) / 0.1));
    check_conserved(layout, f.timeline);
}

TEST_CASE("an_unfittable_request_at_the_fixed_end_falls_back_to_the_unedited_layout",
          "[solve_layout]") {
    const auto f = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)}, 4.0);
    const Layout layout = solve(f, edits_of({scale_of("a", 3.0)}));  // a ends 1.8 late; max 1.5
    CHECK(layout.fell_back);
    CHECK(layout.repaired == std::vector<bool>(2, true));
    for (const IntervalPlan& i : layout.intervals) {
        CHECK(!i.gap);
        CHECK(near(delta_of(i), 0.0));
    }
}

TEST_CASE("with_a_sample_rate_every_shift_and_gap_edit_is_a_whole_number_of_samples",
          "[solve_layout]") {
    constexpr double k_rate = 44100.0;
    const auto f = fixture3();
    const Layout layout = solve(f, edits_of({shift_of("a", 0.25003)}), {}, k_rate);
    for (const IntervalPlan& i : layout.intervals) {
        const double shift_samples = (o_of(i.out_from) - s_of(i.from)) * k_rate;
        CHECK(std::abs(shift_samples - std::round(shift_samples)) < 1e-6);
        if (i.gap) {
            const double n = i.gap->length.value() * k_rate;
            CHECK(std::abs(n - std::round(n)) < 1e-6);
            const double at = s_of(i.gap->at) * k_rate;
            CHECK(std::abs(at - std::round(at)) < 1e-6);
        }
    }
    check_conserved(layout, f.timeline);
    // the room of a short gap is floored to whole samples: 0.15 s is exactly 6615 samples
    const auto small = fixture_of({make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)}, 4.0);
    const Layout cut = solve(small, edits_of({scale_of("a", 2.0)}), {}, k_rate);
    REQUIRE(cut.intervals.back().gap);
    CHECK(std::abs(cut.intervals.back().gap->length.value() * k_rate - 6615.0) < 1e-6);
}

TEST_CASE("the_layout_does_not_depend_on_the_order_the_edits_were_made_in", "[solve_layout]") {
    const auto f = fixture3();
    const Layout one = solve(f, edits_of({shift_of("a", 0.25), shift_of("c", -0.1)}));
    const std::vector<EditSpec> first{shift_of("c", -0.1)};
    const std::vector<EditSpec> second{shift_of("a", 0.25)};
    const EditList two = EditList{TrackId{"t1"}}.add(first, k_t0).value().add(second, k_t0).value();
    const Layout other = solve(f, two);
    REQUIRE(one.intervals.size() == other.intervals.size());
    for (std::size_t k = 0; k < one.intervals.size(); ++k) {
        CHECK(one.intervals[k].out_from == other.intervals[k].out_from);
        CHECK(one.intervals[k].out_to == other.intervals[k].out_to);
        CHECK(one.intervals[k].residual_slope == other.intervals[k].residual_slope);
        CHECK(one.intervals[k].gap.has_value() == other.intervals[k].gap.has_value());
    }
}

TEST_CASE("markers_that_do_not_sit_on_the_timeline_are_an_invalid_argument", "[solve_layout]") {
    const auto f = fixture3();
    const LayoutMarkers markers =
        layout_markers(notes3(), edits_of({shift_of("b", 0.25)}), src(7.0)).value();
    const Fixture other = fixture_of({make_note("x", 1.5, 2.5), make_note("y", 4.0, 5.0)}, 7.0);
    CHECK(solve_layout(other.timeline, markers, {}, {}).error().code ==
          ErrorCode::invalid_argument);
    const Fixture shorter = fixture_of(notes3(), 8.0);
    CHECK(solve_layout(shorter.timeline, markers, {}, {}).error().code ==
          ErrorCode::invalid_argument);
}
