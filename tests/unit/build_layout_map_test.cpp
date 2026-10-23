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
constexpr double k_rate = 44100.0;

EditSpec shift_of(const char* id, double seconds) {
    return make_spec(id, EditCategory::time, EditParam::onset_shift, EditOp::set, Seconds{seconds});
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

std::vector<Note> notes3() {
    return {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0), make_note("c", 5.0, 6.0)};
}

TimeMapBuild build(const std::vector<Note>& notes, double end, const EditList& edits,
                   const LayoutParams& params = {}, double rate_hz = 0.0) {
    TimeMapParams tm;
    tm.sample_rate = Hz{rate_hz};
    return build_layout_time_map(notes, timeline_of(notes, end), edits, params, tm).value();
}

double out_of(const TimeMap& m, double source_s) {
    return ((*m.to_output(src(source_s))) - OutputTime{}).value();
}
bool near(double a, double b) {
    return std::abs(a - b) < k_eps;
}
double s(SourceTime t) {
    return (t - SourceTime{}).value();
}
double o(OutputTime t) {
    return (t - OutputTime{}).value();
}
bool whole(double seconds) {
    const double n = seconds * k_rate;
    return std::abs(n - std::round(n)) < 1e-6;
}

}  // namespace

TEST_CASE("no_timing_edits_gives_the_identity_map", "[build_layout_map]") {
    const auto built = build(notes3(), 7.0, EditList{TrackId{"t1"}});
    CHECK(built.map.is_identity());
    CHECK(built.repaired_notes == 0);
    CHECK(near(built.map.output_length().value(), 7.0));
}

TEST_CASE("a_moved_note_keeps_its_neighbours_in_place_and_the_gaps_change", "[build_layout_map]") {
    const auto built = build(notes3(), 7.0, edits_of({shift_of("a", 0.25)}));
    const TimeMap& m = built.map;
    CHECK(near(out_of(m, 1.0), 1.25));  // a plays 0.25 later: the leading gap got longer
    CHECK(std::abs(out_of(m, 2.0 - 1e-6) - 2.25) < 1e-5);
    CHECK(near(out_of(m, 3.0), 3.0));  // b stays: gA gave the 0.25 back
    CHECK(near(out_of(m, 5.0), 5.0));
    CHECK(near(m.output_length().value(), 7.0));
    int inserts = 0;
    int cuts = 0;
    for (const TimeSpan& span : m.spans()) {
        inserts += span.kind == SpanKind::insert ? 1 : 0;
        cuts += span.kind == SpanKind::cut ? 1 : 0;
        if (span.kind != SpanKind::warp) {
            REQUIRE(span.segment);
            CHECK(span.segment->kind == SegmentKind::gap);
        }
    }
    CHECK(inserts == 1);
    CHECK(cuts == 1);
}

TEST_CASE("growth_stretches_the_audio_before_the_marker_and_keeps_the_gap_at_slope_one",
          "[build_layout_map]") {
    const auto built = build(notes3(), 7.0, edits_of({shift_of("b", 0.25)}));
    const TimeMap& m = built.map;
    CHECK(near(out_of(m, 0.5), 0.5));                // g0 held at slope 1
    CHECK(near(out_of(m, 2.0), 1.0 + 1.0 * 1.125));  // a..gA stretched 2 s -> 2.25 s from 1.0
    CHECK(near(out_of(m, 3.0), 3.25));
    CHECK(std::abs(out_of(m, 4.0 - 1e-6) - 4.25) < 1e-5);
    CHECK(near(out_of(m, 7.0), 7.0));
    int cuts = 0;
    for (const TimeSpan& span : m.spans()) {
        CHECK(span.kind != SpanKind::insert);  // nothing is inserted: the audio took the growth
        cuts += span.kind == SpanKind::cut ? 1 : 0;
    }
    CHECK(cuts == 1);  // b's end moved with it, so the gap after b gave the 0.25 back
}

TEST_CASE("every_span_tiles_and_the_map_round_trips", "[build_layout_map]") {
    const auto built = build(notes3(), 7.0, edits_of({shift_of("a", 0.25), shift_of("c", -0.4)}));
    const TimeMap& m = built.map;
    double src_at = 0.0;
    double out_at = 0.0;
    for (const TimeSpan& span : m.spans()) {
        CHECK(near(s(span.source_start), src_at));
        CHECK(near(o(span.output_start), out_at));
        src_at = s(span.source_end);
        out_at = o(span.output_end);
    }
    CHECK(near(src_at, 7.0));
    CHECK(near(out_at, m.output_length().value()));
    for (int k = 0; k < 74; ++k) {
        const double t = 0.0937 * k;
        const auto out = m.to_output(src(t));
        if (!out) {
            continue;  // inside a cut
        }
        const auto back = m.to_source(*out);
        REQUIRE(std::holds_alternative<SourceTime>(back));
        CHECK(near(s(std::get<SourceTime>(back)), t));
    }
}

