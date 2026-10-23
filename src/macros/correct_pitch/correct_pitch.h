#pragma once

#include "core/tuning/scale_grid.h"
#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Correct Pitch macro (features spec 5d, ROADMAP Phase 6): pulls each note's pitch target toward
// the nearest pitch of a scale. Reads the analysis, never audio. Design:
// docs/design/phase6-correct-pitch.md.

namespace anno {

struct CorrectPitchParams {
    ScaleGrid grid;  // the scale the notes are pulled toward
    Ratio center_amount{
        1.0};                 // 0..1: the share of the distance to the grid pitch that is corrected
    Ratio drift_amount{0.0};  // 0..1: how much of the slow drift is straightened (drift_scale =
                              // 1 - amount). No engine realises drift_scale yet; 0 emits nothing.
    Cents min_deviation{0.0};  // a note already closer than this to its grid pitch is left alone;
                               // >= 0, finite
};

/// One `center_offset` edit (op set, origin "correct_pitch") per active note with a single pitch
/// target whose distance to the nearest grid pitch is at least min_deviation (and not zero):
/// offset = (grid pitch - target) x center_amount, so the note's centre moves that share of the
/// way and vibrato and scoops ride on it unchanged. With drift_amount > 0 one `drift_scale` edit
/// (op set, 1 - drift_amount) per such note as well, whatever its deviation. Notes of kind multi or
/// unpitched are skipped (they have no single target). Result in note order; empty if nothing
/// qualifies.
/// Errors: invalid_argument (a bad grid; an amount outside 0..1 or not finite; min_deviation
/// negative or not finite); out_of_range (an offset above k_max_pitch_offset). Pure; thread-safe;
/// not real-time safe (allocates the specs).
[[nodiscard]] Result<std::vector<EditSpec>> correct_pitch_edits(const AnalysisResult& analysis,
                                                                const CorrectPitchParams& params);

}  // namespace anno
