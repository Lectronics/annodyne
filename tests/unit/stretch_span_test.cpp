#include "core/synthesis/stretch/stretch.h"
#include "support/engine_fixture.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using namespace anno;
using namespace anno::test;
using namespace anno::test::engine;

namespace {

OutputTime out_t(double s) {
    return OutputTime::unchecked_from_origin(Seconds{s});
}
SourceTime src_t(double s) {
    return SourceTime::unchecked_from_origin(Seconds{s});
}

/// One warp: source [s0, s1] played over [o0, o1].
TimeSpan warp(double s0, double s1, double o0, double o1) {
    return {.kind = SpanKind::warp,
            .source_start = src_t(s0),
            .source_end = src_t(s1),
            .output_start = out_t(o0),
            .output_end = out_t(o1),
            .segment = std::nullopt};
}

/// A stretch of [s0, s1] by `ratio`, starting at output time s0 (the map before it is slope 1).
TimeSpan stretched(double s0, double s1, double ratio) {
    return warp(s0, s1, s0, s0 + (s1 - s0) * ratio);
}

OutputRange whole(const TimeSpan& span) {
    return OutputRange::from_start(span.output_start, span.output_end - span.output_start);
}

std::vector<float> samples_of(const OutputAudio& a) {
    return {a.audio.channel(0).begin(), a.audio.channel(0).end()};
}

double db(double ratio) {
    return 20.0 * std::log10(ratio);
}

double rms_of(std::span<const float> x) {
    double sum = 0.0;
    for (const float v : x) {
        sum += static_cast<double>(v) * v;
    }
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, x.size())));
}

/// Fundamental from the autocorrelation peak between 100 and 500 Hz, parabolic refinement.
double autocorrelation_hz(const std::vector<float>& x, double rate_hz) {
    const auto lo = static_cast<std::size_t>(rate_hz / 500.0);
    const auto hi = static_cast<std::size_t>(rate_hz / 100.0);
    const auto corr = [&](std::size_t lag) {
        double dot = 0.0;
        for (std::size_t i = 0; i + lag < x.size(); ++i) {
            dot += static_cast<double>(x[i]) * x[i + lag];
        }
        return dot;
    };
    std::size_t best = lo;
    double best_value = -1e30;
    for (std::size_t lag = lo; lag <= hi; ++lag) {
        const double v = corr(lag);
        if (v > best_value) {
            best_value = v;
            best = lag;
        }
    }
    const double left = corr(best - 1);
    const double right = corr(best + 1);
    const double curve = left - 2.0 * best_value + right;
    const double offset = curve < 0.0 ? 0.5 * (left - right) / curve : 0.0;
    return rate_hz / (static_cast<double>(best) + offset);
}

/// A sustained 220 Hz harmonic tone over 0.2 .. 1.4 s (the engine fixtures' voice).
Fixture steady_voice() {
    return Fixture{voice([](double) { return 220.0; }, harmonic_amps(10, 0.5))};
}

}  // namespace

