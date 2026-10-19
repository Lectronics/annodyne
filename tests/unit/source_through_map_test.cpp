#include "core/synthesis/engine/source_through_map.h"
#include "support/audio_checks.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

TimeSpan span(SpanKind kind, double s0, double s1, double t0, double t1) {
    return TimeSpan{.kind = kind,
                    .source_start = src(s0),
                    .source_end = src(s1),
                    .output_start = out(t0),
                    .output_end = out(t1),
                    .segment = std::nullopt};
}

// 1 s source at 1 kHz so every boundary below is a whole sample: kept [0,0.25), cut [0.25,0.5),
// 0.1 s of silence, [0.5,1.0) shifted to output [0.35,0.85).
constexpr double k_rate = 1000.0;

AudioSource source() {
    return AudioSource::from_buffer(make_signal(1, 1.0, k_rate));
}

TimeMap spliced() {
    return TimeMap::make({span(SpanKind::warp, 0.0, 0.25, 0.0, 0.25),
                          span(SpanKind::cut, 0.25, 0.5, 0.25, 0.25),
                          span(SpanKind::insert, 0.5, 0.5, 0.25, 0.35),
                          span(SpanKind::warp, 0.5, 1.0, 0.35, 0.85)})
        .value();
}

}  // namespace

TEST_CASE("a_range_straddling_two_warps_and_an_insert_is_assembled_from_all_three",
          "[source_through_map]") {
    const AudioSource s = source();
    const auto got =
        read_through_map(s, spliced(), Samples{200}, Samples{300}).value();  // [200,500)
    const AudioBuffer all = s.read(Samples{0}, Samples{1000}).value();
    for (std::size_t i = 0; i < 300; ++i) {
        const std::size_t o = i + 200;
        float want = 0.0F;  // 250..350 is the insert
        if (o < 250) {
            want = all.channel(0)[o];
        } else if (o >= 350) {
            want = all.channel(0)[o + 150];
        }
        REQUIRE(got.channel(0)[i] == want);
    }
}

TEST_CASE("an_empty_range_and_the_whole_output_are_valid", "[source_through_map]") {
    const AudioSource s = source();
    CHECK(read_through_map(s, spliced(), Samples{850}, Samples{0}).value().frame_count().value() ==
          0);
    CHECK(read_through_map(s, spliced(), Samples{0}, Samples{850}).value().frame_count().value() ==
          850);
}

TEST_CASE("bad_ranges_and_mismatched_maps_are_errors", "[source_through_map]") {
    const AudioSource s = source();
    CHECK(read_through_map(s, spliced(), Samples{-1}, Samples{10}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(read_through_map(s, spliced(), Samples{10}, Samples{-1}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(read_through_map(s, spliced(), Samples{800}, Samples{100}).error().code ==
          ErrorCode::out_of_range);  // output is 850 samples
    CHECK(read_through_map(s, TimeMap::identity(Seconds{2.0}), Samples{0}, Samples{10})
              .error()
              .code == ErrorCode::invalid_argument);  // the map is for a longer recording
}

TEST_CASE("only_spans_meeting_the_range_must_be_slope_one", "[source_through_map]") {
    // warp [0,0.5) slope 1, warp [0.5,1.0) -> [0.5,1.5) stretched (slope 2).
    const AudioSource s = source();
    const auto map = TimeMap::make({span(SpanKind::warp, 0.0, 0.5, 0.0, 0.5),
                                    span(SpanKind::warp, 0.5, 1.0, 0.5, 1.5)})
                         .value();
    CHECK(read_through_map(s, map, Samples{0}, Samples{500}).has_value());
    CHECK(read_through_map(s, map, Samples{400}, Samples{200}).error().code ==
          ErrorCode::not_implemented);
}

TEST_CASE("an_identity_map_reads_the_source_exactly", "[source_through_map]") {
    const AudioSource s = source();
    const auto got =
        read_through_map(s, TimeMap::identity(Seconds{1.0}), Samples{0}, Samples{1000}).value();
    CHECK(bit_identical(got, s.read(Samples{0}, Samples{1000}).value()));
}
