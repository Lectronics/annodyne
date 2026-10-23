#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

// The shift grid (ADR-0023 amendment 1, ADR-0025 decision 4): timing shifts are multiples of a
// small musical step so every shift is a whole number of samples and a gap absorbs a whole number
// of steps. The step is a 1/512 (or 1/256) note at the global tempo, normalised to whole samples.

namespace anno {

struct ShiftGridParams {
    double tempo_bpm = 120.0;  // the global tempo step; the fallback until Phase 8 reads a tempo
    int note_division = 512;   // the step is a whole note / note_division (256 or 512)
};

/// step_samples = max(1, round(step_seconds x sample_rate)) with step_seconds = (240 / tempo_bpm) /
/// note_division (a whole note is four beats). The rounding error is at most half a sample.
/// Errors: invalid_argument for a non-finite or non-positive tempo, rate or division.
/// Pure; thread-safe.
[[nodiscard]] Result<Samples> shift_step_samples(Hz sample_rate, const ShiftGridParams& grid);

}  // namespace anno
