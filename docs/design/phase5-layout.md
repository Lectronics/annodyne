# Design note: Layout markers and the shift-first solver (Phase 5, slice 8)

- **Phase:** 5 (Timing)
- **Status:** draft, revision 1 (owner's marker model, 2026-10-22); not reviewed
- **Spec sections:** architecture spec §2, §5.4; ADR-0005 §16; ADR-0008 §4 and §5; ADR-0023 §3; `silence-and-gaps.md` §1.2-1.4 and §3.3-3.5 (this note replaces them for Phase 5); `phase5-gaps-core.md` (needs its types: `SegmentTimeline`, `Gap`, `GapRegion`).
- **Folders / CMake targets:** `src/core/types` (marker and layout types), `src/core/timemap` (solver, layer 3). Nothing new in synthesis: spans already render (`stretch_span`, `read_through_map`, inserts as silence). Room tone is slice 9.
- **ADR needed:** ADR-0025 (proposed with this note): amends ADR-0008 §4 and ADR-0023 §3 (the layout path, the mode, `dirty_regions`).

## Purpose

A timing edit changes the length of something (a move, a longer note, a longer attack, a handle). Something downstream has to give. There are three ways, and none is best every time (owner, 2026-10-22):

- **absorb**: the gap after the edit takes the change (a cut in it, or an insert), so later notes stay where they are while the gap has room. Nothing but the gap is touched: notes keep their audio and their position.
- **ripple**: the change carries on: everything downstream moves by the same amount (pure shift; the gap is unchanged; the output gets longer or shorter).
- **stretch**: the audio between this marker and the next is stretched (Flex, ADR-0005 §16; what `build_time_map` does today).

The owner's model: **markers** that hold a position (a source time) and the parameters of the edit that sits there (its shift, its length change), kept in source order; where a marker sits in the sequence decides whether the markers downstream have their own length changed. Because the markers are derived from the edits, not stored on their own, they are **dynamic**: every edit re-derives the markers and re-solves, and the result is a function of the set of edits and the mode, never of the order the edits were made in. A cascade (the same edit on many notes, for instance "shorten every gap to 400 ms") is just many markers with the same kind of delta; the carry resolves them in one pass.

## Public types (core/types/layout.h, data only)

```cpp
enum class MarkerKind { note_start, attack_end, handle, note_end, gap_target };

/// One timing request at a source position. `delta` is the change this marker makes to the output
/// time that follows it (relative: the output distance from the previous marker is the source
/// distance plus `delta`); `absolute` instead pins the marker's output time (a time handle).
struct LayoutMarker {
    MarkerKind kind = MarkerKind::note_start;
    SourceTime at;
    Seconds delta{};                          // ignored when `absolute`
    std::optional<OutputTime> pinned;         // set: an absolute output anchor (time_handle)
    std::optional<NoteId> note;               // the note the marker came from (empty: a gap target)
};

enum class LayoutMode {
    absorb_then_ripple,    // gaps take the change while they have room, the rest ripples (default)
    absorb_then_stretch,   // gaps take it, the rest stretches up to the next pinned marker (Flex)
    ripple,                // never touch a gap: everything downstream shifts
    stretch                // today's Flex builder, unchanged
};

struct LayoutParams {
    LayoutMode mode = LayoutMode::absorb_then_ripple;
    Seconds min_gap{0.020};       // a gap is never cut shorter than its protected regions + this
    Hz sample_rate{0.0};          // > 0: every cut and insert is a whole number of samples
};

/// Where the solver put one marker and what it did to the gap after it.
struct MarkerPlacement { std::size_t marker; OutputTime at; };
struct GapChange { std::size_t gap; Seconds new_length; };   // cut (shorter) or insert (longer)
struct Layout {
    std::vector<MarkerPlacement> placements;
    std::vector<GapChange> gaps;
    int repaired = 0;             // requests the solver could not honour exactly, counted not failed
};
```

## Public functions (core/timemap)

```cpp
/// The markers of the active notes' timing edits, in source order: note_start (onset_shift), a
/// handle per time handle (pinned), attack_end (attack_time_scale), note_end (length_scale), and a
/// gap_target per gap edit that sets a target duration (a later slice). Pure; thread-safe.
[[nodiscard]] std::vector<LayoutMarker> layout_markers(std::span<const Note>, const EditList&);

/// The carry pass. Walks the markers in source order with `carry` = the output time owed by the
/// edits so far (initially 0): a marker adds its delta; a pinned marker sets the carry so the marker
/// lands on its output time. Between two notes the gap is the place the carry goes:
///   absorb_*:  a positive carry shortens the gap to at most carry (never below the gap's protected
///              regions + min_gap); a negative carry lengthens it by -carry (an insert). The part
///              the gap could not take stays in the carry.
///   ripple:    the gap is untouched.
/// What stays in the carry after the gap passes to the next note (ripple) or is spread over the
/// audio up to the next pinned marker or the recording's end (stretch). `fixed_end`: the carry is
/// absorbed by the stretch up to the end instead of lengthening the output.
/// Cut position: inside the gap's `silence` region, centred; an insert goes at the same place.
/// Protected: `tail` and `protected_preroll` regions are never cut. Whole-sample: with a sample
/// rate every cut and insert length is rounded to whole samples (the carry keeps the remainder).
/// Errors: invalid_argument (markers not sorted by source time or outside the timeline, bad
/// params). Never fails for an unsatisfiable request: it repairs and counts (`Layout::repaired`).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<Layout> solve_layout(const SegmentTimeline&, std::span<const LayoutMarker>,
                                          const LayoutParams&);

/// The TimeMap of a layout: warps for the notes and the kept parts of gaps, cuts and inserts where
/// the gaps changed, a stretched warp where a stretch mode applies; whole-sample shifts for every
/// slope-1 span. Errors: internal (the spans do not tile: a bug).
[[nodiscard]] Result<TimeMapBuild> build_layout_map(const SegmentTimeline&, const Layout&,
                                                    const TimeMapParams&);

/// The entry point render_region uses once a timeline exists: markers -> solve -> map. mode `stretch`
/// calls the existing build_time_map (bit-identical to today).
[[nodiscard]] Result<TimeMapBuild> build_time_map(std::span<const Note>, const SegmentTimeline&,
                                                  const EditList&, const LayoutParams&,
                                                  const TimeMapParams&);
```

`build_time_map(notes, edits, params)` (the Phase 1 builder) stays and is what `LayoutMode::stretch` and every caller without a timeline use.

## How the carry works (the owner's example)

Notes A, B, C with gaps g1 (A-B) and g2 (B-C). Lengthen A by 0.3 s (`note_end` marker, delta +0.3): the carry is 0.3 after A. g1 is 0.5 s with a 0.1 s protected minimum, so it gives 0.3 (cut in its silence); the carry is 0; B and C are exactly where they were and their audio is untouched. Lengthen A by 0.6: g1 gives 0.4 (to its minimum), the carry 0.2 reaches B: `absorb_then_ripple` shifts B and C by 0.2; `absorb_then_stretch` stretches B's audio to hold C in place; `ripple` ignores g1 and shifts everything by 0.6. If B also has a `note_end` delta of +0.2 the carry at g2 is 0.2 + 0.2 = 0.4: the later edit's effect depends on the edits before it in the sequence, which is the point of the markers. Shorten A by 0.3 (delta -0.3): g1 is lengthened by 0.3 with an insert (silence for now, room tone in slice 9) and B stays put.

## Effect on audio

Changes only audio that moves or is cut or inserted. With `absorb_*` the notes are untouched samples; a cut inside `silence` is the only removed audio (joined with the splice of `core/dsp/crossfade`, equal power, 5 ms, a test with the click detector). Stretching happens only in the stretch modes and where the carry exceeds the gaps' room.

## Consequences for existing code

- `render_region` builds the map with the new `build_time_map` when `analysis->timeline` exists, mode from `RenderParams::layout` (hashed into the cache key; key version bumped); without a timeline it behaves as today (ADR-0023).
- `dirty_regions`: a layout change dirties from the first changed marker to the end (a ripple moves everything after it); with `absorb` and a changed gap it dirties the gap's output range and, if the carry left over, onward. The function takes the timeline (the signature change of ADR-0024 is shared).
- Cuts and inserts lie only inside gap `silence` regions (ADR-0008 §2's rule, checked by `build_layout_map`).
- `tools/time` is unchanged; the UI gains a mode switch (Phase 10).

## Split / join decisions

- **`layout_markers`, `solve_layout`, `build_layout_map` separate:** the first reads edits, the second is pure arithmetic over a timeline (testable with no audio or notes), the third builds spans (split rules 2 and 4).
- **The mode is an `enum class`**, never a bool (coding standards §3); four values because two axes (what the gap does, what the remainder does) cross.
- **Markers are derived, not stored:** a stored marker list would be a second source of truth next to the `EditList` (undo, remap).
- **No `allocate_stretch`:** the stretch modes spread the leftover carry linearly between markers; segment weights (sibilants stretch less) are a later refinement of the map, not of this solver.

## Tests (written first)

- **Carry arithmetic (no audio):** each worked example above, for all four modes; the carry across several notes; a pinned marker resets the carry; the repaired count when a gap's minimum is reached and when a note is moved before its predecessor's end.
- **Order independence:** the layout of a set of edits is the same for any order they were added (property test over random edit sets).
- **Map invariants (property tests):** spans tile the source and the output; cuts and inserts only inside `silence`; whole-sample shifts at a sample rate; `to_source(to_output(s)) == s` for kept `s`.
- **Equivalence:** mode `stretch` equals the Phase 1 builder; with no timing edit every mode gives the identity map.
- **Render:** moving a note earlier by less than its gap leaves both neighbours bit-identical (shifted) and the gap shorter by the move; click detector on every cut; a `tail` region is never cut; the exhausted gap falls back as the mode says; `absorb_then_stretch` renders through `stretch_span`.
- **Dirty regions and cache key** follow the mode and the markers.

## Open questions

1. Default mode: `absorb_then_ripple` (notes untouched) is proposed; the listening sheet decides.
2. Per-edit mode (a marker carrying its own mode) instead of one mode per render: not now; the solver takes one mode, markers carry no mode (join rule: no speculative generality).
3. Where in a long silence to cut: centred is a first guess; the quietest frame is the alternative (slice 9 with the floor curve).

## Review log

*(not reviewed yet)*
