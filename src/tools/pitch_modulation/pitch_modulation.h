#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Pitch Modulation Tool (features spec 5b, vibrato): scales the note's fast pitch component
// (vibrato, fine movement): 1 = as analysed, 0 = flat, 2 = twice as deep, negative = inverted.
// Reads the analysis, never audio. Design: docs/design/phase6-expression-tools.md.

namespace anno {

/// The edit that sets the note's `mod_scale`: origin "pitch_modulation_tool", category pitch_mod,
/// op set. A scale within k_neutral_snap_offset of 1 folds to no edit. Errors: invalid_argument
/// (unknown or inactive note, a non-finite scale or one outside the legal range of the parameter).
/// Pure; thread-safe; not real-time safe (allocates the spec).
[[nodiscard]] Result<EditSpec> modulation_edit(const AnalysisResult& analysis, const NoteId& note,
                                               Ratio scale);

}  // namespace anno
