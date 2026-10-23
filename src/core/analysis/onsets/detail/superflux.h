#pragma once

#include "core/analysis/onsets/onsets.h"
#include "core/analysis/onsets/stages/spectral_flux.h"
#include "core/types/result.h"

#include <span>

// The SuperFlux onset activation of detect_onsets: the stages P1 (band_spectrogram), P3
// (log_compress) and D (superflux) of the stages run with the OnsetParams. Internal to
// core/analysis/onsets.

namespace anno {

/// Errors: invalid_argument for bad parameters (the whole of OnsetParams is checked here, as in
/// Phase 3); an empty signal gives an empty activation. An even max_filter_bins keeps its Phase 3
/// meaning (radius (w - 1) / 2). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<Activation> superflux_activation(std::span<const float> mono, Hz rate,
                                                      const OnsetParams& params);

}  // namespace anno
