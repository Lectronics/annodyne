#include "core/synthesis/engine/engine_support.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace anno {

namespace {

constexpr double k_slope_tolerance = 1e-9;

double ramp(double u) {
    if (u <= 0.0) {
        return 0.0;
    }
    if (u >= 1.0) {
        return 1.0;
    }
    const double s = std::sin(0.5 * std::numbers::pi * u);
    return s * s;
}

}  // namespace

bool only_pitch_edited(const EffectiveNote& e) {
    return e.mod_scale == Ratio{1.0} && e.drift_scale == Ratio{1.0} &&
           e.transition_in_scale == Ratio{1.0} && e.transition_out_scale == Ratio{1.0} &&
           e.formant_offset == Cents{} && e.gain == Decibels{} &&
           e.within_note_flatten == Ratio{} && e.fade_in == Seconds{} && e.fade_out == Seconds{} &&
           e.noise_gain == NoiseGains{} && e.length_scale == Ratio{1.0} && e.handles.empty() &&
           e.attack_time_scale == Ratio{1.0};
}

bool only_timing_edited(const EffectiveNote& e, Cents note_center) {
    return std::abs((e.center - note_center).value()) <= k_neutral_snap_offset &&
           e.mod_scale == Ratio{1.0} && e.drift_scale == Ratio{1.0} &&
           e.transition_in_scale == Ratio{1.0} && e.transition_out_scale == Ratio{1.0} &&
           e.formant_offset == Cents{} && e.gain == Decibels{} &&
           e.within_note_flatten == Ratio{} && e.fade_in == Seconds{} && e.fade_out == Seconds{} &&
           e.noise_gain == NoiseGains{};
}

Result<std::int64_t> zone_shift(const TimeMap& map, Hz rate, OutputRange zone,
                                std::string_view engine) {
    for (const TimeSpan& span : map.spans()) {
        if (span.kind != SpanKind::warp || zone.start() < span.output_start ||
            zone.end() > span.output_end) {
            continue;
        }
        const Seconds out_len = span.output_end - span.output_start;
        const Seconds src_len = span.source_end - span.source_start;
        const double shift =
            ((span.output_start - OutputTime{}) - (span.source_start - SourceTime{})).value() *
            rate.value();
        if (std::abs((out_len - src_len).value()) > k_slope_tolerance ||
            std::abs(shift - std::round(shift)) > 1e-6) {
            break;
        }
        return static_cast<std::int64_t>(std::llround(shift));
    }
    return fail(ErrorCode::not_implemented,
                std::string{engine} +
                    ": the zone must lie inside one slope-1 warp of the time map");
}

std::string hex64(std::uint64_t v) {
    constexpr std::string_view k_digits = "0123456789abcdef";
    std::string out(16, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        out[15 - i] = k_digits[(v >> (4 * i)) & 0xFU];
    }
    return out;
}

std::vector<double> run_weights(std::span<const RunFrame> frames, double hop_samples,
                                double fade_samples, std::int64_t first, std::int64_t count) {
    std::vector<double> w(static_cast<std::size_t>(count), 0.0);
    const std::size_t last = frames.size() - 1;
    for (std::size_t i = 0; i <= last;) {
        if (!frames[i].shifted) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j + 1 <= last && frames[j + 1].shifted) {
            ++j;
        }
        const double left = i == 0 ? -std::numeric_limits<double>::infinity()
                                   : frames[i].position - 0.5 * hop_samples;
        const double right = j == last ? std::numeric_limits<double>::infinity()
                                       : frames[j].position + 0.5 * hop_samples;
        for (std::int64_t n = 0; n < count; ++n) {
            const auto pos = static_cast<double>(first + n);
            w[static_cast<std::size_t>(n)] = std::min(
                1.0, w[static_cast<std::size_t>(n)] +
                         ramp((pos - left) / fade_samples) * ramp((right - pos) / fade_samples));
        }
        i = j + 1;
    }
    return w;
}

}  // namespace anno
