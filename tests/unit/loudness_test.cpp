#include "core/analysis/loudness/loudness.h"
#include "core/dsp/k_weight.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 48000.0;

NoteSpan span_of(double start_s, double attack_end_s, double end_s) {
    NoteSpan s;
    s.id = NoteId{"n"};
    s.source_start = SourceTime{} + Seconds{start_s};
    s.attack_end = SourceTime{} + Seconds{attack_end_s};
    s.release_start = SourceTime{} + Seconds{end_s};
    s.source_end = SourceTime{} + Seconds{end_s};
    return s;
}

struct Signals {
    MonoAudio plain;
    MonoAudio weighted;
};

Signals signals_of(const std::vector<float>& x) {
    return {.plain = {.samples = x, .sample_rate = Hz{k_rate}},
            .weighted = {.samples = k_weight(x, Hz{k_rate}), .sample_rate = Hz{k_rate}}};
}

FrameGrid grid_for(const NoteSpan& s) {
    const double start = (s.source_start - SourceTime{}).value();
    return FrameGrid{
        .hop = Seconds{0.005},
        .start = SourceTime{} + Seconds{std::ceil(start / 0.005) * 0.005},
        .n_frames = static_cast<std::int64_t>(((s.source_end - s.source_start).value() / 0.005))};
}

}  // namespace

TEST_CASE("a_known_level_tone_measures_its_lufs_and_amp", "[loudness]") {
    const double a =
        std::pow(10.0, (-23.0 + 3.0103) / 20.0);  // reads -23 LUFS (mono, see k_weight)
    const auto s = signals_of(make_sine(1000.0, 2.0, k_rate, a));
    const NoteSpan span = span_of(0.5, 0.6, 1.5);
    const auto grid = grid_for(span);
    const auto r = measure_loudness(s.plain, s.weighted, span, grid, {}).value();
    CHECK(std::abs(r.loudness.value() - (-23.0)) < 0.1);
    REQUIRE(r.amp.size() == static_cast<std::size_t>(grid.n_frames));
    const double expect = 20.0 * std::log10(a / std::numbers::sqrt2);  // RMS of a sine in dBFS
    for (const Decibels v : r.amp) {
        REQUIRE(std::abs(v.value() - expect) < 0.1);
    }
}

TEST_CASE("notes_shorter_and_longer_than_400_ms_measure_alike", "[loudness]") {
    const auto s = signals_of(make_sine(1000.0, 3.0, k_rate, 0.1));
    const NoteSpan short_note = span_of(0.5, 0.52, 0.7);  // 180 ms from attack end
    const NoteSpan long_note = span_of(0.5, 0.52, 2.5);   // 1.98 s: several blocks
    const double a = measure_loudness(s.plain, s.weighted, short_note, grid_for(short_note), {})
                         .value()
                         .loudness.value();
    const double b = measure_loudness(s.plain, s.weighted, long_note, grid_for(long_note), {})
                         .value()
                         .loudness.value();
    CHECK(std::abs(a - b) < 0.15);  // no step at 400 ms
}

TEST_CASE("the_attack_is_excluded_from_the_loudness", "[loudness]") {
    std::vector<float> x(static_cast<std::size_t>(2.0 * k_rate), 0.0F);
    const auto loud = make_sine(1000.0, 0.1, k_rate, 0.5);
    const auto soft = make_sine(1000.0, 1.0, k_rate, 0.05);
    std::ranges::copy(loud, x.begin() + 24000);  // 0.5 .. 0.6 loud (the attack)
    std::ranges::copy(soft, x.begin() + 28800);  // 0.6 .. 1.6 soft
    const auto s = signals_of(x);
    const NoteSpan span = span_of(0.5, 0.6, 1.6);
    const double lufs =
        measure_loudness(s.plain, s.weighted, span, grid_for(span), {}).value().loudness.value();
    const double expect = -0.691 + 10.0 * std::log10(0.05 * 0.05 / 2.0) + 0.691;
    CHECK(std::abs(lufs - expect) < 0.3);
}

TEST_CASE("a_silent_note_reads_the_gate_and_the_amp_floor", "[loudness]") {
    const auto s = signals_of(std::vector<float>(static_cast<std::size_t>(2.0 * k_rate), 0.0F));
    const NoteSpan span = span_of(0.5, 0.6, 1.0);
    const auto r = measure_loudness(s.plain, s.weighted, span, grid_for(span), {}).value();
    CHECK(r.loudness.value() == -70.0);
    for (const Decibels v : r.amp) {
        REQUIRE(v.value() == -120.0);
    }
}

TEST_CASE("loudness_arguments_are_validated", "[loudness]") {
    const auto s = signals_of(make_sine(1000.0, 1.0, k_rate));
    const NoteSpan span = span_of(0.2, 0.3, 0.8);
    MonoAudio other_rate = s.weighted;
    other_rate.sample_rate = Hz{44100.0};
    CHECK(measure_loudness(s.plain, other_rate, span, grid_for(span), {}).error().code ==
          ErrorCode::invalid_argument);
    MonoAudio shorter = s.weighted;
    shorter.samples.pop_back();
    CHECK(measure_loudness(s.plain, shorter, span, grid_for(span), {}).error().code ==
          ErrorCode::invalid_argument);
    const NoteSpan beyond = span_of(0.5, 0.6, 2.0);  // the signal is 1 s long
    CHECK(measure_loudness(s.plain, s.weighted, beyond, grid_for(beyond), {}).error().code ==
          ErrorCode::invalid_argument);
    LoudnessParams bad;
    bad.amp_window = Seconds{0.0};
    CHECK(measure_loudness(s.plain, s.weighted, span, grid_for(span), bad).error().code ==
          ErrorCode::invalid_argument);
}
