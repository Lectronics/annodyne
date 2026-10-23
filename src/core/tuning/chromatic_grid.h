#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

// The chromatic (12-tone equal temperament) grid and the nearest grid pitch (ROADMAP Phase 4;
// ADR-0016). Key, scale and tuning-curve grids come with Correct Pitch in Phase 6.

namespace anno {

struct ChromaticGrid {
    Hz a4_reference = k_a4_standard_hz;  // the pitch the grid's A4 sits at; finite and > 0
};

/// The grid pitch nearest to `pitch` (absolute cents, A4 = 6900 at 440 Hz). The grid points are
/// to_cents(a4_reference * 2^(n/12)) for whole n, so a 432 Hz reference moves every point by
/// 1200 * log2(432 / 440) cents. A pitch exactly between two points rounds toward +infinity.
/// Errors: invalid_argument if `pitch` is not finite or a4_reference is not finite and > 0.
/// A pitch on the 440 Hz grid maps to itself exactly; for another reference the grid points are
/// computed through to_cents and agree with it to about 1e-9 cents. Pure; thread-safe; real-time
/// safe.
[[nodiscard]] Result<Cents> target_pitch(Cents pitch, const ChromaticGrid& grid);

}  // namespace anno