TEST_CASE("bad_stretch_params_are_invalid_arguments", "[stretch]") {
    CHECK(check_stretch_params({}).has_value());
    const auto code = [](StretchParams p) { return check_stretch_params(p).error().code; };
    StretchParams p;
    p.class_edge_fade = Seconds{-0.001};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.noise_grain = Seconds{0.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.noise_jitter_hop_fraction = 0.6;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.min_slope = Ratio{0.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.min_slope = Ratio{2.0};
    p.max_slope = Ratio{1.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.noise_grain = Seconds{std::nan("")};
    CHECK(code(p) == ErrorCode::invalid_argument);
}

TEST_CASE("stretch_span_returns_exactly_the_samples_of_the_range", "[stretch]") {
    const Fixture fx = steady_voice();
    const TimeSpan span = stretched(0.5, 0.9, 1.5);  // output 0.5 .. 1.1
    const auto out = stretch_span(fx.src, *fx.analysis, span, whole(span), {}).value();
    const auto begin = static_cast<std::int64_t>(std::llround(0.5 * k_rate));
    const auto end = static_cast<std::int64_t>(std::llround(1.1 * k_rate));
    CHECK(static_cast<std::int64_t>(out.audio.frame_count().value()) == end - begin);
    CHECK(out.audio.channel_count() == 1);
    CHECK(out.audio.sample_rate() == Hz{k_rate});
    CHECK(out.start == span.output_start);
}

TEST_CASE("stretch_span_is_deterministic_and_independent_of_the_range_asked_for", "[stretch]") {
    const Fixture fx = steady_voice();
    const TimeSpan span = stretched(0.5, 0.9, 1.5);
    const auto all = samples_of(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).value());
    const auto again =
        samples_of(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).value());
    CHECK(all == again);
    // a range in the middle of the span, not on a sample boundary of the span
    const OutputRange mid = OutputRange::from_start(out_t(0.7), Seconds{0.2});
    const auto part = samples_of(stretch_span(fx.src, *fx.analysis, span, mid, {}).value());
    const auto first = static_cast<std::size_t>(std::llround(0.7 * k_rate)) -
                       static_cast<std::size_t>(std::llround(0.5 * k_rate));
    REQUIRE(first + part.size() <= all.size());
    CHECK(std::memcmp(part.data(), all.data() + first, part.size() * sizeof(float)) == 0);
}

TEST_CASE("stretch_span_rejects_what_it_cannot_do", "[stretch]") {
    const Fixture fx = steady_voice();
    const auto code = [&](const TimeSpan& s, OutputRange r, const StretchParams& p = {}) {
        return stretch_span(fx.src, *fx.analysis, s, r, p).error().code;
    };
    const TimeSpan span = stretched(0.5, 0.9, 1.5);
    TimeSpan cut = span;
    cut.kind = SpanKind::cut;
    CHECK(code(cut, whole(span)) == ErrorCode::invalid_argument);
    CHECK(code(span, OutputRange::from_start(out_t(0.4), Seconds{0.3})) ==
          ErrorCode::invalid_argument);  // starts before the span
    CHECK(code(span, OutputRange::from_start(out_t(0.9), Seconds{0.5})) ==
          ErrorCode::invalid_argument);  // ends after the span
    CHECK(code(stretched(0.5, 0.9, 4.5), whole(stretched(0.5, 0.9, 4.5))) ==
          ErrorCode::not_implemented);
    CHECK(code(stretched(0.5, 0.9, 0.2), whole(stretched(0.5, 0.9, 0.2))) ==
          ErrorCode::not_implemented);
    StretchParams bad;
    bad.noise_grain = Seconds{-1.0};
    CHECK(code(span, whole(span), bad) == ErrorCode::invalid_argument);
}

TEST_CASE("the_slope_limits_are_inclusive", "[stretch]") {
    const Fixture fx = steady_voice();
    for (const double ratio : {0.25, 4.0}) {
        const TimeSpan span = stretched(0.5, 0.7, ratio);
        CHECK(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).has_value());
    }
}

TEST_CASE("stretch_span_is_not_implemented_for_stereo", "[stretch]") {
    auto stereo = AudioBuffer::create(2, Samples{44100}, Hz{44100.0}).value();
    const AudioSource src = AudioSource::from_buffer(std::move(stereo));
    const AnalysisResult analysis;
    const TimeSpan span = stretched(0.1, 0.3, 1.5);
    CHECK(stretch_span(src, analysis, span, whole(span), {}).error().code ==
          ErrorCode::not_implemented);
}

TEST_CASE("a_stretched_voice_keeps_its_pitch_and_level", "[stretch]") {
    const Fixture fx = steady_voice();
    for (const double ratio : {0.5, 0.7, 1.5, 2.0}) {
        INFO("ratio " << ratio);
        const TimeSpan span = stretched(0.5, 1.0, ratio);
        const auto out =
            samples_of(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).value());
        // the interior: skip 30 ms at each end
        const auto skip = static_cast<std::size_t>(0.03 * k_rate);
        REQUIRE(out.size() > 4 * skip);
        const std::span<const float> interior{out.data() + skip, out.size() - 2 * skip};
        const std::vector<float> copy(interior.begin(), interior.end());
        const double hz = autocorrelation_hz(copy, k_rate);
        CHECK(std::abs(1200.0 * std::log2(hz / 220.0)) < 10.0);
        const std::span<const float> original{fx.mono.data() +
                                                  static_cast<std::size_t>(0.53 * k_rate),
                                              static_cast<std::size_t>(0.4 * k_rate)};
        CHECK(std::abs(db(rms_of(interior) / rms_of(original))) < 1.0);
    }
}

