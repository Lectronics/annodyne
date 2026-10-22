#include "core/analysis/onsets/stages/peak_pick.h"

#include "core/analysis/onsets/detail/stage_check.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

/// Window lengths in frames.
struct Windows {
    std::int64_t pre_max;
    std::int64_t post_max;
    std::int64_t pre_avg;
    std::int64_t post_avg;
};

/// The maximum over [i - pre_max, i + post_max] clamped to the signal, the FIRST frame winning a
/// tie: an equal value to the left disqualifies, an equal value to the right does not.
bool is_window_maximum(const std::vector<double>& a, std::int64_t i, const Windows& w) {
    const double v = a[static_cast<std::size_t>(i)];
    const auto n = static_cast<std::int64_t>(a.size());
    for (std::int64_t k = std::max<std::int64_t>(0, i - w.pre_max); k < i; ++k) {
        if (a[static_cast<std::size_t>(k)] >= v) {
            return false;
        }
    }
    for (std::int64_t k = i + 1; k <= std::min(n - 1, i + w.post_max); ++k) {
        if (a[static_cast<std::size_t>(k)] > v) {
            return false;
        }
    }
    return true;
}

/// The mean over [i - pre_avg, i + post_avg], frames outside the signal counting as 0 and the sum
/// divided by the full window width (as the reference does).
double window_mean(const std::vector<double>& a, std::int64_t i, const Windows& w) {
    const auto n = static_cast<std::int64_t>(a.size());
    double sum = 0.0;
    for (std::int64_t k = std::max<std::int64_t>(0, i - w.pre_avg);
         k <= std::min(n - 1, i + w.post_avg); ++k) {
        sum += a[static_cast<std::size_t>(k)];
    }
    return sum / static_cast<double>(w.pre_avg + w.post_avg + 1);
}

}  // namespace

Result<PeakList> pick_peaks_windowed(const Activation& activation,
                                     const WindowedPeakParams& params) {
    const double hop = activation.hop.value();
    if (!detail::positive(hop) || params.pre_max.value() < 0.0 || params.post_max.value() < 0.0 ||
        params.pre_avg.value() < 0.0 || params.post_avg.value() < 0.0 ||
        params.min_gap.value() < 0.0 || !std::isfinite(params.delta_activation) ||
        params.delta_activation < 0.0) {
        return detail::bad_stage_argument("pick_peaks_windowed: invalid parameters");
    }
    const auto count = [&](Seconds s) {
        return static_cast<std::int64_t>(std::llround(s.value() / hop));
    };
    const Windows w{.pre_max = count(params.pre_max),
                    .post_max = count(params.post_max),
                    .pre_avg = count(params.pre_avg),
                    .post_avg = count(params.post_avg)};
    const std::int64_t gap = count(params.min_gap);
    const auto& a = activation.values;
    PeakList out{.hop = activation.hop, .peaks = {}};
    std::int64_t last = -1;
    bool have_last = false;
    for (std::size_t index = 0; index < a.size(); ++index) {
        const auto i = static_cast<std::int64_t>(index);
        const double v = a[index];
        if (!(v > 0.0) || !is_window_maximum(a, i, w)) {
            continue;
        }
        const double threshold = window_mean(a, i, w) + params.delta_activation;
        if (v < threshold || (have_last && i - last <= gap)) {
            continue;
        }
        out.peaks.push_back(
            {.frame = static_cast<std::size_t>(i), .strength = v / std::max(threshold, 1e-9)});
        last = i;
        have_last = true;
    }
    return out;
}

}  // namespace anno
