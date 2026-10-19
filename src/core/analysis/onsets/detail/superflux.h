#pragma once

#include "core/analysis/onsets/onsets.h"
#include "core/types/result.h"

#include <span>
#include <vector>

// The SuperFlux onset activation (Bock & Widmer, DAFx 2013). Internal to core/analysis/onsets.

namespace anno {

struct Activation {
    std::vector<double> values;  // one per STFT frame; frame i is centred at i * hop
    Seconds hop;                 // the ACTUAL hop (whole samples / rate)
};

/// Errors: invalid_argument for bad parameters; an empty signal gives an empty activation.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<Activation> superflux_activation(std::span<const float> mono, Hz rate,
                                                      const OnsetParams& params);

}  // namespace anno
