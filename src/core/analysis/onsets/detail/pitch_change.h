#pragma once

#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// Legato note changes from the f0 curve. Internal to core/analysis/onsets.

namespace anno {

struct PitchChange {
    SourceTime time;
    double strength = 0.0;  // step / threshold, >= 1
};

/// A step of at least `threshold` cents between two voiced frames a < b with only unvoiced frames
/// (at most 60 ms: the f0 estimate blends across a step) between them, where the `stable` seconds
/// up to a and the `stable` seconds from b are each voiced and within a quarter of the threshold of
/// their median, and the medians differ by at least the threshold. A smooth vibrato changes a few
/// cents per frame, so it never qualifies. Reported at the middle of the gap; strength = step /
/// threshold. Sorted by time. Pure; thread-safe.
[[nodiscard]] std::vector<PitchChange> find_pitch_changes(const PitchTrack& pitch, Cents threshold,
                                                          Seconds stable);

}  // namespace anno