TEST_CASE("a_stretch_joins_a_slope_1_copy_without_a_level_bump_or_a_step", "[stretch]") {
    const Fixture fx = steady_voice();
    const TimeSpan span = stretched(0.5, 0.9, 1.5);  // output 0.5 .. 1.1
    const auto body = samples_of(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).value());
    // the output: the unedited samples up to 0.5 s, the stretch, then the source after 0.9 s
    // shifted to follow it (the shift is not a whole sample, so take the nearest sample)
    std::vector<float> joined(fx.mono.begin(),
                              fx.mono.begin() + static_cast<std::ptrdiff_t>(0.5 * k_rate));
    joined.insert(joined.end(), body.begin(), body.end());
    const auto tail_from = static_cast<std::size_t>(std::llround(0.9 * k_rate));
    joined.insert(joined.end(), fx.mono.begin() + static_cast<std::ptrdiff_t>(tail_from),
                  fx.mono.begin() + static_cast<std::ptrdiff_t>(tail_from) +
                      static_cast<std::ptrdiff_t>(0.2 * k_rate));
    // the largest sample-to-sample step stays within 1.5x the source's own largest step
    double source_step = 0.0;
    for (std::size_t i = 1; i < fx.mono.size(); ++i) {
        source_step = std::max<double>(source_step, std::abs(fx.mono[i] - fx.mono[i - 1]));
    }
    for (const double at : {0.5, 1.1}) {
        const auto c = static_cast<std::size_t>(std::llround(at * k_rate));
        double step = 0.0;
        for (std::size_t i = c - 40; i < c + 40; ++i) {
            step = std::max<double>(step, std::abs(joined[i] - joined[i - 1]));
        }
        INFO("join at " << at << " s");
        CHECK(step < 1.5 * source_step);
        // level over 10 ms either side within 0.5 dB
        const auto n = static_cast<std::size_t>(0.01 * k_rate);
        const double before = rms_of({joined.data() + c - n, n});
        const double after = rms_of({joined.data() + c, n});
        CHECK(std::abs(db(after / before)) < 0.5);
    }
}

TEST_CASE("a_voiced_run_shorter_than_a_period_does_not_fail", "[stretch]") {
    const Fixture fx = steady_voice();
    const TimeSpan span =
        stretched(0.5, 0.5008, 2.0);  // under 4 periods of 220 Hz... and below one
    CHECK(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).has_value());
}

TEST_CASE("stretched_noise_keeps_its_level_and_is_not_periodic", "[stretch]") {
    const auto noise = make_noise(2.0, k_rate, 0.2, 7);
    const AudioSource src = AudioSource::from_buffer(to_buffer(noise, k_rate));
    const AnalysisResult analysis;  // no notes: everything goes through the noise path
    for (const double ratio : {0.5, 1.5, 2.0, 4.0}) {
        INFO("ratio " << ratio);
        const TimeSpan span = stretched(0.5, 1.0, ratio);
        const auto out = samples_of(stretch_span(src, analysis, span, whole(span), {}).value());
        const auto skip = static_cast<std::size_t>(0.03 * k_rate);
        const std::span<const float> mid{out.data() + skip, out.size() - 2 * skip};
        const std::span<const float> original{noise.data() + static_cast<std::size_t>(0.5 * k_rate),
                                              static_cast<std::size_t>(0.5 * k_rate)};
        CHECK(std::abs(db(rms_of(mid) / rms_of(original))) < 1.0);
        // autocorrelation at the grain hop (12 ms) and the grain (24 ms): no repetition
        for (const double lag_s : {0.012, 0.024}) {
            const auto lag = static_cast<std::size_t>(lag_s * k_rate);
            double dot = 0.0;
            double e = 0.0;
            for (std::size_t i = 0; i + lag < mid.size(); ++i) {
                dot += static_cast<double>(mid[i]) * mid[i + lag];
                e += static_cast<double>(mid[i]) * mid[i];
            }
            CHECK(std::abs(dot / e) < 0.3);
        }
        // short-time level (10 ms blocks) varies little: standard deviation of the dB level
        const auto block = static_cast<std::size_t>(0.01 * k_rate);
        std::vector<double> levels;
        for (std::size_t i = 0; i + block <= mid.size(); i += block) {
            levels.push_back(db(rms_of(mid.subspan(i, block)) / 0.2));
        }
        const double mean =
            std::accumulate(levels.begin(), levels.end(), 0.0) / static_cast<double>(levels.size());
        double var = 0.0;
        for (const double l : levels) {
            var += (l - mean) * (l - mean);
        }
        CHECK(std::sqrt(var / static_cast<double>(levels.size())) < 1.0);
    }
}

