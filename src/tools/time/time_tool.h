#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/time_pair.h"
#include "core/types/units.h"

// Time Tool (features spec §11, ROADMAP Phase 5): the edits a move, a length drag and a time handle
// make. Reads the analysis, never audio. Design: docs/design/phase5-time-tools.md.

namespace anno {

/// Moves a note in time: its onset plays `shift` later (earlier if negative) than analysed.
struct NoteMove {
    NoteId note;
    Seconds
        shift;  // output onset minus the analysed source onset; a second move replaces the first
};

/// Sets how long the note's body plays. The body is [attack_end, source_end]; the attack only
/// changes with the attack speed tool, so the whole note plays for the attack's length plus this.
struct NoteLength {
    NoteId note;
    Seconds body_length;  // output duration of the body, > 0
};

/// A breakpoint inside the body: source instant `handle.source` plays at output time
/// `handle.output` (an absolute output anchor, ADR-0005 §16).
struct NoteHandle {
    NoteId note;
    TimePair handle;  // source strictly inside the analysed body; output finite and >= 0
};

/// Removes the handle keyed by `source` (within k_handle_key). Removing a handle that is not there
/// is a silent no-op in the fold; a stale handle outside the current body stays removable.
struct NoteHandleRemoval {
    NoteId note;
    SourceTime source;
};

/// The edit that plays the note `move.shift` later: origin "time_tool", category time, op set,
/// param `onset_shift`. A shift below k_neutral_snap_offset folds to no edit.
/// Errors: invalid_argument (unknown or inactive note; a non-finite shift). Every note kind is
/// accepted. Pure; thread-safe; not real-time safe (allocates the spec).
[[nodiscard]] Result<EditSpec> note_move_edit(const AnalysisResult& analysis, const NoteMove& move);

/// The edit that makes the body play `length.body_length` long: category time, op set, param
/// `length_scale` = body_length / (source_end - attack_end).
/// Errors, in this order: invalid_argument (unknown or inactive note; a non-finite or non-positive
/// body_length; a note with no body, attack_end == source_end); out_of_range (the scale is below
/// k_min_time_scale). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<EditSpec> note_length_edit(const AnalysisResult& analysis,
                                                const NoteLength& length);

/// The edit that sets one time handle: category time, op set, param `time_handle`, value the pair.
/// Errors: invalid_argument (unknown or inactive note; a non-finite source or output; an output
/// below 0; a source not strictly inside the analysed body by more than k_handle_key, since the
/// builder would ignore it). How the handle orders against the note's other anchors is the
/// builder's repair, not the tool's. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<EditSpec> note_handle_edit(const AnalysisResult& analysis,
                                                const NoteHandle& handle);

/// The edit that removes a handle: op reset, param `time_handle`, value a pair holding `source`
/// (the output is ignored by the fold). Errors: invalid_argument (unknown or inactive note; a
/// non-finite source). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<EditSpec> note_handle_remove_edit(const AnalysisResult& analysis,
                                                       const NoteHandleRemoval& removal);

}  // namespace anno
