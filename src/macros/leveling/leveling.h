#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <optional>
#include <vector>

// Leveling macro (features spec 8, ROADMAP Phase 6): evens out the level of the notes. Per note,
// gain_dB = (target - level) x amount, and optionally the level inside each note is pulled toward
// its own mean. Reads the analysis, never audio. Design: docs/design/phase6-leveling.md.

namespace anno {

struct LevelingParams {
    Ratio between_notes{0.5};  // 0..1: how far each note's loudness moves toward the target
    Ratio within_notes{0.0};   // 0..1: within_note_flatten for every note (0 emits none)
    std::optional<LoudnessUnits> target;  // the loudness the notes move toward; unset: the mean of
                                          // the active notes' loudness
    Decibels max_gain{12.0};              // |gain| is limited to this; > 0, finite
    Decibels min_gain{0.05};  // a correction smaller than this is not emitted; >= 0, finite
};

/// One `gain` edit (op set, origin "leveling") per active note with a finite loudness whose
/// correction (target - loudness) x between_notes, limited to +-max_gain, is at least min_gain in
/// size; with within_notes > 0 also one `within_note_flatten` (op set) per active note. In note
/// order; empty if nothing qualifies.
/// Errors: invalid_argument (an amount outside 0..1 or not finite, max_gain not > 0, min_gain
/// negative, a non-finite target). Pure; thread-safe; not real-time safe (allocates the specs).
[[nodiscard]] Result<std::vector<EditSpec>> leveling_edits(const AnalysisResult& analysis,
                                                           const LevelingParams& params);

}  // namespace anno
