#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// Band-limited resampling (design: docs/design/phase3-dsp.md).

namespace anno {

struct ResampleParams {
    int taps_per_side =
        32;  // sinc zero crossings on each side at the narrower of the two rates; >= 8
};

/// Kaiser-windowed (beta 8.6) sinc resampling from `from` to `to`, cutoff 0.97 of the narrower
/// Nyquist, unity gain at DC. Output length round(n * to / from); output sample j reads the input
/// at position j * from / to (sample 0 is aligned). Samples outside the input are zero. Errors:
/// invalid_argument for non-finite or non-positive rates or taps_per_side < 8. Pure; thread-safe;
/// not real-time safe. Cost O(n_out * taps).
[[nodiscard]] Result<std::vector<float>> resample(std::span<const float> mono, Hz from, Hz to,
                                                  const ResampleParams& params);

}  // namespace anno
