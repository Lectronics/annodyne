#pragma once

#include "core/types/frame_grid.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// What the features say about each frame of a recording, for the gap regions (ADR-0024 amendment 1;
// design: docs/design/phase5-gap-consonants.md). Produced by classify_gap_frames, read by
// build_segment_timeline.

namespace anno {

/// One entry per frame of `grid` (the activity grid, source axis). Entries of note frames are
/// unspecified and never read.
struct GapEvidence {
    FrameGrid grid;
    // The class the ADR-0017 classifier gives the frame when judged as unvoiced: only silence,
    // plosive, sibilant or breath can occur.
    std::vector<FrameClass> frame_class;
    // activity.level - floor.curve per frame, dB: the threshold-free quantity.
    std::vector<Decibels> excess_db;
};

/// Evidence that supports nothing: every frame `silence`, an excess far below any margin. With it
/// build_segment_timeline gives the level-only regions. Pure; thread-safe.
[[nodiscard]] GapEvidence neutral_gap_evidence(const FrameGrid& grid);

}  // namespace anno
