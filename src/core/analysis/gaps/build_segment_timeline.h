#pragma once

#include "core/analysis/activity/activity.h"
#include "core/types/noise_floor.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/segments.h"
#include "core/types/units.h"

#include <span>

// The segment timeline: the recording as notes and the gaps between them (ADR-0024; design:
// docs/design/phase5-gaps-core.md, slices 5 and 6).

namespace anno {

struct GapParams {
    Decibels tail_margin{6.0};  // a tail lasts while the floor curve + margin is exceeded
    Seconds max_tail{1.5};
    Seconds preroll_guard{0.015};  // the protected_preroll region before a note
    Seconds min_audible{0.02};     // a run above floor + tail_margin shorter than this is ignored
    Seconds audible_pad{0.01};     // an audible region reaches this far each side of its run
};

/// Partitions [0, source_end] into the active notes (each exactly its source range) and the gaps
/// between them: a leading gap from 0 to the first note, a gap between every two notes that do not
/// touch exactly, a trailing gap to source_end; no gap where a note starts at 0 or ends at
/// source_end. (A space of any positive length is a gap: a hole would break the partition, so the
/// consumers apply their own minimums: the layout's min_gap, the gate's fade.) With no active note
/// the whole recording is one gap. Each gap holds one `silence` region over its whole range
/// (slice 6 adds tail and protected_preroll regions) and floor_dbfs = the median of the floor curve
/// over the gap's frames (the nearest frame if it holds none). Ghost (inactive) notes are ignored.
/// A source_end of 0 gives the empty timeline.
/// Errors: invalid_argument (bad params; the floor and the activity track on different grids;
/// active notes unsorted, overlapping, of no length or outside [0, source_end]; a non-finite
/// source_end). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<SegmentTimeline> build_segment_timeline(std::span<const Note> notes,
                                                             SourceTime source_end,
                                                             const ActivityTrack& activity,
                                                             const NoiseFloor& floor,
                                                             const GapParams& params);

}  // namespace anno
