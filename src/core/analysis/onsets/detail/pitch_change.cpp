#include "core/analysis/onsets/detail/pitch_change.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

// The f0 estimate blends across a step and is often unvoiced for a few frames there; a change
// counts if the unvoiced gap between the two stable sides is at most this long.
constexpr double k_max_gap_s = 0.060;

struct Side {
    bool stable = false;
    double median = 0.0;
};

/// The median of f0[first, last) if all voiced and within `band` cents of it.
Side measure(const std::vector<Cents>& f0, std::size_t first, std::size_t last, double band) {
    std::vector<double> values;
    values.reserve(last - first);
    for (std::size_t i = first; i < last; ++i) {
        if (std::isnan(f0[i].value())) {
            return {};
        }
        values.push_back(f0[i].value());
    }
    std::ranges::sort(values);
    const double median = values[values.size() / 2];
    const bool stable =
        std::ranges::all_of(values, [&](double v) { return std::abs(v - median) <= band; });
    return {.stable = stable, .median = median};
}

/// The next voiced frame after `a` with only unvoiced frames in between, within `max_gap` frames;
/// or n.
std::size_t next_voiced(const std::vector<Cents>& f0, std::size_t a, std::size_t max_gap) {
    for (std::size_t b = a + 1; b < f0.size() && b - a <= max_gap; ++b) {
        if (!std::isnan(f0[b].value())) {
            return b;
        }
    }
    return f0.size();
}

}  // namespace

std::vector<PitchChange> find_pitch_changes(const PitchTrack& pitch, Cents threshold,
                                            Seconds stable) {
    std::vector<PitchChange> out;
    const std::vector<Cents>& f0 = pitch.f0;
    const auto n = static_cast<std::size_t>(pitch.grid.n_frames);
    const auto side = static_cast<std::size_t>(
        std::max(2L, std::lround(stable.value() / pitch.grid.hop.value())));
    const auto max_gap =
        static_cast<std::size_t>(std::max(1L, std::lround(k_max_gap_s / pitch.grid.hop.value())));
    const double band = threshold.value() / 4.0;
    // Pairs (a, b) of voiced frames with only unvoiced frames between them, where f0 jumps.
    for (std::size_t a = side - 1; a + 1 < n; ++a) {
        if (std::isnan(f0[a].value())) {
            continue;
        }
        const std::size_t b = next_voiced(f0, a, max_gap);
        if (b >= n || b + side > n || std::abs(f0[b].value() - f0[a].value()) < threshold.value()) {
            continue;
        }
        const Side left = measure(f0, a + 1 - side, a + 1, band);
        const Side right = measure(f0, b, b + side, band);
        const double step = std::abs(right.median - left.median);
        if (left.stable && right.stable && step >= threshold.value()) {
            const SourceTime t0 = frame_time(pitch.grid, static_cast<std::int64_t>(a));
            const SourceTime t1 = frame_time(pitch.grid, static_cast<std::int64_t>(b));
            out.push_back({.time = t0 + (t1 - t0) * 0.5, .strength = step / threshold.value()});
        }
    }
    return out;
}

}  // namespace anno
