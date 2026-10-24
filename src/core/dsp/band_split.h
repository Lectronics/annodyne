#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// Time-varying band split (design: docs/design/phase6-voiced-frequency.md, ADR-0033).

namespace anno {

struct BandSplit {
    std::vector<float> low;   // below the cut-off
    std::vector<float> high;  // x - low, so low + high == x to float rounding
};

/// Splits `x` at a time-varying cut-off. low is an STFT low-pass (2048-point periodic Hann, hop
/// 512, weighted overlap-add) whose mask falls from 1 to 0 as a raised cosine in frequency over
/// `transition` centred on the cut-off; the mask of a frame uses the cut-off at the frame's centre
/// sample. high = x - low, exactly: the masks are amplitude-complementary (M_lo + M_hi = 1), not
/// power-complementary. A cut-off of 0 gives low = 0 and high = x.
/// `cutoff_hz` has one value per sample of `x`: a raw-double span because it is a per-sample kernel
/// buffer (the unit is in the name); the caller interpolates a frame curve. Units: Hz; time axis:
/// whichever `x` is on.
/// Errors: invalid_argument for an empty `x`, a non-finite or non-positive rate or transition, a
/// cutoff span of another length, or a negative or non-finite cut-off.
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<BandSplit> split_band(std::span<const float> x, Hz sample_rate,
                                           std::span<const double> cutoff_hz, Hz transition);

}  // namespace anno
