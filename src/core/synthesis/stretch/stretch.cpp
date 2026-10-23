#include "core/synthesis/stretch/stretch.h"

#include "core/synthesis/grain/signal.h"
#include "core/synthesis/stretch/detail/class_map.h"
#include "core/synthesis/stretch/detail/geometry.h"
#include "core/synthesis/stretch/detail/noise_stretch.h"
#include "core/synthesis/stretch/detail/voiced_stretch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

using stretch_detail::Run;
using stretch_detail::SpanGeometry;

constexpr double k_context_s = 0.1;  // source read beyond the span, for marks and grains

bool finite_positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

double raw(OutputTime t) {
    return (t - OutputTime{}).value();
}
double raw(SourceTime t) {
    return (t - SourceTime{}).value();
}

Result<SpanGeometry> make_geometry(const TimeSpan& span, double rate_hz,
                                   const StretchParams& params) {
    if (span.kind != SpanKind::warp) {
        return fail(ErrorCode::invalid_argument, "stretch_span: the span is not a warp");
    }
    const double source_length = raw(span.source_end) - raw(span.source_start);
    const double output_length = raw(span.output_end) - raw(span.output_start);
    if (!(source_length > 0.0) || !(output_length > 0.0)) {
        return fail(ErrorCode::invalid_argument, "stretch_span: the span has no length");
    }
    SpanGeometry g;
    g.rate_hz = rate_hz;
    g.slope = output_length / source_length;
    if (g.slope < params.min_slope.value() || g.slope > params.max_slope.value()) {
        return fail(ErrorCode::not_implemented, "stretch_span: slope " + std::to_string(g.slope) +
                                                    " is outside the supported range");
    }
    g.out_start = raw(span.output_start) * rate_hz;
    g.src_start = raw(span.source_start) * rate_hz;
    g.first = std::llround(g.out_start);
    g.last = std::llround(raw(span.output_end) * rate_hz);
    return g;
}

/// The requested range as output sample indices inside the span.
Result<std::pair<std::int64_t, std::int64_t>> range_samples(const SpanGeometry& g,
                                                            OutputRange range) {
    const std::int64_t begin = std::llround(raw(range.start()) * g.rate_hz);
    const std::int64_t end = std::llround(raw(range.end()) * g.rate_hz);
    if (begin < g.first || end > g.last || end < begin) {
        return fail(ErrorCode::invalid_argument, "stretch_span: the range lies outside the span");
    }
    return std::pair{begin, end};
}

/// The source around the span (plus context), as one channel with its first global sample.
struct SourceWindow {
    AudioBuffer audio;
    std::int64_t first = 0;
    [[nodiscard]] grain::Signal signal() const { return {audio.channel(0), first}; }
};

Result<SourceWindow> read_window(const AudioSource& src, const SpanGeometry& g) {
    const auto context = static_cast<std::int64_t>(std::ceil(k_context_s * g.rate_hz));
    const double span_src = static_cast<double>(g.last - g.first) / g.slope;
    const std::int64_t frames = src.frame_count().value();
    const std::int64_t from = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor(g.src_start)) - context, 0, frames);
    const std::int64_t to = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::ceil(g.src_start + span_src)) + context, from, frames);
    auto audio = src.read(Samples{from}, Samples{to - from});
    if (!audio) {
        return Unexpected{audio.error()};
    }
    return SourceWindow{.audio = std::move(*audio), .first = from};
}

/// Fills the stretches of 0 shorter than `max_samples` that have a 1 on both sides.
void bridge_short_gaps(std::vector<std::uint8_t>& indicator, std::int64_t max_samples) {
    const auto n = static_cast<std::int64_t>(indicator.size());
    for (std::int64_t i = 1; i < n;) {
        if (indicator[static_cast<std::size_t>(i)] != 0 ||
            indicator[static_cast<std::size_t>(i - 1)] == 0) {
            ++i;
            continue;
        }
        std::int64_t j = i;
        while (j < n && indicator[static_cast<std::size_t>(j)] == 0) {
            ++j;
        }
        if (j < n && j - i < max_samples) {
            std::fill(indicator.begin() + i, indicator.begin() + j, std::uint8_t{1});
        }
        i = j;
    }
}

