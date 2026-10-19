#include "core/types/time_map.h"
#include "support/test_data.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <variant>

using namespace anno;
using namespace anno::test;

namespace {

double s(OutputTime t) {
    return t.unchecked_since_origin().value();
}
double s(SourceTime t) {
    return t.unchecked_since_origin().value();
}

TimeSpan span(SpanKind kind, double s0, double s1, double t0, double t1) {
    return TimeSpan{.kind = kind,
                    .source_start = src(s0),
                    .source_end = src(s1),
                    .output_start = out(t0),
                    .output_end = out(t1),
                    .segment = std::nullopt};
}

// source: warp [0,1)->[0,2) slope 2, cut [1,2), warp [2,4)->[2,4), insert 0.5 s, warp
// [4,5)->[4.5,5.5).
TimeMap spans_map() {
    return TimeMap::make({span(SpanKind::warp, 0, 1, 0, 2), span(SpanKind::cut, 1, 2, 2, 2),
                          span(SpanKind::warp, 2, 4, 2, 4), span(SpanKind::insert, 4, 4, 4, 4.5),
                          span(SpanKind::warp, 4, 5, 4.5, 5.5)})
        .value();
}

// 0->0, 1->2 (slope 2), 3->3 (slope 0.5), then slope 1 to 6.
TimeMap anchor_map() {
    return TimeMap::from_anchors({{.source = src(0.0), .output = out(0.0)},
                                  {.source = src(1.0), .output = out(2.0)},
                                  {.source = src(3.0), .output = out(3.0)}},
                                 Seconds{6.0})
        .value();
}

bool is_generated(const SourceLookup& l) {
    return std::holds_alternative<GeneratedTime>(l);
}
double source_of(const SourceLookup& l) {
    return s(std::get<SourceTime>(l));
}

}  // namespace

TEST_CASE("identity_returns_its_input_exactly", "[time_map]") {
    const TimeMap map = TimeMap::identity(Seconds{10.0});
    CHECK(map.is_identity());
    CHECK(map.source_length() == Seconds{10.0});
    CHECK(map.output_length() == Seconds{10.0});
    const double awkward = 0.1 + 0.2;
    CHECK(s(*map.to_output(src(awkward))) == awkward);
    CHECK(source_of(map.to_source(out(awkward))) == awkward);
    CHECK(map.slope_at(out(5.0)) == Ratio{1.0});
}

TEST_CASE("the_empty_map_is_the_identity_of_an_empty_recording", "[time_map]") {
    for (const TimeMap& map : {TimeMap::identity(Seconds{0.0}), TimeMap::make({}).value()}) {
        CHECK(map.is_identity());
        CHECK(map.spans().empty());
        CHECK(map.output_length() == Seconds{0.0});
        CHECK(s(*map.to_output(src(0.0))) == 0.0);
        CHECK(source_of(map.to_source(out(0.0))) == 0.0);
    }
}

TEST_CASE("an_anchor_list_with_equal_source_and_output_is_identity", "[time_map]") {
    const auto map = TimeMap::from_anchors({{.source = src(0.0), .output = out(0.0)},
                                            {.source = src(0.3), .output = out(0.3)}},
                                           Seconds{1.0})
                         .value();
    CHECK(map.is_identity());
    CHECK(map.source_length() == Seconds{1.0});
    CHECK_FALSE(anchor_map().is_identity());
}

TEST_CASE("forward_map_interpolates_between_anchors", "[time_map]") {
    const TimeMap map = anchor_map();
    CHECK(s(*map.to_output(src(0.5))) == 1.0);
    CHECK(s(*map.to_output(src(1.0))) == 2.0);
    CHECK(s(*map.to_output(src(2.0))) == 2.5);
}

TEST_CASE("slope_is_one_after_the_last_anchor_up_to_the_source_length", "[time_map]") {
    const TimeMap map = anchor_map();
    CHECK(s(*map.to_output(src(4.0))) == 4.0);  // 3 -> 3, then slope 1
    CHECK(s(*map.to_output(src(6.0))) == 6.0);
    const auto shifted =
        TimeMap::from_anchors({{.source = src(1.0), .output = out(1.5)}}, Seconds{3.0}).value();
    CHECK(s(*shifted.to_output(src(0.5))) == 0.75);  // (0,0) is an implicit anchor: stretched
    CHECK(s(*shifted.to_output(src(2.0))) == 2.5);   // then slope 1
    CHECK(shifted.output_length() == Seconds{3.5});
}

