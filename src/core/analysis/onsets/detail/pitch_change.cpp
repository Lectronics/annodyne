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
    const double band =
        threshold.value() /
        8.0;  // spread <= 2 x band; a vibrato turnaround stays within 20 cents for ~50 ms
    // A change is a stable left stretch ending at frame a and a stable right stretch starting at
    // frame b <= a + max_gap + 1 at least `threshold` away, whatever lies between (unvoiced
    // frames, or the blended ramp a tracker with a loose voicing threshold draws across a step).
    for (std::size_t a = side - 1; a + 1 < n; ++a) {
        const Side left = measure(f0, a + 1 - side, a + 1, band);
        if (!left.stable) {
            continue;
        }
        for (std::size_t b = a + 1; b + side <= n && b <= a + max_gap + 1; ++b) {
            const Side right = measure(f0, b, b + side, band);
            if (!right.stable) {
                continue;
            }
            const double step = std::abs(right.median - left.median);
            if (step < threshold.value()) {
                break;  // the pitch came back to the left note (or never left it): no change
            }
            // Tighten to the transition: last frame of the left note, first of the right note.
            std::size_t last_left = a;
            while (last_left + 1 < b && !std::isnan(f0[last_left + 1].value()) &&
                   std::abs(f0[last_left + 1].value() - left.median) <= band) {
                ++last_left;
            }
            std::size_t first_right = b;
            while (first_right > last_left + 1 && !std::isnan(f0[first_right - 1].value()) &&
                   std::abs(f0[first_right - 1].value() - right.median) <= band) {
                --first_right;
            }
            const SourceTime t0 = frame_time(pitch.grid, static_cast<std::int64_t>(last_left));
            const SourceTime t1 = frame_time(pitch.grid, static_cast<std::int64_t>(first_right));
            out.push_back({.time = t0 + (t1 - t0) * 0.5, .strength = step / threshold.value()});
            a = b;  // resume inside the right note
            break;
        }
    }
    return out;
}

}  // namespace anno
