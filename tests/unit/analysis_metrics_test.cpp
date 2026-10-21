#include "support/analysis_metrics.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

PitchTrack track_of(const std::vector<double>& cents) {
    PitchTrack t;
    t.grid = FrameGrid{.hop = Seconds{0.005},
                       .start = SourceTime{},
                       .n_frames = static_cast<std::int64_t>(cents.size())};
    for (const double c : cents) {
        t.f0.emplace_back(c);
    }
    return t;
}

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST_CASE("pitch_scores_count_correct_octave_and_false_alarm_frames", "[metrics]") {
    const std::vector<double> reference{6900, 6900, 6900, 6900, k_nan, k_nan};
    const auto est = track_of({6905, 6930, 8100, k_nan, 6900, k_nan});
    // frame 0: 5 cents (correct); 1: 30 cents (wrong at 10, right at 50); 2: +1200 (octave error);
    // 3: unvoiced while the reference is voiced (a miss); 4: voiced where the reference is not.
    const auto s10 = score_pitch(est, reference, 10.0, 0, 6);
    CHECK(s10.voiced_reference == 4);
    CHECK(s10.correct == 1);
    CHECK(s10.octave_errors == 1);
    CHECK(s10.false_alarms == 1);
    CHECK(s10.unvoiced_reference == 2);
    CHECK(s10.rpa() == 0.25);
    CHECK(s10.false_alarm_rate() == 0.5);
    CHECK(score_pitch(est, reference, 50.0, 0, 6).correct == 2);
    CHECK(max_abs_error_cents(est, reference, 0, 3) == 1200.0);
}

TEST_CASE("onset_scores_match_one_to_one_within_the_window", "[metrics]") {
    const auto perfect = score_onsets({1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}, 0.02);
    CHECK(perfect.f_measure() == 1.0);
    const auto s = score_onsets({1.01, 2.5, 3.0, 3.005}, {1.0, 2.0, 3.0}, 0.02);
    CHECK(s.matched == 2);  // 1.01 ~ 1.0 and 3.0 ~ 3.0; 3.005 has no free reference; 2.5 is too far
    CHECK(s.estimated == 4);
    CHECK(s.reference == 3);
    CHECK(s.precision() == 0.5);
    CHECK(std::abs(s.recall() - 2.0 / 3.0) < 1e-12);
    CHECK(std::abs(s.f_measure() - 2.0 * 0.5 * (2.0 / 3.0) / (0.5 + 2.0 / 3.0)) < 1e-12);
    CHECK(score_onsets({}, {}, 0.02).f_measure() == 1.0);
    CHECK(score_onsets({1.0}, {}, 0.02).precision() == 0.0);
    CHECK(score_onsets({}, {1.0}, 0.02).recall() == 0.0);
}
