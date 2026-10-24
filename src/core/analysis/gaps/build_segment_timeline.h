#pragma once

#include "core/analysis/activity/activity.h"
#include "core/analysis/gaps/gap_evidence.h"
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
    Seconds preroll_guard{0.015};   // the protected_preroll region before a note
    Seconds min_audible{0.02};      // a run above floor + tail_margin shorter than this is ignored
    Seconds audible_pad{0.01};      // an audible region reaches this far each side of its run
    Decibels event_margin{3.0};     // a classified frame is evidence if excess_db is above this
    Seconds evidence_guard{0.015};  // classified runs ignore this long after a note's end (the
                                    // classifier's smoothing and the note's release bleed over)
};

/// Partitions [0, source_end] into the active notes (each exactly its source range) and the gaps
/// between them: a leading gap from 0 to the first note, a gap between every two notes that do not
/// touch exactly, a trailing gap to source_end; no gap where a note starts at 0 or ends at
/// source_end. (A space of any positive length is a gap: a hole would break the partition, so the
/// consumers apply their own minimums: the layout's min_gap, the gate's fade.) With no active note
/// the whole recording is one gap. Ghost (inactive) notes are ignored. A source_end of 0 gives the
/// empty timeline. floor_dbfs = the median of the floor curve over the gap's frames [start, end)
/// (the nearest frame if it holds none).
/// Regions of a gap, in order, tiling it: `tail` (from the previous note's end while the activity
/// level is strictly above floor + tail_margin, at most max_tail), then stretches in between, then
/// `protected_preroll` (the last preroll_guard before the next note), `silence` elsewhere. The
/// stretches are (1) classified runs: maximal runs of one class in {sibilant, breath, plosive} with
/// `evidence.excess_db > event_margin`, from the gap's frames after evidence_guard past a note's
/// end and before the preroll, sibilant and breath at least min_audible long, a plosive of any
/// length; padded by audible_pad, clipped to the earlier region; and (2) `audible` runs: runs of
/// frames strictly above floor + tail_margin, at least min_audible, padded, minus the classified
/// regions (an audible remainder shorter than min_audible joins the classified region it touches).
/// `neutral_gap_evidence(activity.grid)` gives the level-only regions.
/// Errors: invalid_argument (bad params; the floor, the evidence and the activity track on
/// different grids or with arrays that do not match; active notes unsorted, overlapping, of no
/// length or outside [0, source_end]; a non-finite source_end). Pure; thread-safe; not real-time
/// safe.
[[nodiscard]] Result<SegmentTimeline>
build_segment_timeline(std::span<const Note> notes, SourceTime source_end,
                       const ActivityTrack& activity, const NoiseFloor& floor,
                       const GapEvidence& evidence, const GapParams& params);

}  // namespace anno
