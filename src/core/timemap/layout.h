#pragma once

#include "core/edits/edit_list.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <cstddef>
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

}  // namespace anno
