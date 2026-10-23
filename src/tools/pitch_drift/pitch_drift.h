#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Pitch Drift Tool (features spec 5b): scales the note's slow pitch component: 1 = as analysed, 0 =
// straightened. Reads the analysis, never audio. Design: docs/design/phase6-expression-tools.md.

namespace anno {

/// The edit that sets the note's `drift_scale`: origin "pitch_drift_tool", category pitch_drift, op
/// set. A scale within k_neutral_snap_offset of 1 folds to no edit. Errors: invalid_argument
/// (unknown or inactive note, a non-finite scale or one outside the legal range of the parameter).
/// Pure; thread-safe; not real-time safe (allocates the spec).
[[nodiscard]] Result<EditSpec> drift_edit(const AnalysisResult& analysis, const NoteId& note,
                                          Ratio scale);

}  // namespace anno