/// Moving average of the indicator over `length` samples, edges replicated: 0..1 per sample.
std::vector<double> smoothed(const std::vector<std::uint8_t>& indicator, std::int64_t length) {
    const auto n = static_cast<std::int64_t>(indicator.size());
    std::vector<double> out(indicator.size(), 0.0);
    if (length <= 1) {
        std::ranges::transform(indicator, out.begin(),
                               [](std::uint8_t v) { return static_cast<double>(v); });
        return out;
    }
    const auto value = [&](std::int64_t i) {
        return static_cast<double>(
            indicator[static_cast<std::size_t>(std::clamp<std::int64_t>(i, 0, n - 1))]);
    };
    const std::int64_t half = length / 2;
    double sum = 0.0;
    for (std::int64_t i = -half; i < -half + length; ++i) {
        sum += value(i);
    }
    for (std::int64_t i = 0; i < n; ++i) {
        out[static_cast<std::size_t>(i)] = sum / static_cast<double>(length);
        sum += value(i - half + length) - value(i - half);
    }
    return out;
}

/// The vibrato clock of the true run (not the halo-padded interval), or nullopt: `retime`, or no
/// vibrato to preserve. A join is where the run touches the span's edge and the source frame just
/// outside is voiced too (slope-1 or another stretched span follows).
std::optional<stretch_detail::VibratoClock> vibrato_clock_of(const AnalysisResult& analysis,
                                                             const SpanGeometry& g,
                                                             const stretch_detail::Run& run,
                                                             const StretchParams& params) {
    if (params.vibrato != VibratoMode::preserve) {
        return std::nullopt;
    }
    const double from = g.source_at(static_cast<double>(run.begin));
    const double to = g.source_at(static_cast<double>(run.end));
    const bool join_begin =
        run.begin == g.first &&
        stretch_detail::voiced_at(analysis, g.source_at(static_cast<double>(g.first) - 1.0),
                                  g.rate_hz, params);
    const bool join_end =
        run.end == g.last &&
        stretch_detail::voiced_at(analysis, g.source_at(static_cast<double>(g.last)), g.rate_hz,
                                  params);
    return stretch_detail::VibratoClock::make(
        stretch_detail::vibrato_points(analysis, g.rate_hz, from, to), g, run, join_begin,
        join_end);
}

/// The voiced signal over the whole span (absolute index n at [n - first]); runs that cannot be
/// synthesised are cleared from the indicator, which then goes through the noise path.
std::vector<double> render_voiced(const grain::Signal& x, const AnalysisResult& analysis,
                                  const SpanGeometry& g, std::vector<std::uint8_t>& indicator,
                                  std::pair<std::int64_t, std::int64_t> wanted, std::int64_t halo,
                                  const StretchParams& params) {
    std::vector<double> voiced(indicator.size(), 0.0);
    for (const Run& run : stretch_detail::voiced_runs(indicator, g.first)) {
        const std::int64_t begin = std::max(g.first, run.begin - halo);
        const std::int64_t end = std::min(g.last, run.end + halo);
        if (end <= wanted.first || begin >= wanted.second) {
            continue;
        }
        const double from = g.source_at(static_cast<double>(begin));
        const double to = g.source_at(static_cast<double>(end));
        auto points = stretch_detail::pitch_points(analysis, g.rate_hz, from, to);
        std::optional<std::vector<double>> run_signal;
        if (!points.empty()) {
            const grain::PeriodCurve period{std::move(points), g.rate_hz};
            const auto clock = vibrato_clock_of(analysis, g, run, params);
            std::optional<grain::PeriodCurve> slow;
            if (clock) {
                auto slow_points = stretch_detail::pitch_points(analysis, g.rate_hz, from, to,
                                                                stretch_detail::PitchPart::slow);
                slow.emplace(std::move(slow_points), g.rate_hz);
            }
            const stretch_detail::OutputPitch output{.curve = clock ? &*slow : &period,
                                                     .geometry = &g,
                                                     .clock = clock ? &*clock : nullptr,
                                                     .rate_hz = g.rate_hz};
            run_signal = stretch_detail::synthesise_voiced_run(x, period, output, g, begin, end);
        }
        if (!run_signal) {
            std::fill(indicator.begin() + (run.begin - g.first),
                      indicator.begin() + (run.end - g.first), std::uint8_t{0});
            continue;
        }
        std::ranges::copy(*run_signal, voiced.begin() + (begin - g.first));
    }
    return voiced;
}

/// Equal-power gains (voiced, noise) for a voiced weight 0..1; exact at both ends.
std::pair<double, double> equal_power_gains(double w) {
    if (w >= 1.0) {
        return {1.0, 0.0};
    }
    if (w <= 0.0) {
        return {0.0, 1.0};
    }
    return {std::sin(0.5 * std::numbers::pi * w), std::cos(0.5 * std::numbers::pi * w)};
}

