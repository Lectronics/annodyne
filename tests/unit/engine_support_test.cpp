#include "core/synthesis/engine/engine_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

using namespace anno;

TEST_CASE("only_pitch_edited_allows_center_and_onset_shift_only", "[engine_support]") {
    EffectiveNote e;
    CHECK(only_pitch_edited(e));
    e.center = Cents{300.0};
    e.onset_shift = Seconds{0.01};
    CHECK(only_pitch_edited(e));
    e.gain = Decibels{1.0};
    CHECK_FALSE(only_pitch_edited(e));
    e = {};
    e.length_scale = Ratio{2.0};
    CHECK_FALSE(only_pitch_edited(e));
}

TEST_CASE("only_timing_edited_allows_the_timing_fields_and_a_neutral_centre", "[engine_support]") {
    const Cents centre{6900.0};
    EffectiveNote e;
    e.center = centre;
    CHECK(only_timing_edited(e, centre));
    e.onset_shift = Seconds{0.2};
    e.length_scale = Ratio{1.5};
    e.attack_time_scale = Ratio{2.0};
    CHECK(only_timing_edited(e, centre));
    e.center = centre + Cents{1.0};
    CHECK_FALSE(only_timing_edited(e, centre));
    e.center = centre;
    e.formant_offset = Cents{10.0};
    CHECK_FALSE(only_timing_edited(e, centre));
    e = {};
    e.center = centre;
    e.gain = Decibels{1.0};
    CHECK_FALSE(only_timing_edited(e, centre));
}

TEST_CASE("hex64_is_sixteen_lowercase_digits", "[engine_support]") {
    CHECK(hex64(0) == "0000000000000000");
    CHECK(hex64(0xDEADBEEF12345678ULL) == "deadbeef12345678");
}

TEST_CASE("zone_shift_reads_whole_sample_shifts_of_a_slope_one_warp", "[engine_support]") {
    const Hz rate{1000.0};
    const auto zone = OutputRange::from_start(OutputTime{} + Seconds{0.2}, Seconds{0.1});
    CHECK(zone_shift(TimeMap::identity(Seconds{1.0}), rate, zone, "T").value() == 0);
    const TimeSpan shifted{.kind = SpanKind::warp,
                           .source_start = SourceTime{},
                           .source_end = SourceTime{} + Seconds{1.0},
                           .output_start = OutputTime{},
                           .output_end = OutputTime{} + Seconds{1.0},
                           .segment = std::nullopt};
    CHECK(zone_shift(TimeMap::make({shifted}).value(), rate, zone, "T").value() == 0);
    const TimeSpan stretched{.kind = SpanKind::warp,
                             .source_start = SourceTime{},
                             .source_end = SourceTime{} + Seconds{1.0},
                             .output_start = OutputTime{},
                             .output_end = OutputTime{} + Seconds{2.0},
                             .segment = std::nullopt};
    const auto err = zone_shift(TimeMap::make({stretched}).value(), rate, zone, "Eng");
    CHECK(err.error().code == ErrorCode::not_implemented);
    CHECK(err.error().message.starts_with("Eng"));
    const auto outside = OutputRange::from_start(OutputTime{} + Seconds{0.9}, Seconds{0.5});
    CHECK(zone_shift(TimeMap::identity(Seconds{1.0}), rate, outside, "T").error().code ==
          ErrorCode::not_implemented);
}

TEST_CASE("run_weights_are_one_inside_a_shifted_run_zero_outside_with_raised_cosine_edges",
          "[engine_support]") {
    // Frames every 100 samples; frames 2..4 shifted. A run ends half a hop beyond its last frame.
    std::vector<RunFrame> frames;
    frames.reserve(8);
    for (int i = 0; i < 8; ++i) {
        frames.push_back({.position = 100.0 * i, .shifted = i >= 2 && i <= 4});
    }
    const auto w = run_weights(frames, 100.0, 20.0, 0, 800);
    CHECK(w[0] == 0.0);
    CHECK(w[100] == 0.0);
    CHECK(w[150 - 30] == 0.0);  // before the run's left edge (150 - 20... edge at 150)
    CHECK(w[300] == 1.0);       // inside
    CHECK(w[449] > 0.0);        // inside the right fade (edge at 450, fade ends at 430)
    CHECK(w[600] == 0.0);
    CHECK(w[750] == 0.0);
    for (std::size_t n = 1; n < w.size(); ++n) {
        CHECK(w[n] >= 0.0);
        CHECK(w[n] <= 1.0);
    }
    // Monotone ramp up across the left edge (frames[2] at 200, edge at 150).
    for (std::size_t n = 151; n < 170; ++n) {
        CHECK(w[n] >= w[n - 1]);
    }
}

TEST_CASE("run_weights_have_no_edge_on_the_outer_side_of_the_first_and_last_frame",
          "[engine_support]") {
    std::vector<RunFrame> frames{{.position = 100.0, .shifted = true},
                                 {.position = 200.0, .shifted = true},
                                 {.position = 300.0, .shifted = true}};
    const auto w = run_weights(frames, 100.0, 20.0, 0, 400);
    for (const double v : w) {
        CHECK(v == 1.0);
    }
}
