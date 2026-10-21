#pragma once

#include "core/types/units.h"

#include <span>
#include <vector>

// Legato level steps: a sustained rise of the short-time level with no spectral onset (a singer
// changes note without a new attack). Internal to core/analysis/onsets.

namespace anno {

struct EnergyStep {
    SourceTime time;
    double rise_db = 0.0;  // mean level of the window after minus the window before
};

struct EnergyStepParams {
    Seconds window{0.060};  // each side of the step is averaged over this long
    Decibels min_rise{3.0};
    Decibels floor_below_peak{35.0};  // quieter than the loudest point by this much: ignored
};

/// Rises of the 20 ms RMS level (hop 5 ms): where the mean level of the `window` after a frame
/// exceeds the mean of the `window` before it by at least `min_rise`, at the local maximum of that
/// difference over +-window. Reported where the level crosses the midpoint of the two means.
/// Sorted by time. Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<EnergyStep> find_energy_steps(std::span<const float> mono, Hz rate,
                                                        const EnergyStepParams& params);

}  // namespace anno