/// Equal-power mix of the voiced and noise signals by the smoothed indicator.
OutputAudio mix(const SpanGeometry& g, std::pair<std::int64_t, std::int64_t> wanted,
                const std::vector<double>& weight, const std::vector<double>& voiced,
                const std::vector<double>& noise, Hz rate, OutputTime start) {
    auto buffer = AudioBuffer::create(1, Samples{wanted.second - wanted.first}, rate).value();
    auto out = buffer.channel(0);
    for (std::int64_t n = wanted.first; n < wanted.second; ++n) {
        const auto i = static_cast<std::size_t>(n - g.first);
        const double w = weight[i];
        const auto [v_gain, n_gain] = equal_power_gains(w);
        const double v = v_gain > 0.0 ? v_gain * voiced[i] : 0.0;
        const double s =
            n_gain > 0.0 ? n_gain * noise[static_cast<std::size_t>(n - wanted.first)] : 0.0;
        out[static_cast<std::size_t>(n - wanted.first)] = static_cast<float>(v + s);
    }
    return OutputAudio{.audio = std::move(buffer), .start = start};
}

}  // namespace

Result<void> check_stretch_params(const StretchParams& p) {
    const auto bad = [](const char* what) -> Result<void> {
        return fail(ErrorCode::invalid_argument, std::string{"StretchParams: "} + what);
    };
    if (!std::isfinite(p.class_edge_fade.value()) || p.class_edge_fade.value() < 0.0) {
        return bad("class_edge_fade must be finite and >= 0");
    }
    if (p.vibrato != VibratoMode::preserve && p.vibrato != VibratoMode::retime) {
        return bad("vibrato must be preserve or retime");
    }
    if (!std::isfinite(p.voiced_bridge.value()) || p.voiced_bridge.value() < 0.0) {
        return bad("voiced_bridge must be finite and >= 0");
    }
    if (!finite_positive(p.noise_grain.value())) {
        return bad("noise_grain must be finite and > 0");
    }
    if (p.noise_jitter_hop_fraction < 0.0 || p.noise_jitter_hop_fraction > 0.5 ||
        std::isnan(p.noise_jitter_hop_fraction)) {
        return bad("noise_jitter_hop_fraction must be in 0..0.5");
    }
    if (!finite_positive(p.min_slope.value()) || !finite_positive(p.max_slope.value()) ||
        p.min_slope.value() > p.max_slope.value()) {
        return bad("0 < min_slope <= max_slope required");
    }
    return {};
}

Result<OutputAudio> stretch_span(const AudioSource& src, const AnalysisResult& analysis,
                                 const TimeSpan& span, OutputRange out_range,
                                 const StretchParams& params) {
    if (auto r = check_stretch_params(params); !r) {
        return Unexpected{r.error()};
    }
    if (src.channel_count() != 1) {
        return fail(ErrorCode::not_implemented, "stretch_span: mono sources only");
    }
    auto geometry = make_geometry(span, src.sample_rate().value(), params);
    if (!geometry) {
        return Unexpected{geometry.error()};
    }
    const SpanGeometry& g = *geometry;
    auto wanted = range_samples(g, out_range);
    if (!wanted) {
        return Unexpected{wanted.error()};
    }
    auto window = read_window(src, g);
    if (!window) {
        return Unexpected{window.error()};
    }
    auto indicator = stretch_detail::voiced_indicator(analysis, g, params);
    if (!indicator) {
        return Unexpected{indicator.error()};
    }
    bridge_short_gaps(*indicator, static_cast<std::int64_t>(std::llround(
                                      params.voiced_bridge.value() * g.rate_hz * g.slope)));
    const grain::Signal x = window->signal();
    const auto fade_samples =
        static_cast<std::int64_t>(std::llround(params.class_edge_fade.value() * g.rate_hz));
    const std::vector<double> voiced =
        render_voiced(x, analysis, g, *indicator, *wanted, fade_samples / 2 + 1, params);
    const std::vector<double> weight = smoothed(*indicator, fade_samples);
    const bool all_voiced =
        std::ranges::all_of(std::ranges::subrange(weight.begin() + (wanted->first - g.first),
                                                  weight.begin() + (wanted->second - g.first)),
                            [](double w) { return w >= 1.0; });
    const std::vector<double> noise =
        all_voiced
            ? std::vector<double>(static_cast<std::size_t>(wanted->second - wanted->first), 0.0)
            : stretch_detail::synthesise_noise(x, g, wanted->first, wanted->second, params);
    return mix(g, *wanted, weight, voiced, noise, src.sample_rate(), out_range.start());
}

}  // namespace anno