TEST_CASE("an_anchor_at_the_source_length_adds_no_empty_span", "[time_map]") {
    const auto map = TimeMap::from_anchors({{.source = src(1.0), .output = out(2.0)},
                                            {.source = src(3.0), .output = out(3.0)}},
                                           Seconds{3.0})
                         .value();
    CHECK(map.spans().size() == 2);
    CHECK(map.output_length() == Seconds{3.0});
}

TEST_CASE("slope_at_is_the_warp_slope_and_zero_in_an_insert", "[time_map]") {
    const TimeMap map = anchor_map();
    CHECK(map.slope_at(out(1.0)) == Ratio{2.0});
    CHECK(map.slope_at(out(2.0)) == Ratio{0.5});  // right derivative at the anchor
    CHECK(map.slope_at(out(5.0)) == Ratio{1.0});
    CHECK(spans_map().slope_at(out(4.2)) == Ratio{0.0});
    CHECK(spans_map().slope_at(out(1.0)) == Ratio{2.0});
}

TEST_CASE("inverse_undoes_forward_within_tolerance", "[time_map]") {
    const TimeMap map = anchor_map();
    for (double v : {0.0, 0.123, 0.999, 1.0, 1.5, 2.9, 3.0, 5.9}) {
        const double back = source_of(map.to_source(*map.to_output(src(v))));
        CHECK(std::abs(back - v) <= tolerance::k_timemap_roundtrip.value());
    }
}

TEST_CASE("a_cut_has_no_output_position_and_an_insert_has_no_source", "[time_map]") {
    const TimeMap map = spans_map();
    CHECK(map.source_length() == Seconds{5.0});
    CHECK(map.output_length() == Seconds{5.5});
    CHECK_FALSE(map.to_output(src(1.0)).has_value());  // start of the cut
    CHECK_FALSE(map.to_output(src(1.5)).has_value());
    CHECK(s(*map.to_output(src(2.0))) == 2.0);  // end of the cut: the next warp answers
    CHECK(is_generated(map.to_source(out(4.0))));
    CHECK(is_generated(map.to_source(out(4.25))));
    CHECK(source_of(map.to_source(out(4.5))) == 4.0);  // end of the insert: the next warp answers
}

TEST_CASE("generated_time_reports_the_offset_inside_the_insert", "[time_map]") {
    const auto lookup = spans_map().to_source(out(4.25));
    REQUIRE(is_generated(lookup));
    CHECK(std::get<GeneratedTime>(lookup).offset == Seconds{0.25});
    CHECK(std::get<GeneratedTime>(spans_map().to_source(out(4.0))).offset == Seconds{0.0});
}

TEST_CASE("the_ends_of_the_domain_map_to_the_ends_of_the_output", "[time_map]") {
    const TimeMap map = spans_map();
    CHECK(s(*map.to_output(src(0.0))) == 0.0);
    CHECK(s(*map.to_output(src(5.0))) == 5.5);
    CHECK(source_of(map.to_source(out(5.5))) == 5.0);
    CHECK(source_of(map.to_source(out(0.0))) == 0.0);
}

TEST_CASE("to_output_end_is_left_continuous_at_boundaries", "[time_map]") {
    const TimeMap map = spans_map();
    CHECK(s(*map.to_output_end(src(1.0))) == 2.0);         // end of the first warp, not the cut
    CHECK_FALSE(map.to_output_end(src(2.0)).has_value());  // end of the cut
    CHECK(s(*map.to_output_end(src(4.0))) == 4.0);         // end of the warp before the insert
    CHECK(s(*map.to_output_end(src(5.0))) == 5.5);
    CHECK(s(*map.to_output_end(src(0.5))) == 1.0);  // interior: same as to_output
}

TEST_CASE("a_leading_insert_and_a_trailing_cut_are_valid", "[time_map]") {
    const auto map =
        TimeMap::make({span(SpanKind::insert, 0, 0, 0, 1), span(SpanKind::warp, 0, 2, 1, 3),
                       span(SpanKind::cut, 2, 3, 3, 3)})
            .value();
    CHECK(is_generated(map.to_source(out(0.5))));
    CHECK(source_of(map.to_source(out(1.0))) == 0.0);
    CHECK(s(*map.to_output(src(0.0))) == 1.0);
    CHECK_FALSE(map.to_output(src(2.5)).has_value());
    CHECK(s(*map.to_output(src(3.0))) == 3.0);  // the closing point
    CHECK(source_of(map.to_source(out(3.0))) == 2.0);
    CHECK(map.is_identity() == false);
}

