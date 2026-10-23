#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Amplitude Tool (features spec §7, ROADMAP Phase 6): the level of a note, its mute and its
// within-note flattening. Reads the analysis, never audio. Design: docs/design/phase6-level.md.

namespace anno {

/// The edit that sets the note's gain: origin "amplitude_tool", category amplitude, op set, param
/// `gain`. A gain within k_neutral_snap_offset of 0 folds to no edit; -120 dB and below is a mute.
/// Errors: invalid_argument (unknown or inactive note, a non-finite gain or one outside the legal
/// range of the parameter). Pure; thread-safe; not real-time safe (allocates the spec).
[[nodiscard]] Result<EditSpec> gain_edit(const AnalysisResult& analysis, const NoteId& note,
                                         Decibels gain);

/// The edit that mutes the note (gain_edit at -120 dB).
[[nodiscard]] Result<EditSpec> mute_edit(const AnalysisResult& analysis, const NoteId& note);

/// The edit that sets how far the note's level is pulled toward its own mean: 0 = as analysed,
/// 1 = flat. Origin "amplitude_tool", category amplitude, op set, param `within_note_flatten`.
/// Errors: invalid_argument (unknown or inactive note, a non-finite amount or one outside 0..1).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<EditSpec> flatten_edit(const AnalysisResult& analysis, const NoteId& note,
                                            Ratio amount);

}  // namespace anno
