#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Main Tool (features spec "Main Tool", ROADMAP Phase 4): the edit a pitch drag makes. Reads the
// analysis, never audio. Design: docs/design/phase4-pitch-edit.md.

namespace anno {

/// Where the user dropped a note: its new pitch target, absolute cents (A4 = 6900).
struct PitchDrag {
    NoteId note;
    Cents target;
};

/// The edit that puts the note's pitch target at `drag.target`: category pitch, op set,
/// `center_offset` = drag.target - note.pitch_target.target (origin "main_tool", target = the
/// note). The baseline is the analysed target, so a second drag replaces the first and lands on
/// its own target. It does not snap: the caller passes the final target (target_pitch is a
/// separate call). Glide and ornament notes are accepted (their `target` is the main pitch).
/// The result has passed validate_for_add.
/// Errors: invalid_argument (non-finite target, unknown or inactive note, a note of kind multi or
/// unpitched, which have no single target); out_of_range (|offset| above k_max_pitch_offset).
/// Pure; thread-safe; not real-time safe (allocates the spec).
[[nodiscard]] Result<EditSpec> pitch_drag_edit(const AnalysisResult& analysis,
                                               const PitchDrag& drag);

}  // namespace anno
