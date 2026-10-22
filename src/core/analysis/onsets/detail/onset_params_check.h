#pragma once

#include "core/analysis/onsets/onsets.h"
#include "core/types/result.h"

// Validation of OnsetParams, in one place. Internal to core/analysis/onsets.

namespace anno {

/// The parameters detect_onsets needs for the method in `params.method`: always the energy-step and
/// merge fields (`min_gap`, `energy_step_*`); for `adaptive_threshold` also the Phase 3 chain's
/// (`hop`, `window`, `lag`, `threshold_window`, `refine_window`, `log_lambda`, `delta`,
/// `filterbank_bands_per_octave`, `max_filter_bins`). For `windowed_peaks` the pipeline's own
/// parameters are checked by its stages. Errors: invalid_argument. Pure; thread-safe.
[[nodiscard]] Result<void> check_onset_params(const OnsetParams& params);

/// The shared fields plus the Phase 3 chain's, whatever `method` says (what superflux_activation
/// has always required).
[[nodiscard]] Result<void> check_adaptive_chain_params(const OnsetParams& params);

}  // namespace anno
