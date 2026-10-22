# Design note: Pinned markers and the absorb-first layout (Phase 5, slice 8)

- **Phase:** 5 (Timing)
- **Status:** draft, revision 2 (owner's answers 2026-10-22; answers structure review round 1: B1-B7, S1-S10)
- **Spec sections:** architecture spec §2, §5.4; ADR-0005 §16; ADR-0008 §4, §5; ADR-0023 §3, §3a, §7; ADR-0025 (proposed with this note); `phase5-gaps-core.md` (`SegmentTimeline`, `Gap`, `GapRegion`); `phase5-time-tools.md`; `silence-and-gaps.md` §1.2-1.4 (replaced for Phase 5 by this note).
- **Folders / CMake targets:** `src/core/timemap` (layer 3: `layout.h/.cpp`, `layout_markers.cpp`, `build_layout_map.cpp`), `src/core/types` (`LayoutMode` only, because `RenderParams` holds it), `src/core/render` (the splice, `render_unedited`).

## The model (the owner's)

Every timing edit places **markers**: pinned positions, each a source time with the output time it must play at. They are the anchors `build_time_map` builds today (a note's start, attack end, handles, end), in source order. The audio between two consecutive markers is one **interval**. The rules:

1. **Local.** Moving a marker changes only the two intervals next to it: one gets longer, one shorter (one is stretched, the other squished). Markers further away keep their output times. Nothing else is touched.
2. **Cumulative.** The map from output back to source is the running sum over the intervals, so at every point the solver knows which part of the real wave it is playing (the existing `TimeMap` spans are exactly that).
3. **Absorb first.** An interval that contains a gap next to the edited marker and that needs to be longer or shorter takes the change **in that gap** (a cut or an insert inside its silence), so the notes keep their audio, slope 1, and only shift. This works as long as the edited note is isolated by gaps with room. When the gap cannot take all of it (not enough silence, legato neighbour), the rest is spread over the other audio of the interval by stretching or squishing (Flex), or, in the ripple modes, passed on downstream.
4. **Dynamic.** Markers are derived from the effective edit state on every solve (`layout_markers`), never stored: the layout is a pure function of (effective states, timeline, mode, params), so it does not depend on the order the edits were made in (two `set`s of one parameter still resolve by the edit list's own seq rule, before this stage).

## Modes

```cpp
enum class LayoutMode {
    absorb_then_stretch,   // DEFAULT. The gap next to the edit takes the change; the rest of the
                           // interval is stretched up to the pinned markers (Flex). Nothing moves
                           // beyond the next marker.
    absorb_then_ripple,    // The gap takes what it can; the rest shifts everything after the
                           // interval (the tail is shifted, ADR-0023 TailPolicy::shift).
    ripple,                // Gaps are never touched; every change shifts everything after.
    stretch                // Today's builder, unchanged (Flex without absorbing).
};
```

`LayoutMode` lives in `core/types/layout_mode.h` (so `RenderParams` can hold it); everything else is in `core/timemap`.

**Without a timeline** (`analysis->timeline` empty) the mode is ignored: `stretch` is used, except `ripple` and `absorb_then_ripple`, which become the pure shift (`TailPolicy::shift`: it needs no gaps); `RenderResult::layout_mode_used` says which ran. Tested.

## Derivation of the markers (the table; sign convention: a marker is an absolute output time)

For a note with effective `onset_shift = d`, `attack_time_scale = a`, `length_scale = l`, handles `h_i` (source, output):

| Marker | Source time | Output time |
|---|---|---|
| start | `start` | `start + d` |
| attack end | `attack_end` | `attack_end + d + (attack_end - start)(a - 1)` |
| handle i | `h_i.source` (strictly inside the body) | `h_i.output` (absolute) |
| end | `end` | `end + d + attack_extra + (end - attack_end)(l - 1)` |

This is `desired_anchors` in `build_time_map.cpp` unchanged, so every existing tool and test keeps its meaning (`onset_shift` stays "play d later than analysed", the Time Tool contract of `phase5-time-tools.md`). A handle set before a move stays where it is (absolute) and the conflict is repaired and counted, as today. Coincident markers (zero-length attack or body, a handle at the attack end, legato note end = next note start) keep the **later output** (as `append` does); the sort key is (source time, note index, kind rank start < attack end < handle < end), a total order. `solve_layout` checks that every marker's note is in the timeline with the same range, else `invalid_argument`.

## The solver

```cpp
struct LayoutParams {
    LayoutMode mode = LayoutMode::absorb_then_stretch;
    Seconds min_gap{0.020};      // a cut never leaves a gap shorter than its protected regions + this
    Seconds splice_fade{0.005};  // crossfade at a cut (and fade at an insert), see "Rendering"
};
```
(`TimeMapParams` keeps `min_segment`, `tail`, `sample_rate`, `source_end`; the layout entry point takes both structs; `LayoutParams` has no sample rate of its own.)

```cpp
/// Per interval between consecutive markers.
struct IntervalPlan {
    SourceRange source;          // between the two markers
    OutputRange output;          // requested: the markers' output times
    std::optional<GapEdit> gap;  // the cut or insert that absorbs the change
    double residual_slope;       // output / source length of the audio that is NOT the gap edit's
};
struct GapEdit {
    std::size_t gap;             // index into SegmentTimeline::gaps()
    bool cut;                    // true: source removed; false: output inserted
    SourceTime at;               // cut: the removed source range starts here; insert: the position
    Seconds length;              // whole samples at the sample rate
};
struct Layout {
    std::vector<IntervalPlan> intervals;
    int repaired = 0;            // requests that could not be honoured exactly
};

/// The markers of the active notes' timing edits (derivation table), sorted by the total key.
[[nodiscard]] std::vector<LayoutMarker> layout_markers(std::span<const Note>, const EditList&);

/// Intervals, gap edits and residual slopes.
[[nodiscard]] Result<Layout> solve_layout(const SegmentTimeline&, std::span<const LayoutMarker>,
                                          const LayoutParams&, const TimeMapParams&);

/// The map: warps, cuts, inserts; every slope-1 span a whole-sample shift.
[[nodiscard]] Result<TimeMapBuild> build_layout_map(const SegmentTimeline&, const Layout&,
                                                    const TimeMapParams&);

/// markers -> solve -> map; mode `stretch` is build_time_map unchanged (bit-identical).
[[nodiscard]] Result<TimeMapBuild> build_layout_time_map(std::span<const Note>, const SegmentTimeline&,
                                                         const EditList&, const LayoutParams&,
                                                         const TimeMapParams&);
```

`build_time_map(notes, edits, params)` (Phase 1) stays and is what `stretch` and every caller without a timeline use. The new entry point has its own name (no overload).

**Per interval** `[m_k, m_k+1]` with source length `S` and requested output length `L = out(m_k+1) - out(m_k)`; `Δ = L - S`:
1. `Δ == 0`: slope 1, nothing to do.
2. **The absorbing gap** is the gap adjacent to the **edited** marker of the interval: for a marker that is a note's start, the gap before it; for a note's end, the gap after it; for a handle (inside a note) none. Only that one gap (owner: first gap only). If the interval has no such gap (legato; the recording's first note has no gap before it only if it starts at 0), the whole `Δ` goes to the residual stretch. A **leading** gap before the first note and a **trailing** gap after the last are real timeline gaps; a note at source 0 with a requested later start gets an insert **inside nothing**: it is a virtual leading gap (insert only, at source 0), the same as today's leading insert span.
3. `Δ < 0` (shorten): cut `min(-Δ, room)`: only the longest `silence` region of the gap may be cut, never `tail` or `protected_preroll`; the cut is centred in it; `room = silence_length - min_gap - 2 * splice_fade`, so the crossfade zone stays inside the silence (no cut if `room <= 0`). `Δ > 0` (lengthen): an insert of `Δ` at the centre of the silence region (silence length unlimited; at a virtual gap, at its edge).
4. What the gap could not take, `Δ' = Δ - absorbed`, is the **residual**: the other audio of the interval (unedited notes and non-absorbing gaps) gets slope `residual_slope = (S_other + Δ') / S_other` (Flex). `S_other` = the interval's source length minus the gap edit's cut range (for a cut) or the whole length (insert: the insert is extra output). If `residual_slope` falls outside `[min_slope, max_slope]` of `StretchParams` (0.25..4) or `S_other` is below `min_segment`, the request is repaired by the Phase 1 rules (`enforce_min_segments` push/pull, fall back to the unedited timeline) and `repaired` counts it.
5. `absorb_then_ripple` / `ripple`: the interval after the last marker (and each interval's leftover) is not stretched: the unabsorbed `Δ'` becomes a shift of everything after the interval (the next markers' output times move by `Δ'`... only the markers that are **not pinned by an edit** move; an edited marker keeps its requested time), which is the shifted tail of ADR-0023 made local.

**Whole samples (B6).** The marker output times are quantised as `build_time_map` does (the shift `t - s` of every anchor rounded to a whole sample, after the repair), so `Δ` between two markers is a whole number of samples **exactly**; the cut and insert lengths are `Δ` (whole), never a carried remainder; the residual stretch absorbs only what the gap could not take, and when the gap takes all, every note in the interval is slope 1 with a whole-sample shift. `sample_rate == 0` (exact arithmetic) is allowed for tests. A cut's source endpoints may lie between samples; only its length is whole. The strictness push of `quantise_shifts` applies to the marker times as before.

## Rendering (B7)

- **Cut**: a cut span has no output (as `read_through_map`). The splice is a **post-pass in `render_unedited`** (layer 4, `core/dsp/crossfade`, equal power, length `splice_fade`): around the join point the output is the left audio continued into the cut and the right audio taken from before it, crossfaded; both continuations come from the source **inside the same silence region** (guaranteed by `room` above), so no note audio is mixed. Samples inside the splice zone differ from the source; the definition of "bit-identical outside" (rule 5, ADR-0023 §7) is: every note's samples and every silence sample outside the splice zones are the source shifted by a whole number of samples.
- **Insert**: digital zeros with a `splice_fade` fade from the silence audio into zero and back (no click); room tone replaces it in slice 9. The click detector test covers inserts as well as cuts.
- **An edited note whose zone (note + margin) meets a cut or an insert** returns `not_implemented` naming the note (as for stretched zones, ADR-0023): `read_through_map` is the engines' "original" and does not splice.
- **Stretched intervals** render through `stretch_span` (ADR-0023); an edited note inside a stretched interval is the existing `not_implemented`.

## Consequences

- `render_region` uses `build_layout_time_map` when `analysis->timeline` exists (mode from `RenderParams::layout`, hashed into the cache key; key version 5); otherwise as today. `RenderResult` gains `layout_mode_used` and the repaired count maps `Layout::repaired` into `repaired_notes` (one per repaired request).
- **`dirty_regions`** stays an over-approximation computed from effective states and the timeline (it cannot run the solver: layer 2 below layer 3): from the gap before the earliest changed note (less the margin) to the end for the carry modes; origin-to-end for `stretch`. The cache key, not `dirty_regions`, decides block reuse (it hashes the whole map), so a mode change invalidates through the key.
- **ADR-0025** supersedes: ADR-0008 §4's fallback order (absorb first, then Flex stretch over the **whole interval between markers**, not the "second handle at the next transient"; that rule is retired), ADR-0005 §16 for the default mode (Flex remains as `stretch` and as the fallback), and the "ripple bounded by anchors" wording of ADR-0008 §5 (the ripple modes here are local to the interval); it keeps ADR-0023 §3/§3a.

## Split / join decisions

- **Three functions** (markers, solve, map): edits in, arithmetic, spans out (split rules 2 and 4).
- **Types in `core/timemap`** except `LayoutMode` (needed by `RenderParams`); no speculative `gap_target` marker kind yet.
- **A new entry-point name** (`build_layout_time_map`), not an overload.
- **One absorbing gap per interval** (owner), so the solver has no search; a second gap could join later without a type change.

## Tests (written first; the slices below)

- **Markers (8b):** the table for every parameter; a handle then a move (repaired, counted); coincident markers; the sort key; markers from the same effective state in any edit order are identical; splitting an `add` into two `add`s gives the same markers.
- **Solver, no audio (8c, 8d):** a note moved later/earlier with both gaps (cut or insert in the gap next to the marker, the other side untouched); lengthen/shorten the end; a move larger than the gap's room (cut to `min_gap`, residual stretch for the rest); legato neighbour (no gap: all residual); leading/trailing virtual gaps; a gap with only `tail` + `protected_preroll` (an insert works, a cut never); a note moved across its neighbour (monotone result, repaired); a pinned handle after a delta and a delta after a handle; whole-sample: every slope-1 span aligned, lengths integral, at several sample rates; `sample_rate` 0 exact; causality (changing a marker leaves the layout before the previous marker bit-identical); conservation (absorbed + residual = requested `Δ`).
- **Map (8e):** span tiling/round-trip property tests; cuts and inserts only in `silence`; `stretch` mode equals `build_time_map` bit for bit; no timeline: modes ignored/`ripple` pure shift; every mode with no timing edit gives the identity.
- **Render (8f, 8g):** a note moved by less than its gap: both neighbours slope 1, notes bit-identical shifted, the gap shorter by the move, click detector on the splice; a cut shorter than the crossfade or closer than a crossfade to a note edge refused/repaired; insert fade (click detector); an edited note next to a cut `not_implemented`; `absorb_then_stretch` residual through `stretch_span` with the ratio bound; mode switch changes the cache key; determinism (twice, same bytes); audition package (audio-verifier).

## Slices

8a this note + ADR-0025 reviewed (round 2); 8b types and `layout_markers`; 8c `solve_layout` for `absorb_then_stretch`/`stretch`, `sample_rate` 0; 8d whole-sample arithmetic and property tests; 8e `build_layout_map` + `render_region` wiring (key version 5, `RenderParams::layout`); 8f splice and insert fades in `render_unedited` (audio-verifier); 8g the ripple modes; 8h `dirty_regions` (with the ADR-0024 timeline parameter).

## Open questions

1. Where in the silence to cut (centred now; the quietest frame once the floor curve is used, slice 9).
2. A second absorbing gap (the one on the other side of a long note) is not used now.

## Review log

*(round 2 pending; round 1 findings answered above: B1/B2 anchors are absolute, the derivation table is `desired_anchors`; B3 the Phase 1 repair rules apply, a move past room cuts to `min_gap` and stretches the rest; B4 first gap only; B5 intervals between markers, ratio bound, whole-interval stretch; B6 integer whole-sample `Δ` by quantised markers; B7 splice post-pass, room constraint, `not_implemented` for edited notes near a cut; S1 `GapEdit` carries the position; S2 legato and virtual gaps; S3 `TimeMapParams` kept, one entry point name; S4 handle pinned, end derived absolutely; S5 total key; S6 `dirty_regions` over-approximation; S7 no timeline; S8 types in timemap; S9 ADR-0025; S10 limitation stated.)*
