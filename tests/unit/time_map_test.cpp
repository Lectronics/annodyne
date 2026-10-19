#include "core/types/time_map.h"
#include "support/test_data.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

// 0->0, 1->2 (slope 2), 3->3 (slope 0.5), then slope 1.
TimeMap sample_map() {
    return TimeMap::from_anchors({{.source = src(0.0), .output = out(0.0)},
                                  {.source = src(1.0), .output = out(2.0)},
                                  {.source = src(3.0), .output = out(3.0)}})
        .value();
}

double s(OutputTime t) {
    return t.unchecked_since_origin().value();
}
double s(SourceTime t) {
    return t.unchecked_since_origin().value();
}

}  // namespace

TEST_CASE("identity_returns_its_input_exactly", "[time_map]") {
    const TimeMap map = TimeMap::identity();
    CHECK(map.is_identity());
    const double awkward = 0.1 + 0.2;
    CHECK(s(map.to_output(src(awkward))) == awkward);
    CHECK(s(map.to_source(out(awkward))) == awkward);
    CHECK(map.stretch_ratio(src(5.0)) == Ratio{1.0});
}

TEST_CASE("an_anchor_list_with_equal_source_and_output_is_identity", "[time_map]") {
    const auto map = TimeMap::from_anchors({{.source = src(0.0), .output = out(0.0)},
                                            {.source = src(0.3), .output = out(0.3)}})
                         .value();
    CHECK(map.is_identity());
    CHECK_FALSE(sample_map().is_identity());
}

TEST_CASE("forward_map_interpolates_between_anchors", "[time_map]") {
    const TimeMap map = sample_map();
    CHECK(s(map.to_output(src(0.5))) == 1.0);
    CHECK(s(map.to_output(src(1.0))) == 2.0);
    CHECK(s(map.to_output(src(2.0))) == 2.5);
}

TEST_CASE("slope_is_one_outside_the_anchors", "[time_map]") {
    const TimeMap map = sample_map();
    CHECK(s(map.to_output(src(4.0))) == 4.0);  // 3 -> 3, then slope 1
    CHECK(s(map.to_output(src(-1.0))) == -1.0);
    const auto shifted = TimeMap::from_anchors({{.source = src(1.0), .output = out(1.5)}}).value();
    CHECK(s(shifted.to_output(src(0.0))) == 0.5);
    CHECK(s(shifted.to_output(src(2.0))) == 2.5);
}

TEST_CASE("stretch_ratio_is_the_segment_slope", "[time_map]") {
    const TimeMap map = sample_map();
    CHECK(map.stretch_ratio(src(0.5)) == Ratio{2.0});
    CHECK(map.stretch_ratio(src(1.0)) == Ratio{0.5});  // right derivative at the anchor
    CHECK(map.stretch_ratio(src(2.5)) == Ratio{0.5});
    CHECK(map.stretch_ratio(src(9.0)) == Ratio{1.0});
    CHECK(map.stretch_ratio(src(-3.0)) == Ratio{1.0});
}

TEST_CASE("inverse_undoes_forward_within_tolerance", "[time_map]") {
    const TimeMap map = sample_map();
    for (double v : {-2.0, 0.0, 0.123, 0.999, 1.0, 1.5, 2.9, 3.0, 7.7}) {
        const SourceTime back = map.to_source(map.to_output(src(v)));
        CHECK(std::abs(s(back) - v) <= tolerance::k_timemap_roundtrip.value());
    }
}

TEST_CASE("from_anchors_rejects_bad_input", "[time_map]") {
    CHECK(TimeMap::from_anchors({}).error().code == ErrorCode::invalid_argument);
    CHECK_FALSE(TimeMap::from_anchors({{src(1.0), out(1.0)}, {src(1.0), out(2.0)}}).has_value());
    CHECK_FALSE(TimeMap::from_anchors({{src(1.0), out(2.0)}, {src(2.0), out(2.0)}}).has_value());
    CHECK_FALSE(TimeMap::from_anchors({{src(1.0), out(2.0)}, {src(2.0), out(1.0)}}).has_value());
    CHECK_FALSE(TimeMap::from_anchors({{src(std::nan("")), out(1.0)}}).has_value());
}