TEST_CASE("digital_silence_stays_exactly_zero", "[stretch]") {
    const std::vector<float> silence(static_cast<std::size_t>(2.0 * k_rate), 0.0F);
    const AudioSource src = AudioSource::from_buffer(to_buffer(silence, k_rate));
    const AnalysisResult analysis;
    const TimeSpan span = stretched(0.5, 1.0, 1.7);
    const auto out = samples_of(stretch_span(src, analysis, span, whole(span), {}).value());
    CHECK(std::ranges::all_of(out, [](float v) { return v == 0.0F && std::isfinite(v); }));
}

TEST_CASE("a_noise_burst_before_a_vowel_does_not_enter_the_voiced_path", "[stretch]") {
    // 0.3 s of noise, then the 220 Hz voice: the stretched noise part keeps no pitch
    auto x = voice([](double) { return 220.0; }, harmonic_amps(10, 0.5));
    const auto noise = make_noise(0.2, k_rate, 0.3, 11);
    for (std::size_t i = 0; i < noise.size(); ++i) {
        x[static_cast<std::size_t>(0.2 * k_rate) + i] = noise[i];
    }
    const Fixture fx{x};
    const TimeSpan span = stretched(0.25, 0.35, 2.0);
    const auto out = samples_of(stretch_span(fx.src, *fx.analysis, span, whole(span), {}).value());
    const auto lag = static_cast<std::size_t>(k_rate / 220.0);
    double dot = 0.0;
    double e = 0.0;
    for (std::size_t i = 0; i + lag < out.size(); ++i) {
        dot += static_cast<double>(out[i]) * out[i + lag];
        e += static_cast<double>(out[i]) * out[i];
    }
    CHECK(std::abs(dot / e) < 0.4);
}

namespace {

/// 0.2 .. 1.4 s of a harmonic voice with a sinusoidal vibrato of `rate_hz` and `depth_cents`.
Fixture vibrato_voice(double rate_hz, double depth_cents) {
    return Fixture{voice(
        [=](double t) {
            return 220.0 * std::pow(2.0, depth_cents * std::sin(2.0 * M_PI * rate_hz * t) / 1200.0);
        },
        harmonic_amps(10, 0.5))};
}

/// Peak of the DFT of the analysed fast pitch component (cents) of the longest note of `out`,
/// 3..9 Hz in 0.05 Hz steps: {rate_hz, depth_cents}; {0, 0} if there is no note.
std::pair<double, double> vibrato_of(const OutputAudio& out) {
    const std::vector<float> mono = samples_of(out);
    const auto src = AudioSource::from_buffer(to_buffer(mono, k_rate));
    const auto analysis = analyse(src, engine_analysis_params()).value();
    if (analysis->notes.empty()) {
        return {0.0, 0.0};
    }
    const Note& n = *std::max_element(
        analysis->notes.begin(), analysis->notes.end(),
        [](const Note& a, const Note& b) { return a.grid.n_frames < b.grid.n_frames; });
    const double hop = n.grid.hop.value();
    std::pair<double, double> best{0.0, 0.0};
    for (int k = 60; k <= 180; ++k) {
        const double f = k * 0.05;
        double re = 0.0;
        double im = 0.0;
        for (std::size_t i = 0; i < n.pitch_mod.size(); ++i) {
            const double v = n.pitch_mod[i].value();
            re += v * std::cos(2.0 * M_PI * f * static_cast<double>(i) * hop);
            im += v * std::sin(2.0 * M_PI * f * static_cast<double>(i) * hop);
        }
        const double amp = 2.0 * std::hypot(re, im) / static_cast<double>(n.pitch_mod.size());
        if (amp > best.second) {
            best = {f, amp};
        }
    }
    return best;
}

OutputAudio stretch_whole_note(const Fixture& fx, double ratio, VibratoMode mode) {
    StretchParams params;
    params.vibrato = mode;
    const TimeSpan span = stretched(0.2, 1.4, ratio);
    return stretch_span(fx.src, *fx.analysis, span, whole(span), params).value();
}

}  // namespace

