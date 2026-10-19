#include "core/render/blend_plan.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace anno;

namespace {

Ramp ramp(std::int64_t b, std::int64_t e, int from, int to) {
    return {.begin = b, .end = e, .from = from, .to = to};
}

}  // namespace

TEST_CASE("no_edited_notes_give_no_ramps", "[blend_plan]") {
    CHECK(plan_blend({}, 480).empty());
}

TEST_CASE("one_note_gets_a_rise_and_a_fall", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 2000}};
    const auto ramps = plan_blend(spans, 100);
    REQUIRE(ramps.size() == 2);
    CHECK(ramps[0] == ramp(900, 1000, k_pristine, 0));
    CHECK(ramps[1] == ramp(2000, 2100, 0, k_pristine));
}

TEST_CASE("neighbours_within_one_margin_are_crossfaded_into_each_other", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 2000},
                                        {.begin = 2000, .end = 3000}};  // gap 0
    const auto ramps = plan_blend(spans, 100);
    REQUIRE(ramps.size() == 3);
    CHECK(ramps[1] == ramp(1900, 2100, 0, 1));  // length 2m - g, centred on the boundary
    const std::vector<SampleSpan> at_margin{{.begin = 1000, .end = 2000},
                                            {.begin = 2100, .end = 3000}};  // gap == m
    CHECK(plan_blend(at_margin, 100)[1] == ramp(2000, 2100, 0, 1));         // length m
}

TEST_CASE("a_gap_between_one_and_two_margins_shortens_both_edge_ramps", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 2000},
                                        {.begin = 2150, .end = 3000}};  // gap 150, m 100
    const auto ramps = plan_blend(spans, 100);
    REQUIRE(ramps.size() == 4);
    CHECK(ramps[1] == ramp(2000, 2075, 0, k_pristine));  // met in the middle of the overlap
    CHECK(ramps[2] == ramp(2075, 2150, k_pristine, 1));
}

TEST_CASE("a_gap_over_two_margins_keeps_full_ramps", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 2000}, {.begin = 2500, .end = 3000}};
    const auto ramps = plan_blend(spans, 100);
    REQUIRE(ramps.size() == 4);
    CHECK(ramps[1] == ramp(2000, 2100, 0, k_pristine));
    CHECK(ramps[2] == ramp(2400, 2500, k_pristine, 1));
}

TEST_CASE("an_odd_overlap_splits_with_the_floor", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 2000},
                                        {.begin = 2151, .end = 3000}};  // gap 151, overlap 49
    const auto ramps = plan_blend(spans, 100);
    CHECK(ramps[1].end == 2000 + 24 + 51);  // rise starts at 2051 + 24
    CHECK(ramps[1].end == ramps[2].begin);
}

TEST_CASE("three_short_merged_notes_never_overlap_their_ramps", "[blend_plan]") {
    const std::vector<SampleSpan> spans{
        {.begin = 1000, .end = 1100}, {.begin = 1100, .end = 1200}, {.begin = 1200, .end = 1300}};
    const auto ramps = plan_blend(spans, 480);  // notes much shorter than the margin
    for (std::size_t j = 0; j + 1 < ramps.size(); ++j) {
        CHECK(ramps[j].begin <= ramps[j].end);
        CHECK(ramps[j].end <= ramps[j + 1].begin);
        CHECK(ramps[j].to == ramps[j + 1].from);
    }
}

TEST_CASE("a_zero_margin_makes_every_ramp_a_hard_switch", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 2000}, {.begin = 2000, .end = 3000}};
    for (const Ramp& r : plan_blend(spans, 0)) {
        CHECK(r.begin == r.end);
    }
}

TEST_CASE("an_empty_span_is_tolerated", "[blend_plan]") {
    const std::vector<SampleSpan> spans{{.begin = 1000, .end = 1000}};
    const auto ramps = plan_blend(spans, 50);
    REQUIRE(ramps.size() == 2);
    CHECK(ramps[0].end == 1000);
    CHECK(ramps[1].begin == 1000);
}
