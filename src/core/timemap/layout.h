#pragma once

#include "core/edits/edit_list.h"
#include "core/timemap/build_time_map.h"
#include "core/timemap/shift_grid.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/segments.h"
#include "core/types/units.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// The layout of timing edits (ADR-0025; design: docs/design/phase5-layout.md): markers are the
// pinned positions the timing edits make; the solver (later slices) decides where a gap absorbs a
// change and where audio is stretched.

namespace anno {

enum class LayoutMode {
    absorb_then_stretch,  // DEFAULT: the gap next to the edit takes the change first; the rest of
                          // the interval is stretched between the markers (Flex)
    stretch               // today's builder (build_time_map), unchanged
};

struct LayoutParams {
    LayoutMode mode = LayoutMode::absorb_then_stretch;
    Seconds min_gap{0.020};      // a cut never leaves a gap shorter than this
    Seconds splice_fade{0.005};  // crossfade at a cut, fade at an insert
    Seconds note_margin{0.010};  // the renderer's crossfade_length: a cut keeps this + splice_fade
                                 // away from a note edge
    ShiftGridParams grid = {};   // the shift step (tempo, 1/512 note); see shift_grid.h
};

enum class MarkerKind { origin, note_start, attack_end, handle, note_end, end };

inline constexpr std::size_t k_no_marker_note = static_cast<std::size_t>(-1);

/// One pinned position: source time and the output time it must play at (absolute).
struct LayoutMarker {
    MarkerKind kind = MarkerKind::origin;
    SourceTime source;
    OutputTime output;
    std::size_t note = k_no_marker_note;  // index into the notes span; none for origin and end
};

struct LayoutMarkers {
    std::vector<LayoutMarker> markers;  // by source time, merged; origin first
    std::vector<bool> repaired;         // per note: a requested time that merging changed
};

/// The markers of the active notes' timing edits, as build_time_map derives its anchors
/// (docs/design/phase5-layout.md, the derivation table): for each note with a timing edit its
/// start (start + onset_shift), attack end (attack_end + shift + attack extra), every handle inside
/// the body (absolute output) and end (end + shift + attack extra + body extra). Added: the origin
/// (0, 0) and, when the last marker is before `source_end`, the end marker (source_end,
/// source_end), the fixed end (the layout path always keeps the recording's length). Coincident
/// source times merge and keep the later output (a differing output marks the later note
/// repaired); a merged marker takes the highest-ranked kind (start < attack end < handle < end is
/// by position; at a legato boundary the later note's start). A pure function of the effective
/// states: the order the edits were made in does not matter.
/// Pre: `notes` sorted by start. `source_end` must be > 0.
/// Errors: invalid_argument (source_end not > 0; notes of another track, overlapping, unsorted or
/// invalid; a note ending after source_end). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<LayoutMarkers> layout_markers(std::span<const Note> notes,
                                                   const EditList& edits, SourceTime source_end);

/// A cut (source removed) inside one gap's `silence` region, or the hold of a growing interval's
/// gap: the gap stays at slope 1 and the other audio is stretched; `length` is then 0, or the part
/// of the growth the audio could not take (silence inserted at `at`).
struct GapEdit {
    std::size_t gap = 0;  // index into SegmentTimeline::gaps()
    bool cut = true;      // true: [at, at + length] of the source is removed; false: hold, plus
                          // `length` seconds of silence inserted at `at`
    SourceTime at;
    Seconds length{0.0};  // a whole number of samples when TimeMapParams::sample_rate > 0
};

/// One interval between two consecutive markers, after repair. Output times are absolute and
/// satisfy out_to - out_from = (to - from) + the interval's delta. Span order inside (design note):
/// with an absorbing gap after the left marker [gap left remnant][cut/insert][gap right
/// remnant][other audio, stretched]; with a gap before the right marker [other audio,
/// stretched][left remnant] [cut/insert][right remnant]. Remnants have slope 1. Without a gap edit
/// the whole interval is stretched by `residual_slope`.
struct IntervalPlan {
    SourceTime from;
    SourceTime to;
    OutputTime out_from;
    OutputTime out_to;
    std::optional<GapEdit> gap;
    double residual_slope = 1.0;  // of the audio the gap edit did not cover; 1 when it took all
};

struct Layout {
    std::vector<IntervalPlan> intervals;  // consecutive, from the origin to the end marker
    std::vector<bool> repaired;           // per note, as LayoutMarkers::repaired
    bool fell_back = false;               // the edits could not fit: the unedited layout
};

/// Slope limits of the stretched audio (ADR-0023 section 3): a request beyond them is repaired.
inline constexpr double k_min_layout_slope = 0.25;
inline constexpr double k_max_layout_slope = 4.0;

/// The interval solve of ADR-0025 (docs/design/phase5-layout.md, "The solver"): repairs the
/// markers' outputs (minimum segment, pull-back from the fixed end, whole-sample shifts when
/// `time_map.sample_rate` > 0), then per interval lets the one adjacent gap absorb the change
/// (rule a-d of the design note) and stretches the rest within the slope limits. In mode `stretch`
/// no gap absorbs and the slope is not limited (today's builder). Uses `time_map.min_segment` and
/// `sample_rate`; ignores `source_end` and `tail` (the layout keeps the recording's length).
/// Errors: invalid_argument if a marker does not sit where the timeline says (origin at 0, end at
/// the timeline's end, note starts and ends on note segments, attack ends and handles inside one).
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<Layout> solve_layout(const SegmentTimeline& timeline,
                                          const LayoutMarkers& markers, const LayoutParams& params,
                                          const TimeMapParams& time_map);

}  // namespace anno
