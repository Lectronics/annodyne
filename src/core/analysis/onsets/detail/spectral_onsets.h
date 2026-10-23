#pragma once

#include "core/analysis/onsets/detail/raw_onset.h"
#include "core/analysis/onsets/onsets.h"
#include "core/dsp/mono.h"
#include "core/types/result.h"

#include <vector>

// The spectral front end of detect_onsets: the onsets found in the signal itself (not the energy
// steps or the pitch changes), by the method in OnsetParams. Internal to core/analysis/onsets.

namespace anno {

/// `adaptive_threshold`: SuperFlux activation -> adaptive-threshold peaks -> energy refinement
/// (Phase 3, bit-identical). `windowed_peaks`: transient_onsets with `params.pipeline`. Every
/// result is `spectral`; `energy_rise` says whether the short-window energy really rose. Errors:
/// those of the method (invalid_argument for bad parameters). Pure; thread-safe; not real-time
/// safe.
[[nodiscard]] Result<std::vector<RawOnset>> spectral_onsets(const MonoAudio& mono,
                                                            const OnsetParams& params);

}  // namespace anno
