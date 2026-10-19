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

/// Longest crossfade the renderer accepts (architecture spec §5.5: 5-20 ms). Callers of
/// dirty_regions must use crossfade_margin >= k_max_crossfade + 1 sample, so the blend zone of an
/// edited note lies inside its dirty range (ADR-0006).
inline constexpr Seconds k_max_crossfade{0.020};

struct DirtyParams {
    /// Guard band added on both sides of a changed note. The same concept as the renderer's
    /// crossfade length at a different layer: this must be >= k_max_crossfade + 1 sample.
    Seconds crossfade_margin{0.050};
    SourceTime source_end;  // end of the recording (a timing change dirties up to here)
};

static_assert(k_max_crossfade.value() + 0.001 <= DirtyParams{}.crossfade_margin.value(),
              "the default dirty margin must contain the longest crossfade plus a sample at 1 kHz");

/// Source ranges whose rendered content may differ between two edit lists of the same track: a
/// safe over-approximation, never smaller than the set of changed notes.
///  - Only active notes (is_active) take part. For each whose EffectiveNote differs between the
///    lists: [source_start - margin, source_end + margin], clamped at the source origin.
///  - If a timing field differs (onset_shift, length_scale, handles, attack_time_scale), the
///    TimeMap changes: audio is stretched back to the previous changed handle or the origin and
///    forward to the next one or the end, and repair can cascade both ways. So everything from the
///    origin to the end of the recording (params.source_end or the end of the last active note)
///    is dirty.
/// Result: sorted, merged, non-overlapping. Empty when nothing changed.
/// Preconditions: `notes` sorted by source_start. Errors: invalid_argument if the lists belong to
/// different tracks, a note's track differs, or notes are unsorted.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<SourceRange>> dirty_regions(std::span<const Note> notes,
                                                             const EditList& old_edits,
                                                             const EditList& new_edits,
                                                             const DirtyParams& params);

}  // namespace anno
