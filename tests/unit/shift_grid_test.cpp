#include "core/timemap/build_time_map.h"
#include "core/timemap/layout.h"
#include "support/test_data.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

std::int64_t step_of(double rate_hz, double bpm, int division) {
    return shift_step_samples(Hz{rate_hz}, {.tempo_bpm = bpm, .note_division = division})
        .value()
        .value();
}

bool whole_multiple(double seconds, double rate_hz, std::int64_t step) {
    const double units = seconds * rate_hz / static_cast<double>(step);
    return std::abs(units - std::round(units)) < 1e-6;
}

}  // namespace

TEST_CASE("the_step_is_a_512th_note_normalised_to_whole_samples", "[shift_grid]") {
    // 120 bpm: a whole note is 2 s, a 512th 3.90625 ms = 172.27 samples at 44.1 kHz.
    CHECK(step_of(44100.0, 120.0, 512) == 172);
    CHECK(step_of(48000.0, 120.0, 512) == 188);  // 187.5 rounds half away from zero
    CHECK(step_of(96000.0, 120.0, 512) == 375);
    CHECK(step_of(44100.0, 120.0, 256) == 345);  // 344.53
    CHECK(step_of(44100.0, 60.0, 512) == 345);   // half the tempo, double the step
}

TEST_CASE("the_rounding_error_is_at_most_half_a_sample_and_the_step_is_at_least_one",
          "[shift_grid]") {
    for (const double rate : {8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0}) {
        for (const double bpm : {40.0, 60.0, 90.0, 120.0, 180.0, 240.0, 300.0}) {
            for (const int division : {256, 512}) {
                const std::int64_t step = step_of(rate, bpm, division);
                const double exact = 240.0 / bpm / division * rate;
                INFO(rate << " Hz " << bpm << " bpm 1/" << division);
                CHECK(step >= 1);
                const bool close = std::abs(static_cast<double>(step) - exact) <= 0.5 + 1e-9;
                CHECK((close || (step == 1 && exact < 1.0)));
            }
        }
    }
    CHECK(step_of(8000.0, 4000.0, 512) == 1);  // a tiny step never reaches zero
}

TEST_CASE("a_bad_tempo_rate_or_division_is_an_invalid_argument", "[shift_grid]") {
    CHECK(shift_step_samples(Hz{44100.0}, {.tempo_bpm = 0.0}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(shift_step_samples(Hz{44100.0}, {.tempo_bpm = std::nan("")}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(shift_step_samples(Hz{0.0}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(shift_step_samples(Hz{44100.0}, {.note_division = 0}).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("build_time_map_puts_every_anchor_shift_on_the_grid", "[shift_grid]") {
    std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)};
    const std::vector<EditSpec> group{
        make_spec("a", EditCategory::time, EditParam::onset_shift, EditOp::set, Seconds{0.2501}),
        make_spec("b", EditCategory::time, EditParam::onset_shift, EditOp::set, Seconds{-0.0713})};
    const EditList edits = EditList{TrackId{"t1"}}.add(group, k_t0).value();
    constexpr double rate = 44100.0;
    for (const std::int64_t step : {1, 172, 345}) {
        TimeMapParams params;
        params.source_end = src(6.0);
        params.sample_rate = Hz{rate};
        params.shift_step = Samples{step};
        const auto built = build_time_map(notes, edits, params).value();
        for (const TimeSpan& span : built.map.spans()) {
            const double shift = (span.output_start - OutputTime{}).value() -
                                 (span.source_start - SourceTime{}).value();
            CHECK(whole_multiple(shift, rate, step));
        }
    }
}

TEST_CASE("the_solver_cuts_and_stretches_in_whole_grid_steps", "[shift_grid]") {
    constexpr double rate = 44100.0;
    constexpr std::int64_t step = 172;
    const std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 2.2, 3.0)};
    const SegmentTimeline timeline = timeline_of(notes, 4.0);
    const std::vector<EditSpec> group{
        make_spec("a", EditCategory::time, EditParam::length_scale, EditOp::set, Ratio{2.0})};
    const EditList edits = EditList{TrackId{"t1"}}.add(group, k_t0).value();
    const LayoutMarkers markers = layout_markers(notes, edits, src(4.0)).value();
    TimeMapParams tm;
    tm.sample_rate = Hz{rate};
    tm.shift_step = Samples{step};
    const Layout layout = solve_layout(timeline, markers, {}, tm).value();
    REQUIRE(layout.intervals.back().gap);
    // room 0.15 s = 6615 samples floors to 38 steps of 172 = 6536 samples
    const double cut = layout.intervals.back().gap->length.value() * rate;
    CHECK(std::abs(cut - 6536.0) < 1e-6);
    for (const IntervalPlan& i : layout.intervals) {
        const double shift = (i.out_from - OutputTime{}).value() - (i.from - SourceTime{}).value();
        CHECK(whole_multiple(shift, rate, step));
    }
}
