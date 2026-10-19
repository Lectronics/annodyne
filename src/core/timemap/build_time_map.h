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
    /// End of the recording. Fixed anchor (source_end -> same output time): audio after the last
    /// edited handle is stretched up to it, so handle edits keep the file length. The origin
    /// (default) means unknown: the tail after the last handle is then shifted, not stretched.
    SourceTime source_end;
};

struct TimeMapBuild {
    TimeMap map;
    int repaired_notes = 0;  // notes whose requested timing could not be honoured exactly
};

/// Builds the track's TimeMap from `notes` (active ones only) and the timing edits in `edits`
/// (onset_shift, length_scale, attack_time_scale, time_handle). Flex-style (ADR-0005 §11):
///  - Only notes with a timing edit pin anchors. The source origin and params.source_end are fixed
///    anchors. Everything between two neighbouring anchors, edited or not, is stretched linearly,
///    so moving a handle stretches the audio on both sides up to the next changed handle or the
///    ends of the file. Unedited notes in between are stretched too.
///  - Within an edited note the attack segment [source_start, attack_end] is scaled only by
///    attack_time_scale; the rest absorbs length_scale (consonant/attack protection); handles
///    inside the body are absolute output anchors.
///  - Conflicts are repaired deterministically and counted, never an error: outputs are pushed
///    later, and pulled back from the fixed end, so each segment keeps min_segment; a coincident
///    boundary takes the later output; if there is no room at all the unedited timeline is used.
///  - With no timing edits the result is exactly TimeMap::identity() and repaired_notes == 0.
/// Preconditions: `notes` sorted by source_start. Errors: invalid_argument if active notes overlap
/// in source time (polyphony is Phase 11), are unsorted, belong to another track than `edits`, or
/// end after a non-origin params.source_end.
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<TimeMapBuild>
build_time_map(std::span<const Note> notes, const EditList& edits, const TimeMapParams& params);

}  // namespace anno
