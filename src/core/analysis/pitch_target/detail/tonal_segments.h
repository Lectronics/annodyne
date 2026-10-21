#pragma once

#include "core/analysis/pitch_target/pitch_target.h"

#include <cstddef>
#include <vector>

// Splitting a note's smoothed pitch curve into level and moving segments (d'Alessandro & Mertens
// 1995, tonal perception model; design note §3 steps 2, 4, 5). Internal to
// core/analysis/pitch_target.

namespace anno {

/// Frames [first, last] of the note's grid; pitches are the smoothed curve at the two ends.
struct Seg {
    std::size_t first = 0;
    std::size_t last = 0;
    double start_cents = 0.0;
    double end_cents = 0.0;
    bool moving = false;

    [[nodiscard]] std::size_t frames() const { return last - first + 1; }
};

/// Moving mean over `window_frames` of the frames whose weight is at least `min_weight` (others
/// take the mean of their neighbourhood too, so the curve stays defined everywhere).
[[nodiscard]] std::vector<double> smooth_curve(const std::vector<double>& curve,
                                               const std::vector<double>& weight, double min_weight,
                                               std::size_t window_frames);

/// Recursive turning-point split: a segment is cut where the curve departs from the straight line
/// between its ends by more than turn_threshold; neighbours whose slopes differ by less than
/// slope_merge, and adjacent flat segments within plateau_merge, are then joined. Covers all frames
/// of `curve` (non-empty), in order.
[[nodiscard]] std::vector<Seg> split_curve(const std::vector<double>& curve, Seconds hop,
                                           const SegmentingParams& params);

/// Sets `moving`. In the first or last transient_window of the note (segment middle inside it) a
/// change of at least turn_threshold is a transient and counts as moving whatever its rate;
/// elsewhere a segment is moving if |delta| >= min_glide_extent and |delta| > g * 100 / T (the
/// glissando threshold G = g / T^2 semitones/s for a change of T seconds, T capped at
/// glissando_max_duration), or if it is a fast step of at least step_min_extent.
void label_segments(std::vector<Seg>& segs, Seconds hop, std::size_t note_frames,
                    const SegmentingParams& params);

}  // namespace anno
