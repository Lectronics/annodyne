#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <cstdint>
#include <vector>

// Scale grids for Correct Pitch (ROADMAP Phase 6, features spec 5d). Design:
// docs/design/phase6-correct-pitch.md.

namespace anno {

enum class ScaleType : std::uint8_t {
    chromatic,
    major,
    natural_minor,
    harmonic_minor,
    major_pentatonic,
    minor_pentatonic,
    dorian,
    mixolydian
};

/// Semitones above the root of each degree of `type`, ascending within 0..11 (chromatic: all 12).
/// Pure; thread-safe; real-time safe except that it allocates.
[[nodiscard]] std::vector<int> scale_degrees(ScaleType type);

/// A scale on a tuning reference. Points: every pitch class `(root + d) mod 12` for a degree d, in
/// every octave, on the equal-tempered grid of `a4_reference` (the same points as ChromaticGrid
/// has). `root` is a pitch class, C = 0, so A = 9.
struct ScaleGrid {
    Hz a4_reference = k_a4_standard_hz;                          // finite and > 0
    int root = 0;                                                // 0..11
    std::vector<int> degrees = scale_degrees(ScaleType::major);  // non-empty, ascending, 0..11
};

/// A grid of a named scale on `root` (a pitch class 0..11).
/// Errors: invalid_argument (root outside 0..11, a4_reference not finite and > 0).
[[nodiscard]] Result<ScaleGrid> make_scale_grid(ScaleType type, int root,
                                                Hz a4_reference = k_a4_standard_hz);

/// The scale pitch nearest to `pitch` (absolute cents, A4 = 6900 at 440 Hz); a pitch exactly
/// between two scale points rounds toward +infinity, as for the chromatic grid. A chromatic scale
/// agrees with target_pitch(pitch, ChromaticGrid{a4}).
/// Errors: invalid_argument (pitch not finite; a4_reference not finite and > 0; root outside
/// 0..11; degrees empty, not strictly ascending or outside 0..11). Pure; thread-safe; real-time
/// safe.
[[nodiscard]] Result<Cents> target_pitch(Cents pitch, const ScaleGrid& grid);

}  // namespace anno
