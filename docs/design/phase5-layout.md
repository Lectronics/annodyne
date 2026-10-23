# Design note: Pinned markers and the absorb-first layout (Phase 5, slice 8)

- **Phase:** 5 (Timing)
- **Status:** draft, revision 3 (owner's answers 2026-10-22; answers structure review rounds 1 and 2)
- **Spec sections:** architecture spec §2, §5.4; ADR-0005 §16; ADR-0008 §4, §5; ADR-0023 §3, §3a, §7; ADR-0025 (proposed with this note); `phase5-gaps-core.md` (`SegmentTimeline`, `Gap`, `GapRegion`); `phase5-time-tools.md`; `silence-and-gaps.md` §1.2-1.4 (replaced for Phase 5 by this note).
- **Folders / CMake targets:** `src/core/timemap` (layer 3: `layout.h/.cpp`, `layout_markers.cpp`, `build_layout_map.cpp`), `src/core/timemap/detail` (shared anchor helpers), `src/core/render` (the splice, `render_unedited`).

## Revision 4 (owner, 2026-10-22): read this first

Where this block and the text below differ, this block wins (ADR-0025 revision 2, ADR-0023 amendment 1).

- **Ripple is dropped**, not deferred. Slice 8g is removed; open question 0 is closed.
- **Silent anchors.** The start and end of the recording are fixed anchors treated as silence; the output length equals the source length; `render_region` defaults to `keep_length` once the layout path is wired (slice 8e flips it).
- **Shift grid** replaces whole-sample quantisation: marker shifts are multiples of `step_samples`, the step being a 1/512 (or 1/256) note from the tempo (fallback 120 bpm), normalised to whole samples. Source marker positions round to whole samples, so every span maps N whole samples to M whole samples. `sample_rate` in the slice list reads `shift_grid {step_samples}`; `room` of a cut is floored to whole grid steps. Slice 8d tests the normalisation (44.1/48/96 kHz, several tempi) and the bound on the rounding error (half a step).
- **Local invalidation.** `dirty_regions` returns the intervals whose spans differ between the old and new maps; block keys hash only the spans a block reads. Slice 8h is part of 8e (not later).
- **Base clip** is the output of `build_layout_map` + `render_unedited`; pitch, level and formant edits are applied on top and never recompute it.
- **Group moves** are one `note_move_edit` per note with the same delta (see `clips-layers-and-groups.md`).

## The model (the owner's)

Every timing edit places **markers**: pinned positions, each a source time with the output time it must play at. They are the anchors `build_time_map` builds today (a note's start, attack end, handles, end), in source order. The audio between two consecutive markers is one **interval**. The rules:

1. **Local.** Moving a marker changes only the two intervals next to it: one gets longer, one shorter (one is stretched, the other squished). Markers further away keep their output times. Nothing else is touched.
2. **Cumulative.** The map from output back to source is the running sum over the intervals, so at every point the solver knows which part of the real wave it is playing (the existing `TimeMap` spans are exactly that).
3. **Absorb first.** An interval that contains a gap next to the edited marker and that needs to be longer or shorter takes the change **in that gap** (a cut or an insert inside its silence), so the notes keep their audio, slope 1, and only shift. This works as long as the edited note is isolated by gaps with room. When the gap cannot take all of it (not enough silence, legato neighbour), the rest is spread over the other audio of the interval by stretching or squishing (Flex).
4. **Dynamic.** Markers are derived from the effective edit state on every solve (`layout_markers`), never stored: the layout is a pure function of (effective states, timeline, mode, params), so it does not depend on the order the edits were made in (two `set`s of one parameter still resolve by the edit list's own seq rule, before this stage).

## Modes

```cpp
// core/timemap/layout.h
enum class LayoutMode {
    absorb_then_stretch,   // DEFAULT: the gap takes the change first, the rest of the interval is
                           // stretched between the markers (Flex)
    stretch                // today's builder, unchanged (Flex without absorbing)
};
struct LayoutParams {
    LayoutMode mode = LayoutMode::absorb_then_stretch;
    Seconds min_gap{0.020};       // a cut never leaves a gap shorter than this
    Seconds splice_fade{0.005};   // crossfade at a cut / fade at an insert (round 1: one name)
    Seconds note_margin{0.010};   // the renderer's crossfade_length: a cut never lands within
                                  // note_margin + splice_fade of a note edge
};
```

`RenderParams` holds a `LayoutParams` (the whole struct is hashed into the cache key, so `splice_fade`, `min_gap` and `note_margin` are covered). **The ripple modes are dropped** (review B7; owner 2026-10-22): with absolute markers a ripple has no coherent meaning (an edited marker cannot both keep its requested time and be pushed). If the owner wants them, they need relative markers or a plain "shift the rest of the file" tail; that is a decision for the owner, recorded in the open questions. `TimeMapParams::tail` is ignored by the layout path: both modes use a fixed end (below).

**Without a timeline** the mode is ignored and `stretch` runs (the Phase 1 builder); `RenderResult::layout_mode_used` says which ran. Tested.

## Markers

```cpp
enum class MarkerKind { origin, note_start, attack_end, handle, note_end, end };
struct LayoutMarker {
    MarkerKind kind = MarkerKind::origin;
    SourceTime source;
    OutputTime output;          // absolute, requested
    std::size_t note = k_no_note;   // index into the notes span (origin and end: none)
};
struct LayoutMarkers {
    std::vector<LayoutMarker> markers;  // sorted, merged, includes origin and end
    std::vector<bool> repaired;         // per note: a request that was changed (counted once per note)
};
/// Pre: notes sorted by start. Errors: invalid_argument as build_time_map's check_notes (notes of
/// another track, overlapping, unsorted, ending after source_end).
[[nodiscard]] Result<LayoutMarkers> layout_markers(std::span<const Note>, const EditList&,
                                                   SourceTime source_end);
```

The derivation table is `desired_anchors` unchanged (below). `layout_markers` also does what `build_time_map` does before it repairs: it adds the **origin marker (0, 0)** and the **end marker (source_end, source_end)** (the fixed end: the layout path always keeps the recording's length), merges coincident markers (later output wins; a differing output marks the note repaired, as `append` does) and sorts by (source, note index, kind rank). Only notes with a timing edit make markers (as `has_timing_edit`). The shared pieces (`check_notes`, `enforce_min_segments`, `quantise_shifts`, the `Anchor` merge) move out of the anonymous namespace of `build_time_map.cpp` into `core/timemap/detail/anchors.h/.cpp` in slice 8b as a **no-behaviour-change refactor** (the Phase 1 tests are the guard).

## The solver

**Order of operations (a constraint):** (1) markers merged; (2) Phase 1 repair `enforce_min_segments` on the marker outputs (min_segment push, pull back from the fixed end, fall back to the unedited timeline if impossible); (3) `quantise_shifts` (whole-sample shifts, strictness push); (4) the interval solve below. Only after 3 are `Δ` and the cut/insert lengths derived, so they are whole sample counts.

For consecutive markers `L, R` with `shift(m) = out(m) - src(m)`, source length `S = src(R) - src(L)`, `Δ = shift(R) - shift(L)` (the requested output length is `S + Δ`):

1. `Δ == 0`: slope 1, nothing to do.
2. **Which gap absorbs (deterministic, one gap only).** In this order, the first that exists: (a) `L` is a `note_end` and a gap follows it in the timeline (the gap after `L`); (b) `L` is the `origin` and the first segment is a gap (the leading gap); (c) `R` is a `note_start` and a gap precedes it; (d) `R` is the `end` and the last segment is a gap (the trailing gap); else none. A gap belongs to exactly one interval (markers sit on note boundaries). If the chosen gap has no `silence` region, it absorbs nothing. A note at source 0 moved later has no leading gap in the timeline: the origin interval has no absorbing gap and the whole Δ is residual (today's leading insert span for a gap-less start is kept as the Phase 1 does; not "an insert inside a note").
3. **Worked table** (notes A, B, C; gaps g0 leading, gA after A, gB after B, gC trailing; `Δ` is the interval's):

| Edit | Interval | Absorbing gap | Result |
|---|---|---|---|
| A moved later by d | origin -> A.start (Δ = +d) | (b) g0 (inserts d) | A keeps slope 1, shifted; A.end -> next marker unchanged unless A has an end marker |
| A.length +d (end marker) | A.end -> end (Δ = -d: A.end now has shift +d, the fixed end 0) | (a) gA | gA is cut by d (if room), else B and C are squished |
| C moved earlier by d | previous marker (A.end or origin) -> C.start (Δ = -d) | (a) gA if the left marker is A.end, else (c) gB | B stays slope 1 if gA absorbs everything; the cut is in gA, B and gB shift earlier together |
| A moved +d and C moved -d | A.end -> C.start (Δ = -2d) | (a) gA | gA cuts what it can, the rest squishes B and gB |
| unedited B between A.end and C.start | same | (a) gA first, gB is never used | per the rule: one gap |

4. **Cut or insert.** `Δ < 0`: cut. The cut lies in the **longest `silence` region** of the gap (ties: the earliest), centred; `room = floor_to_samples(silence_length - min_gap - 2 * (note_margin + splice_fade))`; `cut = min(-Δ, room)` in whole samples; no cut if `room < 1` sample. `Δ > 0`: an insert of `Δ` (whole samples) at the same centre; no insert if the gap has no `silence` region. A cut shorter than `splice_fade` gets a crossfade of its own length (minimum one sample). `GapEdit` carries `Samples length`, the source endpoints as `SourceTime`, and is the authority from which the spans are built.
5. **Span order inside the interval (slope-1 remnants).** Absorbing gap after `L` (a, b): `[gap's left remnant][cut/insert][gap's right remnant][other audio, stretched to end at R]`; absorbing gap before `R` (c, d): `[other audio, stretched][gap's left remnant][cut/insert][gap's right remnant pinned to R]`. The remnants (every region of the gap, `tail` and `protected_preroll` included) are slope 1 and whole-sample shifted: the left remnant of (a) has `shift(L)`, the right remnant `shift(L) - cut` (or `+ insert`); the remnants of (c, d) are pinned to `shift(R)`. `S_other = S - gap source length` (unedited notes and the interval's other gaps only). The other audio gets `slope = (S_other + Δ_rest) / S_other` where `Δ_rest = Δ + cut` (or `- insert`) is what the gap did not take; it is exactly 1 when the gap took everything.
6. **Repair (a single pass in source order, deterministic).** If `S_other == 0` and `Δ_rest != 0`, or the slope leaves `[0.25, 4]`, or `S_other < min_segment` (the interval cannot give what is asked), the **right marker's** output is moved to the nearest feasible value (its shift becomes `shift(L) + the largest |Δ| the interval can take`), the owning note is marked repaired, and the next interval sees the changed marker. If the right marker is the fixed end (its shift cannot move), the whole timeline falls back to the unedited layout and every note is marked repaired (as Phase 1). `Layout::repaired` is **per note** (the same unit as `TimeMapBuild::repaired_notes`); `stretch` mode returns the Phase 1 builder's value unchanged. The causality property (changing a marker leaves the layout before the previous marker identical) holds except when step 2's pull-back from the fixed end or the fallback fires; the test says so.

```cpp
struct GapEdit {
    std::size_t gap;           // index into SegmentTimeline::gaps()
    bool cut;                  // true: source removed; false: output inserted
    SourceTime at;             // cut: the removed source range starts here; insert: the position
    Samples length;            // whole samples (authority for the spans)
};
struct IntervalPlan {
    SourceTime from, to;       // the markers' source times
    std::optional<GapEdit> gap;
    double residual_slope;     // of the other audio; 1 when the gap took everything
};
struct Layout { std::vector<IntervalPlan> intervals; std::vector<bool> repaired; };

[[nodiscard]] Result<Layout> solve_layout(const SegmentTimeline&, const LayoutMarkers&,
                                          const LayoutParams&, const TimeMapParams&);
[[nodiscard]] Result<TimeMapBuild> build_layout_map(const SegmentTimeline&, const LayoutMarkers&,
                                                    const Layout&, const TimeMapParams&);
/// markers -> solve -> map; mode `stretch` is build_time_map unchanged (bit-identical).
[[nodiscard]] Result<TimeMapBuild> build_layout_time_map(std::span<const Note>,
                                                         const SegmentTimeline&, const EditList&,
                                                         const LayoutParams&, const TimeMapParams&);
```

`solve_layout` checks that every marker's note is in the timeline with the same range (`invalid_argument`); `build_layout_map` checks that cuts and inserts lie inside gap `silence` regions (so the comment in `time_map.h` is corrected: only `build_layout_map` checks it). `sample_rate == 0` (exact arithmetic) is allowed for tests: lengths are then `Seconds`-exact and `Samples` is not used.

## Rendering (B7)

- **Cut**: a cut span has no output (as `read_through_map`). The splice is a **post-pass in `render_unedited`** (layer 4, `core/dsp/crossfade`, equal power, length `splice_fade`): around the join point the output is the left audio continued into the cut and the right audio taken from before it, crossfaded; both continuations come from the source **inside the same silence region** (guaranteed by `room` above), so no note audio is mixed. Samples inside the splice zone differ from the source; the definition of "bit-identical outside" (rule 5, ADR-0023 §7) is: every note's samples and every silence sample outside the splice zones are the source shifted by a whole number of samples.
- **Insert**: digital zeros with a `splice_fade` fade from the silence audio into zero and back (no click); room tone replaces it in slice 9. The click detector test covers inserts as well as cuts.
- **An edited note whose zone (note + margin) meets a cut or an insert** returns `not_implemented` naming the note (as for stretched zones, ADR-0023): `read_through_map` is the engines' "original" and does not splice.
- **Stretched intervals** render through `stretch_span` (ADR-0023); an edited note inside a stretched interval is the existing `not_implemented`.

## Consequences

- `render_region` uses `build_layout_time_map` when `analysis->timeline` exists (`RenderParams::layout` is a `LayoutParams`, hashed whole into the cache key: key version "the next one after ADR-0024's slices land", 5 if ADR-0024's 4 is in); otherwise as today. `RenderResult` gains `layout_mode_used`; `repaired_notes` is the per-note count.
- **`dirty_regions` keeps the origin-to-end rule** for any timing change until slice 8h proves a narrower one safe (a stretched interval reaches back to the previous marker and repair can cascade both ways); the cache key decides block reuse.
- **The insert is plain digital zeros** with a fade until room tone (slice 9): audible as a noise-floor dropout; the audition package must flag it.
- **ADR-0025** supersedes ADR-0008 §4's fallback order and its transient-handle rule, ADR-0005 §16 for the default mode (Flex stays as `stretch` and as the fallback) and the "ripple bounded by anchors" wording of ADR-0008 §5; it keeps ADR-0023 §3/§3a. The ADR numbers are added to those ADRs' status lines when 0025 is accepted.

## Split / join decisions

- **Three functions** (markers, solve, map): edits in, arithmetic, spans out (split rules 2 and 4).
- **All layout types in `core/timemap`** (`RenderParams` holds a `LayoutParams`; layer 4 may include layer 3); no speculative `gap_target` marker kind yet.
- **A new entry-point name** (`build_layout_time_map`), not an overload.
- **One absorbing gap per interval** (owner), so the solver has no search; a second gap could join later without a type change.

## Tests (written first; the slices below)

- **Markers (8b):** the table for every parameter; a handle then a move (repaired, counted); coincident markers; the sort key; markers from the same effective state in any edit order are identical; splitting an `add` into two `add`s gives the same markers.
- **Solver, no audio (8c, 8d):** a note moved later/earlier with both gaps (cut or insert in the gap next to the marker, the other side untouched); lengthen/shorten the end; a move larger than the gap's room (cut to `min_gap`, residual stretch for the rest); legato neighbour (no gap: all residual); leading/trailing virtual gaps; a gap with only `tail` + `protected_preroll` (an insert works, a cut never); a note moved across its neighbour (monotone result, repaired); a pinned handle after a delta and a delta after a handle; whole-sample: every slope-1 span aligned, lengths integral, at several sample rates; `sample_rate` 0 exact; causality (changing a marker leaves the layout before the previous marker bit-identical); conservation (absorbed + residual = requested `Δ`).
- **Map (8e):** span tiling/round-trip property tests; cuts and inserts only in `silence`; `stretch` mode equals `build_time_map` bit for bit; no timeline: the mode is ignored (`stretch`); every mode with no timing edit gives the identity.
- **Render (8f, 8g):** a note moved by less than its gap: both neighbours slope 1, notes bit-identical shifted, the gap shorter by the move, click detector on the splice; a cut shorter than the crossfade or closer than a crossfade to a note edge refused/repaired; insert fade (click detector); an edited note next to a cut `not_implemented`; `absorb_then_stretch` residual through `stretch_span` with the ratio bound; mode switch changes the cache key; determinism (twice, same bytes); audition package (audio-verifier).

## Slices

8a this note + ADR-0025 reviewed (round 2); 8b types and `layout_markers`; 8c `solve_layout` for `absorb_then_stretch`/`stretch`, `sample_rate` 0; 8d whole-sample arithmetic and property tests; 8e `build_layout_map` + `render_region` wiring (key version 5, `RenderParams::layout`); 8f splice and insert fades in `render_unedited` (audio-verifier); 8g (dropped); 8h `dirty_regions` (with the ADR-0024 timeline parameter). 8b needs only the markers and the detail refactor; 8c needs the table above; 8d the whole-sample property tests; 8f the splice (B2, B6 settled above).

## Open questions

0. **Ripple modes** (`absorb_then_ripple`, `ripple`): dropped by the owner, 2026-10-22. (Old text:) With absolute markers they need a definition: either only "shift the rest of the file" (the existing `TailPolicy::shift`) or relative markers. Owner to decide.

1. Where in the silence to cut (centred now; the quietest frame once the floor curve is used, slice 9).
2. A second absorbing gap (the one on the other side of a long note) is not used now.

## Review log

*(round 2 pending; round 1 findings answered above: B1/B2 anchors are absolute, the derivation table is `desired_anchors`; B3 the Phase 1 repair rules apply, a move past room cuts to `min_gap` and stretches the rest; B4 first gap only; B5 intervals between markers, ratio bound, whole-interval stretch; B6 integer whole-sample `Δ` by quantised markers; B7 splice post-pass, room constraint, `not_implemented` for edited notes near a cut; S1 `GapEdit` carries the position; S2 legato and virtual gaps; S3 `TimeMapParams` kept, one entry point name; S4 handle pinned, end derived absolutely; S5 total key; S6 `dirty_regions` over-approximation; S7 no timeline; S8 types in timemap; S9 ADR-0025; S10 limitation stated.)*
