#pragma once

#include "core/edits/edit_list.h"
#include "core/types/note.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// dirty_regions: which source ranges need re-rendering (architecture spec §4.4; ADR-0005 §8).
// Design: docs/design/phase1-data-model.md.

namespace anno {

struct DirtyParams {
    Seconds crossfade_margin{0.050};  // added on both sides of a changed note
    SourceTime source_end;            // end of the recording (a timing change dirties up to here)
};

/// Source ranges whose rendered content may differ between two edit lists of the same track: a
/// safe over-approximation, never smaller than the set of changed notes.
///  - Only active notes (is_active) take part. For each whose EffectiveNote differs between the
///    lists: [source_start - margin, source_end + margin], clamped at the source origin.
///  - If a timing field differs (onset_shift, length_scale, handles, attack_time_scale), the
///    TimeMap changes there and overlap repair can cascade, so everything from the start of the
///    previous active note (or the origin) minus margin up to params.source_end is dirty. The
///    previous note's start, not its end: an abutting neighbour's last segment is stretched.
/// Result: sorted, merged, non-overlapping. Empty when nothing changed.
/// Preconditions: `notes` sorted by source_start. Errors: invalid_argument if the lists belong to
/// different tracks, a note's track differs, or notes are unsorted.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<SourceRange>> dirty_regions(std::span<const Note> notes,
                                                             const EditList& old_edits,
                                                             const EditList& new_edits,
                                                             const DirtyParams& params);

}  // namespace anno
