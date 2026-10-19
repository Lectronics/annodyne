#include "core/render/blend_plan.h"

#include "core/types/contract.h"

#include <algorithm>

namespace anno {

namespace {

std::vector<Ramp> nominal_ramps(std::span<const SampleSpan> spans, std::int64_t m) {
    std::vector<Ramp> ramps;
    const int count = static_cast<int>(spans.size());
    ramps.push_back({.begin = spans.front().begin - m,
                     .end = spans.front().begin,
                     .from = k_pristine,
                     .to = 0});
    for (int i = 0; i + 1 < count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const std::int64_t gap = spans[k + 1].begin - spans[k].end;
        if (gap <= m) {
            ramps.push_back(
                {.begin = spans[k + 1].begin - m, .end = spans[k].end + m, .from = i, .to = i + 1});
        } else {
            ramps.push_back(
                {.begin = spans[k].end, .end = spans[k].end + m, .from = i, .to = k_pristine});
            ramps.push_back({.begin = spans[k + 1].begin - m,
                             .end = spans[k + 1].begin,
                             .from = k_pristine,
                             .to = i + 1});
        }
    }
    ramps.push_back({.begin = spans.back().end,
                     .end = spans.back().end + m,
                     .from = count - 1,
                     .to = k_pristine});
    return ramps;
}

/// Left to right: where a ramp runs into the next one, they meet in the middle of the overlap.
void resolve_overlaps(std::vector<Ramp>& ramps) {
    for (std::size_t j = 0; j + 1 < ramps.size(); ++j) {
        Ramp& prev = ramps[j];
        Ramp& next = ramps[j + 1];
        if (prev.end > next.begin) {
            const std::int64_t meet =
                std::max(prev.begin, next.begin + (prev.end - next.begin) / 2);
            prev.end = meet;
            next.begin = meet;
        }
    }
}

}  // namespace

std::vector<Ramp> plan_blend(std::span<const SampleSpan> spans, std::int64_t margin_samples) {
    ANNO_EXPECTS(margin_samples >= 0);
    if (spans.empty()) {
        return {};
    }
    std::vector<Ramp> ramps = nominal_ramps(spans, margin_samples);
    resolve_overlaps(ramps);
    return ramps;
}

}  // namespace anno
