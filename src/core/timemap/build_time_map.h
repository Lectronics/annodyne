#pragma once

#include "core/edits/edit_list.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/units.h"

#include <span>

// build_time_map: timing edits -> TimeMap (architecture spec §4.4, §5.4; ADR-0005 §11).
// Design: docs/design/phase1-data-model.md, "TimeMap and build_time_map".

namespace anno {

struct TimeMapParams {
    /// Minimum output length of a segment that timing edits changed (builder policy, not a TimeMap
    /// invariant). Segments no edit touched keep their source length even when shorter.
    Seconds min_segment{0.001};
};

struct TimeMapBuild {
    TimeMap map;
    int repaired_notes = 0;  // notes whose requested timing could not be honoured exactly
};

/// Builds the track's TimeMap from `notes` (active ones only) and the timing edits in `edits`
/// (onset_shift, length_scale, attack_time_scale, time_handle).
///  - The attack segment [source_start, attack_end] is scaled only by attack_time_scale; the rest
///    of the note absorbs length_scale (consonant/attack protection); handles inside the body are
///    absolute output anchors.
///  - Gaps between notes are stretched by interpolation (no silence, no double audio).
///  - Collisions are repaired deterministically (the later note is shifted rigidly, a coincident
///    boundary takes the later output, crossing handles are clamped) and counted, never an error.
///  - With no timing edits the result is exactly TimeMap::identity() and repaired_notes == 0.
/// Preconditions: `notes` sorted by source_start. Errors: invalid_argument if active notes overlap
/// in source time (polyphony is Phase 11), are unsorted, or belong to another track than `edits`.
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<TimeMapBuild>
build_time_map(std::span<const Note> notes, const EditList& edits, const TimeMapParams& params);

}  // namespace anno
