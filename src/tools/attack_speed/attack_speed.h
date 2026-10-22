#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Attack Speed Tool (features spec §12, ROADMAP Phase 5). Reads the analysis, never audio.
// Design: docs/design/phase5-time-tools.md.

namespace anno {

/// Sharper (< 1) or softer (> 1) attack: the attack segment [source_start, attack_end] plays
/// `time_scale` times as long. 1 = analysed.
struct AttackSpeed {
    NoteId note;
    Ratio time_scale;
};

/// The edit: origin "attack_speed_tool", category attack, op set, param `attack_time_scale`.
/// Errors, in this order: invalid_argument (unknown or inactive note; a non-finite or non-positive
/// scale; a note with no attack, attack_end == source_start); out_of_range (the scale is below
/// k_min_time_scale). A scale within 1e-12 of 1 folds to no edit. Pure; thread-safe; not real-time
/// safe (allocates the spec).
[[nodiscard]] Result<EditSpec> attack_speed_edit(const AnalysisResult& analysis,
                                                 const AttackSpeed& speed);

}  // namespace anno