TEST_CASE("cuts_and_inserts_lie_inside_silence_regions", "[build_layout_map]") {
    const auto notes = notes3();
    const SegmentTimeline timeline = timeline_of(notes, 7.0);
    TimeMapParams tm;
    const auto built =
        build_layout_time_map(notes, timeline, edits_of({shift_of("a", 0.25), shift_of("c", -0.4)}),
                              {}, tm)
            .value();
    for (const TimeSpan& span : built.map.spans()) {
        if (span.kind == SpanKind::warp) {
            continue;
        }
        const double at = s(span.source_start);
        bool inside = false;
        for (const Gap& g : timeline.gaps()) {
            for (const GapRegion& r : g.regions) {
                inside = inside || (r.kind == GapRegionKind::silence && at >= s(r.start) - k_eps &&
                                    s(span.source_end) <= s(r.end) + k_eps);
            }
        }
        CHECK(inside);
    }
}

TEST_CASE("with_a_sample_rate_the_map_is_whole_samples_on_both_axes", "[build_layout_map]") {
    const auto built =
        build(notes3(), 7.0, edits_of({shift_of("a", 0.25003), shift_of("b", 0.0131)}), {}, k_rate);
    for (const TimeSpan& span : built.map.spans()) {
        CHECK(whole(s(span.source_start)));
        CHECK(whole(s(span.source_end)));
        CHECK(whole(o(span.output_start)));
        CHECK(whole(o(span.output_end)));
        const double src_len = s(span.source_end) - s(span.source_start);
        const double out_len = o(span.output_end) - o(span.output_start);
        if (span.kind == SpanKind::warp && std::abs(src_len - out_len) < 1e-9) {
            CHECK(whole(o(span.output_start) - s(span.source_start)));
        }
    }
}

TEST_CASE("stretch_mode_is_build_time_map_bit_for_bit", "[build_layout_map]") {
    const auto notes = notes3();
    const auto edits = edits_of({shift_of("a", 0.25), shift_of("c", -0.4)});
    LayoutParams params;
    params.mode = LayoutMode::stretch;
    TimeMapParams tm;
    tm.source_end = src(7.0);
    tm.sample_rate = Hz{k_rate};
    const auto layout =
        build_layout_time_map(notes, timeline_of(notes, 7.0), edits, params, tm).value();
    const auto flex = build_time_map(notes, edits, tm).value();
    REQUIRE(layout.map.spans().size() == flex.map.spans().size());
    for (std::size_t i = 0; i < flex.map.spans().size(); ++i) {
        const TimeSpan& a = layout.map.spans()[i];
        const TimeSpan& b = flex.map.spans()[i];
        CHECK(a.kind == b.kind);
        CHECK(a.source_start == b.source_start);
        CHECK(a.source_end == b.source_end);
        CHECK(a.output_start == b.output_start);
        CHECK(a.output_end == b.output_end);
    }
    CHECK(layout.repaired_notes == flex.repaired_notes);
}

TEST_CASE("a_note_at_the_start_pushed_later_begins_with_an_insert", "[build_layout_map]") {
    const std::vector<Note> notes{make_note("a", 0.0, 1.0), make_note("b", 2.0, 3.0)};
    const auto built = build(notes, 4.0, edits_of({shift_of("a", 0.1)}));
    REQUIRE(!built.map.spans().empty());
    CHECK(built.map.spans().front().kind == SpanKind::insert);
    CHECK(near(built.map.output_length().value(), 4.0));
    CHECK(near(out_of(built.map, 0.0), 0.1));
}

TEST_CASE("a_fallback_layout_counts_every_note_repaired", "[build_layout_map]") {
    const std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)};
    const std::vector<EditSpec> group{
        make_spec("a", EditCategory::time, EditParam::length_scale, EditOp::set, Ratio{3.0})};
    const auto built = build(notes, 4.0, edits_of(group));
    CHECK(built.repaired_notes == 2);
    CHECK(built.map.is_identity());
}

TEST_CASE("a_cut_outside_silence_is_refused", "[build_layout_map]") {
    const auto notes = notes3();
    const SegmentTimeline timeline = timeline_of(notes, 7.0);
    Layout layout;
    layout.repaired.assign(3, false);
    IntervalPlan plan;
    plan.from = src(0.0);
    plan.to = src(7.0);
    plan.out_from = OutputTime{};
    plan.out_to = out(6.9);
    plan.gap = GapEdit{.gap = 1, .cut = true, .at = src(1.5), .length = Seconds{0.1}};  // in note a
    layout.intervals.push_back(plan);
    CHECK(build_layout_map(timeline, layout, {}).error().code == ErrorCode::invalid_argument);
}