TEST_CASE("a_cut_and_an_insert_at_one_position_are_valid", "[time_map]") {
    const auto map =
        TimeMap::make({span(SpanKind::warp, 0, 1, 0, 1), span(SpanKind::cut, 1, 2, 1, 1),
                       span(SpanKind::insert, 2, 2, 1, 1.5), span(SpanKind::warp, 2, 3, 1.5, 2.5)})
            .value();
    CHECK_FALSE(map.to_output(src(1.5)).has_value());
    CHECK(is_generated(map.to_source(out(1.25))));
    CHECK(s(*map.to_output(src(2.0))) == 1.5);
    CHECK(source_of(map.to_source(out(1.5))) == 2.0);
}

TEST_CASE("segment_ids_are_carried_to_generated_time", "[time_map]") {
    TimeSpan insert = span(SpanKind::insert, 1, 1, 1, 2);
    insert.segment = SegmentId{.kind = SegmentKind::gap, .value = "g1"};
    const auto map =
        TimeMap::make({span(SpanKind::warp, 0, 1, 0, 1), insert, span(SpanKind::warp, 1, 2, 2, 3)})
            .value();
    const auto lookup = map.to_source(out(1.5));
    REQUIRE(is_generated(lookup));
    CHECK(map.spans()[std::get<GeneratedTime>(lookup).span].segment->value == "g1");
    CHECK(std::get<GeneratedTime>(lookup).span == 1);
}

TEST_CASE("make_rejects_spans_that_do_not_tile_or_have_the_wrong_shape", "[time_map]") {
    const auto bad = [](std::vector<TimeSpan> spans) {
        const auto r = TimeMap::make(std::move(spans));
        return !r.has_value() && r.error().code == ErrorCode::invalid_argument;
    };
    CHECK(bad({span(SpanKind::warp, 0.5, 1, 0.5, 1)}));  // does not start at (0, 0)
    CHECK(bad(
        {span(SpanKind::warp, 0, 1, 0, 1), span(SpanKind::warp, 1.5, 2, 1, 1.5)}));  // source gap
    CHECK(
        bad({span(SpanKind::warp, 0, 1, 0, 1), span(SpanKind::warp, 1, 2, 1.5, 2)}));  // output gap
    CHECK(bad({span(SpanKind::warp, 0, 1, 0, 1), span(SpanKind::warp, 0.5, 2, 1, 2)}));  // overlap
    CHECK(bad({span(SpanKind::warp, 0, 0, 0, 1)}));    // a warp with no source
    CHECK(bad({span(SpanKind::warp, 0, 1, 0, 0)}));    // a warp with no output
    CHECK(bad({span(SpanKind::cut, 0, 1, 0, 1)}));     // a cut with output
    CHECK(bad({span(SpanKind::insert, 0, 1, 0, 1)}));  // an insert with source
    CHECK(bad({span(SpanKind::cut, 0, 0, 0, 0)}));     // empty cut
    CHECK(bad({span(SpanKind::warp, 0, 1, 0, std::nan(""))}));
    CHECK(bad({span(SpanKind::warp, 0, 1, 0, -1)}));
}

TEST_CASE("from_anchors_rejects_bad_input", "[time_map]") {
    const Seconds len{10.0};
    CHECK(TimeMap::from_anchors({}, len).error().code == ErrorCode::invalid_argument);
    CHECK_FALSE(
        TimeMap::from_anchors({{src(1.0), out(1.0)}, {src(1.0), out(2.0)}}, len).has_value());
    CHECK_FALSE(
        TimeMap::from_anchors({{src(1.0), out(2.0)}, {src(2.0), out(2.0)}}, len).has_value());
    CHECK_FALSE(
        TimeMap::from_anchors({{src(1.0), out(2.0)}, {src(2.0), out(1.0)}}, len).has_value());
    CHECK_FALSE(TimeMap::from_anchors({{src(std::nan("")), out(1.0)}}, len).has_value());
    CHECK_FALSE(
        TimeMap::from_anchors({{src(0.0), out(-0.5)}}, len).has_value());  // before output 0
    CHECK_FALSE(
        TimeMap::from_anchors({{src(11.0), out(11.0)}}, len).has_value());  // beyond the end
    CHECK_FALSE(TimeMap::from_anchors({{src(1.0), out(1.0)}}, Seconds{-1.0}).has_value());
}

TEST_CASE("an_anchor_at_source_zero_with_later_output_makes_a_leading_insert", "[time_map]") {
    const auto map =
        TimeMap::from_anchors({{.source = src(0.0), .output = out(0.5)}}, Seconds{2.0}).value();
    REQUIRE(map.spans().size() == 2);
    CHECK(map.spans()[0].kind == SpanKind::insert);
    CHECK(map.output_length() == Seconds{2.5});
    CHECK(is_generated(map.to_source(out(0.25))));
    CHECK(s(*map.to_output(src(0.0))) == 0.5);
    CHECK(source_of(map.to_source(out(0.5))) == 0.0);
}
