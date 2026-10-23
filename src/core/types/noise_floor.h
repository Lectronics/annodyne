#pragma once

#include "core/types/frame_grid.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// The recording's noise floor (design: docs/design/phase5-gaps-core.md). All levels are the RMS
// dBFS of the activity stage: 10 * log10(mean square), so a full-scale sine reads -3.01 dB.

namespace anno {

struct NoiseFloor {
    FrameGrid grid;                    // source axis
    std::vector<Decibels> curve_dbfs;  // the floor over time, one per grid frame
    std::vector<Hz> band_centre;       // band centres, ascending (log-spaced)
    std::vector<Decibels> band_dbfs;   // per-band mean power of the floor frames; not summable to
                                       // the curve; same size as band_centre
};

/// Errors: invalid_argument (bad grid; curve not grid.n_frames long; band arrays of different
/// sizes; band centres not finite, > 0 and strictly ascending; a non-finite level).
/// Pure; thread-safe.
[[nodiscard]] Result<void> validate(const NoiseFloor& floor);

}  // namespace anno
