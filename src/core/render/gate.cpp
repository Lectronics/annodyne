#include "core/render/gate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace anno {

namespace {

Unexpected invalid(const std::string& what) {
    return fail(ErrorCode::invalid_argument, "gate_curve: " + what);
}

double raw(OutputTime t) {
    return (t - OutputTime{}).value();
}

std::int64_t to_sample(OutputTime t, double rate) {
    return std::llround(raw(t) * rate);
}

/// Gain at offset `k` samples into the region [0, length): the plateau `g` with linear ramps of
/// `fade` samples (at most half the length when both edges ramp) toward 1 at ramped edges.
float gain_at(const GatedSpan& r, std::int64_t k, std::int64_t fade, double g) {
    const std::int64_t length = r.last - r.first;
    const std::int64_t ramp = std::min(fade, r.ramp_start && r.ramp_end ? length / 2 : length);
    if (ramp > 0 && r.ramp_start && k < ramp) {
        return static_cast<float>(1.0 +
                                  (g - 1.0) * static_cast<double>(k) / static_cast<double>(ramp));
    }
    const std::int64_t from_end = length - 1 - k;
    if (ramp > 0 && r.ramp_end && from_end < ramp) {
        return static_cast<float>(1.0 + (g - 1.0) * static_cast<double>(from_end) /
                                            static_cast<double>(ramp));
    }
    return static_cast<float>(g);
}

}  // namespace

Result<std::vector<GatedSpan>> gated_spans(const Gap& gap, const TimeMap& map, Hz rate) {
    if (!std::isfinite(rate.value()) || !(rate.value() > 0.0)) {
        return invalid("the sample rate must be finite and > 0");
    }
    std::vector<GatedSpan> out;
    const std::size_t n = gap.regions.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GapRegion& r = gap.regions[i];
        if (r.kind != GapRegionKind::silence) {
            continue;
        }
        const auto a = map.to_output(r.start);
        const auto b = map.to_output_end(r.end);
        if (!a || !b) {
            return invalid("a silence region of the gap starts or ends inside a cut");
        }
        // The gap's outer edges border a note, except at the recording's start and end.
        const bool note_before = i == 0 && gap.before.has_value();
        const bool note_after = i + 1 == n && gap.after.has_value();
        out.push_back({.first = to_sample(*a, rate.value()),
                       .last = to_sample(*b, rate.value()),
                       .ramp_start = i > 0 || note_before,
                       .ramp_end = i + 1 < n || note_after});
    }
    return out;
}

Result<std::vector<float>> gate_curve(const Gap& gap, const EffectiveGap& effective,
                                      const TimeMap& map, Hz rate, OutputRange range,
                                      const GateParams& params) {
    if (!std::isfinite(params.gate_fade.value()) || params.gate_fade.value() < 0.0) {
        return invalid("gate_fade must be finite and >= 0");
    }
    if (!std::isfinite(rate.value()) || !(rate.value() > 0.0)) {
        return invalid("the sample rate must be finite and > 0");
    }
    if (!(effective.gap == gap.id)) {
        return invalid("the effective gap is not this gap");
    }
    const double hz = rate.value();
    const std::int64_t first = to_sample(range.start(), hz);
    const std::int64_t last = to_sample(range.end(), hz);
    if (raw(range.start()) < 0.0 || raw(range.end()) > map.output_length().value() + 0.5 / hz) {
        return fail(ErrorCode::out_of_range, "gate_curve: the range lies outside the output");
    }
    std::vector<float> curve(static_cast<std::size_t>(std::max<std::int64_t>(0, last - first)),
                             1.0F);
    auto regions = gated_spans(gap, map, rate);
    if (!regions) {
        return Unexpected{regions.error()};
    }
    if (!effective.is_edited) {
        return curve;
    }
    const double g =
        effective.gain <= k_gap_mute_db ? 0.0 : std::pow(10.0, effective.gain.value() / 20.0);
    const auto fade = static_cast<std::int64_t>(std::llround(params.gate_fade.value() * hz));
    for (const GatedSpan& r : *regions) {
        for (std::int64_t s = std::max(first, r.first); s < std::min(last, r.last); ++s) {
            curve[static_cast<std::size_t>(s - first)] = gain_at(r, s - r.first, fade, g);
        }
    }
    return curve;
}

}  // namespace anno