TEST_CASE("a_stretched_note_keeps_its_vibrato_rate_within_half_a_hertz", "[stretch][vibrato]") {
    const Fixture fx = vibrato_voice(5.5, 40.0);
    const auto source =
        vibrato_of(OutputAudio{.audio = to_buffer(fx.mono, k_rate), .start = out_t(0.0)});
    REQUIRE(std::abs(source.first - 5.5) < 0.3);
    for (const double ratio : {0.6, 1.4, 2.0}) {
        const auto kept = vibrato_of(stretch_whole_note(fx, ratio, VibratoMode::preserve));
        INFO("ratio " << ratio << " rate " << kept.first << " depth " << kept.second);
        CHECK(std::abs(kept.first - source.first) <= 0.5);
        CHECK(kept.second > 0.7 * source.second);
        CHECK(kept.second < 1.3 * source.second);
    }
    // the old behaviour moves the rate with the slope (the test discriminates)
    const auto retimed = vibrato_of(stretch_whole_note(fx, 2.0, VibratoMode::retime));
    CHECK(std::abs(retimed.first - source.first) > 1.0);
}

TEST_CASE("a_note_without_vibrato_renders_the_same_in_both_modes", "[stretch][vibrato]") {
    const Fixture fx = steady_voice();
    for (const double ratio : {0.7, 1.6}) {
        const auto a = samples_of(stretch_whole_note(fx, ratio, VibratoMode::preserve));
        const auto b = samples_of(stretch_whole_note(fx, ratio, VibratoMode::retime));
        REQUIRE(a.size() == b.size());
        CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    }
}

TEST_CASE("vibrato_preserving_stretch_is_deterministic_and_range_independent",
          "[stretch][vibrato]") {
    const Fixture fx = vibrato_voice(5.5, 40.0);
    const TimeSpan span = stretched(0.2, 1.4, 1.5);
    StretchParams params;
    const auto whole_run = stretch_span(fx.src, *fx.analysis, span, whole(span), params).value();
    const auto again = stretch_span(fx.src, *fx.analysis, span, whole(span), params).value();
    const auto a = samples_of(whole_run);
    const auto b = samples_of(again);
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    // a sub-range equals the same samples of the whole render
    const auto part = stretch_span(fx.src, *fx.analysis, span,
                                   OutputRange::from_start(out_t(0.9), Seconds{0.3}), params)
                          .value();
    const auto p = samples_of(part);
    const auto offset = static_cast<std::size_t>(std::llround((0.9 - 0.2) * k_rate));
    CHECK(std::memcmp(p.data(), a.data() + offset, p.size() * sizeof(float)) == 0);
}

