#pragma once

#include "core/analysis/onsets/stages/peak_pick.h"
#include "core/types/units.h"

#include <cstddef>
#include <vector>

// Adaptive-threshold peak picking of an onset activation. Internal to core/analysis/onsets.

namespace anno {

struct PeakPickParams {
    Seconds hop;
    Seconds threshold_window;  // centred window of the local mean and spread
    double delta = 2.0;        // threshold = mean + delta * spread
    double floor = 0.0;        // absolute minimum threshold
    Seconds min_gap;           // peaks closer than this: the stronger wins
};

/// A frame is a peak if its activation reaches the threshold and is the maximum within +-min_gap
/// / 2. Peaks are sorted by frame; strength = activation / threshold. Pure; thread-safe.
[[nodiscard]] std::vector<ActivationPeak> pick_peaks_adaptive(const std::vector<double>& activation,
                                                              const PeakPickParams& params);

}  // namespace anno
