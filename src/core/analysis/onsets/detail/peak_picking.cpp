#include "core/analysis/onsets/detail/peak_picking.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

std::size_t frames_of(Seconds duration, Seconds hop) {
    return static_cast<std::size_t>(std::max(1L, std::lround(duration.value() / hop.value())));
}

}  // namespace

std::vector<ActivationPeak> pick_peaks(const std::vector<double>& a, const PeakPickParams& p) {
    const std::size_t n = a.size();
    std::vector<ActivationPeak> peaks;
    if (n == 0) {
        return peaks;
    }
    std::vector<double> sum(n + 1, 0.0);
    std::vector<double> sum_sq(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        sum[i + 1] = sum[i] + a[i];
        sum_sq[i + 1] = sum_sq[i] + a[i] * a[i];
    }
    const std::size_t half_window = frames_of(p.threshold_window, p.hop) / 2;
    const std::size_t half_gap = std::max<std::size_t>(1, frames_of(p.min_gap, p.hop) / 2);
    const auto threshold_at = [&](std::size_t i) {
        const std::size_t lo = i > half_window ? i - half_window : 0;
        const std::size_t hi = std::min(n, i + half_window + 1);
        const auto count = static_cast<double>(hi - lo);
        const double mean = (sum[hi] - sum[lo]) / count;
        const double variance = std::max(0.0, (sum_sq[hi] - sum_sq[lo]) / count - mean * mean);
        return std::max(p.floor, mean + p.delta * std::sqrt(variance));
    };
    for (std::size_t i = 0; i < n; ++i) {
        const double threshold = threshold_at(i);
        if (a[i] < threshold) {
            continue;
        }
        const std::size_t lo = i > half_gap ? i - half_gap : 0;
        const std::size_t hi = std::min(n, i + half_gap + 1);
        bool is_max = true;
        for (std::size_t k = lo; k < hi && is_max; ++k) {
            is_max = a[k] < a[i] || (a[k] == a[i] && k >= i);  // ties: the first frame wins
        }
        if (is_max) {
            peaks.push_back({.frame = i, .strength = a[i] / threshold});
        }
    }
    return peaks;
}

}  // namespace anno
