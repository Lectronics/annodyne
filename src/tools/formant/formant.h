#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Formant Tool (features spec 6): shifts a note's formant envelope independently of its pitch.
// Reads the analysis, never audio. Design: docs/design/phase6-expression-tools.md.

namespace anno {

/// The edit that sets the note's `formant_offset` (cents of envelope shift, + = brighter/smaller):
/// origin "formant_tool", category formant, op set. 0 folds to no edit.
/// Errors: invalid_argument (unknown or inactive note, a non-finite offset or one outside the legal
/// range of the parameter). Pure; thread-safe; not real-time safe (allocates the spec).
[[nodiscard]] Result<EditSpec> formant_edit(const AnalysisResult& analysis, const NoteId& note,
                                            Cents offset);

}  // namespace anno