TEST_CASE("an_unsupported_vibrato_mode_is_an_invalid_argument", "[stretch][vibrato]") {
    StretchParams p;
    p.vibrato = static_cast<VibratoMode>(7);
    CHECK(check_stretch_params(p).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("the_noise_path_joins_the_source_exactly_at_both_span_edges", "[stretch]") {
    // Tonal material sent through the noise path (no voiced classes): a stretched span must start
    // and end on the source's own samples, or it steps against its slope-1 neighbours.
    const Fixture fx = steady_voice();
    StretchParams params;
    params.voiced_classes = FrameClassSet{};
    const TimeSpan span = stretched(0.5, 0.9, 1.5);
    const auto out =
        samples_of(stretch_span(fx.src, *fx.analysis, span, whole(span), params).value());
    const auto first_out = static_cast<std::int64_t>(std::llround(0.5 * k_rate));
    const auto last_out = static_cast<std::int64_t>(std::llround(1.1 * k_rate));
    REQUIRE(static_cast<std::int64_t>(out.size()) == last_out - first_out);
    const auto src_first = static_cast<std::size_t>(std::llround(0.5 * k_rate));
    const double src_end = 0.5 * k_rate + (static_cast<double>(last_out) - 0.5 * k_rate) / 1.5;
    const auto src_last = static_cast<std::size_t>(std::llround(src_end));
    CHECK(out.front() == fx.mono[src_first]);  // the edge samples are the source's own
    CHECK(out.back() == fx.mono[src_last - 1]);
    for (std::size_t i = 1; i < 8; ++i) {  // and the blend leaves no step next to them
        CHECK(std::abs(out[i] - fx.mono[src_first + i]) < 0.02f);
        CHECK(std::abs(out[out.size() - 1 - i] - fx.mono[src_last - 1 - i]) < 0.02f);
    }
}

TEST_CASE("a_one_frame_voicing_dropout_does_not_switch_the_renderer_to_the_noise_path",
          "[stretch]") {
    const Fixture fx = steady_voice();
    auto edited = std::make_shared<AnalysisResult>(*fx.analysis);
    Note& note = *std::max_element(
        edited->notes.begin(), edited->notes.end(),
        [](const Note& a, const Note& b) { return a.grid.n_frames < b.grid.n_frames; });
    REQUIRE(note.noise.frame_class.size() == static_cast<std::size_t>(note.grid.n_frames));
    note.noise.frame_class[note.noise.frame_class.size() / 2] = FrameClass::breath;
    const TimeSpan span = stretched(0.3, 1.3, 0.8);
    const auto render = [&](double bridge_s) {
        StretchParams params;
        params.voiced_bridge = Seconds{bridge_s};
        return samples_of(stretch_span(fx.src, *edited, span, whole(span), params).value());
    };
    // third-difference outlier against the local level: the dropout steps the unbridged render
    const auto worst_tick = [](const std::vector<float>& x) {
        double worst = 0.0;
        const double level = rms_of(x);
        for (std::size_t i = 3; i < x.size(); ++i) {
            const double d3 = static_cast<double>(x[i]) - 3.0 * static_cast<double>(x[i - 1]) +
                              3.0 * static_cast<double>(x[i - 2]) - static_cast<double>(x[i - 3]);
            worst = std::max(worst, std::abs(d3));
        }
        return worst / level;
    };
    const auto bridged = render(0.015);
    const auto unbridged = render(0.0);
    CHECK(bridged != unbridged);
    CHECK(worst_tick(bridged) < worst_tick(unbridged));
}

TEST_CASE("a_negative_voiced_bridge_is_an_invalid_argument", "[stretch]") {
    StretchParams p;
    p.voiced_bridge = Seconds{-0.001};
    CHECK(check_stretch_params(p).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("a_fast_pitch_glide_inside_a_vibrato_note_stays_with_the_audio", "[stretch][vibrato]") {
    // 5.5 Hz, 40 cent vibrato plus a 150 cent rise over 25 ms at 0.8 s: only the sinusoid is
    // regenerated; the glide is not vibrato and must survive the stretch (at 1.1 s, the time it
    // plays at after 1.5x from 0.2 s).
    const Fixture fx{voice(
        [](double t) {
            const double glide = 150.0 * std::clamp((t - 0.8) / 0.025, 0.0, 1.0);
            return 220.0 * std::pow(2.0, (40.0 * std::sin(2.0 * M_PI * 5.5 * t) + glide) / 1200.0);
        },
        harmonic_amps(10, 0.5))};
    const auto rise_of = [&](VibratoMode mode) {
        const auto out = stretch_whole_note(fx, 1.5, mode);
        const std::vector<float> mono = samples_of(out);
        const auto src = AudioSource::from_buffer(to_buffer(mono, k_rate));
        const auto analysis = analyse(src, engine_analysis_params()).value();
        REQUIRE_FALSE(analysis->notes.empty());
        const Note& n = *std::max_element(
            analysis->notes.begin(), analysis->notes.end(),
            [](const Note& a, const Note& b) { return a.grid.n_frames < b.grid.n_frames; });
        // absolute pitch, frame by frame; the largest rise within 30 ms around the glide's time
        const double hop = n.grid.hop.value();
        const double t0 = (out.start - OutputTime{}).value();
        double rise = 0.0;
        for (std::size_t i = 0; i + 6 < n.pitch_mod.size(); ++i) {
            const double t = (n.grid.start - SourceTime{}).value() + static_cast<double>(i) * hop;
            if (std::abs((t + t0) - 1.1) > 0.06) {
                continue;
            }
            const auto cents = [&](std::size_t k) {
                return n.pitch_drift[k].value() + n.pitch_mod[k].value();
            };
            rise = std::max(rise, cents(i + 6) - cents(i));
        }
        return rise;
    };
    const double kept = rise_of(VibratoMode::preserve);
    const double retimed = rise_of(VibratoMode::retime);
    INFO("preserve " << kept << " retime " << retimed);
    CHECK(kept > 0.8 * retimed);
}
