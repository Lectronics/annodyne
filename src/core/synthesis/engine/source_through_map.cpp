#include "core/synthesis/engine/source_through_map.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_length_tolerance_s = 1e-9;
constexpr double k_shift_tolerance_samples = 1e-6;

/// The whole-sample shift of a slope-1 warp (output index = source index + shift), or an error.
Result<std::int64_t> warp_shift(const TimeSpan& span, Hz rate) {
    const Seconds source_len = span.source_end - span.source_start;
    const Seconds output_len = span.output_end - span.output_start;
    if (std::abs((output_len - source_len).value()) > k_length_tolerance_s) {
        return fail(ErrorCode::not_implemented,
                    "read_through_map: a warp with slope != 1 needs stretching (Phase 6)");
    }
    const Seconds shift = (span.output_start - OutputTime{}) - (span.source_start - SourceTime{});
    const double samples = shift.value() * rate.value();
    const double whole = std::round(samples);
    if (std::abs(samples - whole) > k_shift_tolerance_samples) {
        return fail(ErrorCode::not_implemented,
                    "read_through_map: a warp shifted by a fraction of a sample needs "
                    "resampling (not implemented)");
    }
    return static_cast<std::int64_t>(whole);
}

}  // namespace

Result<AudioBuffer> read_through_map(const AudioSource& source, const TimeMap& map, Samples begin,
                                     Samples count) {
    const Hz rate = source.sample_rate();
    if (begin.value() < 0 || count.value() < 0) {
        return fail(ErrorCode::invalid_argument, "read_through_map: negative range");
    }
    const double duration_s = to_seconds(source.frame_count(), rate).value();
    if (std::abs(map.source_length().value() - duration_s) * rate.value() > 0.5) {
        return fail(ErrorCode::invalid_argument,
                    "read_through_map: the map's source length does not match the recording");
    }
    const std::int64_t total = to_samples(map.output_length(), rate, Rounding::nearest).value();
    const std::int64_t first = begin.value();
    const std::int64_t last = first + count.value();
    if (last > total) {
        return fail(ErrorCode::out_of_range, "read_through_map: range lies outside the output");
    }
    auto out = AudioBuffer::create(source.channel_count(), count, rate);
    if (!out) {
        return Unexpected{out.error()};
    }
    for (const TimeSpan& span : map.spans()) {
        if (span.kind != SpanKind::warp) {
            continue;  // an insert stays zero; a cut has no output
        }
        const std::int64_t span_begin =
            to_sample_index(span.output_start, rate, Rounding::nearest).value();
        const std::int64_t span_end =
            to_sample_index(span.output_end, rate, Rounding::nearest).value();
        const std::int64_t lo = std::max(first, span_begin);
        const std::int64_t hi = std::min(last, span_end);
        if (hi <= lo) {
            continue;
        }
        const auto shift = warp_shift(span, rate);
        if (!shift) {
            return Unexpected{shift.error()};
        }
        auto samples = source.read(Samples{lo - *shift}, Samples{hi - lo});
        if (!samples) {
            return Unexpected{samples.error()};
        }
        for (int c = 0; c < out->channel_count(); ++c) {
            std::ranges::copy(samples->channel(c), out->channel(c).begin() + (lo - first));
        }
    }
    return out;
}

}  // namespace anno
