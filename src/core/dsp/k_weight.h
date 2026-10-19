#pragma once

#include "core/types/units.h"

#include <span>
#include <vector>

// ITU-R BS.1770 K-weighting (design: docs/design/phase3-dsp.md).

namespace anno {

/// K-weighting: a high-shelf (+4 dB, 1681.97 Hz) then a 38.14 Hz high-pass, as two biquads whose
/// coefficients are derived analytically for any sample rate (De Man, "Evaluation of
/// implementations of the ITU-R BS.1770 loudness measure"; ITU-R BS.1770-4 Annex 1 at 48 kHz). Zero
/// initial state; output has the input's length. Pre: sample_rate finite and >= 4 kHz (the 1.68 kHz
/// shelf needs its centre below the Nyquist frequency; below about 3.4 kHz the analytic design
/// breaks down) (asserted in debug). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<float> k_weight(std::span<const float> mono, Hz sample_rate);

}  // namespace anno
