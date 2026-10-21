#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// Zero-phase Butterworth filters (design: docs/design/phase3-activity.md §1).

namespace anno {

/// Zero-phase 2nd-order Butterworth high-pass: run forward then backward, so there is no group
/// delay and the magnitude response is squared (-6 dB at `cutoff`, 24.6 dB down one octave below
/// it). Samples are in the signal's own units; the time axis is unchanged. The edges use odd
/// reflection of 3 x (sample_rate / cutoff) samples, bounded by n - 1. Output has the input's
/// length. Errors: invalid_argument (sample_rate not finite and positive, cutoff not in
/// (0, sample_rate / 2)). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<std::vector<double>> zero_phase_highpass(std::span<const double> x,
                                                              Hz sample_rate, Hz cutoff);

/// The same for the low-pass (-6 dB at `cutoff`, 24.6 dB down one octave above it).
[[nodiscard]] Result<std::vector<double>> zero_phase_lowpass(std::span<const double> x,
                                                             Hz sample_rate, Hz cutoff);

}  // namespace anno
