#pragma once

#include "core/analysis/pitch_target/pitch_target.h"

#include <span>
#include <vector>

// Flags that need the whole take (design note §3 step 7, §12 S3/S4/S13). Pure data in, flags out.

namespace anno {

/// Flags that need many notes, one PitchFlags per note (same order), to be OR-ed into each
/// Note::pitch_target.flags by the caller (a Note is immutable once built). Never changes a
/// target. Notes must be sorted by start (as in AnalysisResult). Phrases are runs of notes
/// separated by less than `phrase_gap` of silence.
///  - `run`: `run_min_notes` or more consecutive notes, each at most `run_max_note` long, with at
///    most `run_max_gap` between them (a melisma / fast run: corrected as a whole, later).
///  - `consistent_off_grid`: the take's median offset from the grid is removed first (a singer
///    sharp throughout is not singing blue notes); a pitch class (nearest semitone mod 12) with at
///    least `off_grid_min_count` whole/settled notes whose remaining offsets agree within
///    `off_grid_spread` and whose median is at least `off_grid_min_offset` from the grid flags
///    those notes (a blue note: leave alone by default).
///  - `expressive_peak`: in a phrase of at least `peak_min_phrase_notes` notes, the note with the
///    highest target, if it is sharp of the grid by `peak_min_offset`..`peak_tolerance` after the
///    take's offset is removed.
/// Notes of kind unpitched / multi / glide / ornament are ignored by the last two. Errors:
/// invalid_argument (invalid params, notes not sorted by source_start). Pure; thread-safe; not
/// real-time safe (allocates).
[[nodiscard]] Result<std::vector<PitchFlags>> annotate_take(std::span<const Note> notes,
                                                            const PitchTargetParams& params);

}  // namespace anno
