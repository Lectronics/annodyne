#pragma once

#include "core/types/frame_grid.h"
#include "core/types/ids.h"
#include "core/types/note.h"
#include "core/types/units.h"

// NoteSpan: one segmented note before it is described (ADR-0010). Plain data; produced by
// segment_notes, consumed by the description stages and the orchestrator, which alone builds a
// Note. Design: docs/design/phase3-notes.md.

namespace anno {

/// One segmented note before it is described: no curves, no loudness. NOT a Note.
struct NoteSpan {
    NoteId id;
    SourceTime source_start;
    SourceTime attack_end;
    SourceTime release_start;
    SourceTime source_end;  // start <= attack_end <= release_start <= end
    Cents rough_pitch;      // the median smoothed f0 over the note's stable part; refined by
                            // perceived_pitch
    SeparationIn separation_in = SeparationIn::hard;
    double confidence_prob = 1.0;  // 0..1
};

/// The frame grid of a note: the pitch-track frames in [source_start, source_end): start =
/// frame_time of the first pitch frame >= source_start, hop = pitch_grid.hop, n_frames = how many
/// such frames there are (0 if none; segment_notes never produces such a note). Curves (drift, mod,
/// amp) are values AT those frames; nothing is interpolated off-grid. Pure; real-time safe.
[[nodiscard]] FrameGrid note_grid(const NoteSpan& span, const FrameGrid& pitch_grid);

}  // namespace anno
