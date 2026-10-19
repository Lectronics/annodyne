# ADR-0008: Segment timeline and TimeMap spans

- **Date:** 2026-10-19
- **Status:** proposed (accepted when the Phase 2 addendum gates pass; see Consequences)
- **Phase:** 2 addendum (TimeMap span model, `SegmentId`, render walks spans); 5 (timeline, layout, gaps); 6 (breath chain, dynamics); 10 (Breath Lane UI)
- **Numbering:** drafted as "ADR-0001"; renumbered because `0001-core-io-target.md` exists.

## Context

The architecture spec defines time as a continuous, monotonic, piecewise-linear `TimeMap` built from note anchors, with gaps between notes treated as leftover space. Three planned features don't fit that model:

1. **Silence removal** (`docs/design/silence-and-gaps.md`) needs to *remove* source time and to *insert* output time with no source (room tone). A stretch-only map can't express either.
2. **Moving a note** stretched its neighbour (spec §5.4 step 5, and the Flex rule of ADR-0005 §16). Shifting adds no stretch artefacts, so it is better wherever there is room.
3. **Breaths** (`docs/design/breath-lane.md`) need to be addressable objects with their own edits, not just frames inside notes.

The layering conflict raised in `docs/design/note-stretching.md` (Open question 1) was already resolved by Phase 1: `TimeMap` lives in `core/types` (ADR-0005 §11). It stays there.

## Decision

1. **Segment timeline.** Each track's source time is partitioned into contiguous `note` and `gap` segments (`SegmentTimeline`, stored in `AnalysisResult` from Phase 5). Gaps have sub-regions: `tail`, `silence`, `breath`, `noise`.
2. **TimeMap spans.** `TimeMap` becomes an ordered list of spans of kind `warp`, `cut` or `insert`, tiling a finite source range `[0, source_length)`. `to_output(SourceTime)` returns `std::optional<OutputTime>` (empty inside a cut); `to_source(OutputTime)` returns `std::variant<SourceTime, GeneratedTime>` (generated inside an insert). Querying outside the domain is a precondition violation (asserted in debug, clamped in release), so the return types stay `optional` / `variant`. `TimeMap::make()` checks tiling and monotonicity; the rule "cut and insert only inside gap segments" cannot be checked without a timeline, so it is checked by `build_time_map` / `solve_layout` (Phase 5).
3. **Type location.** The `TimeMap` data type stays in `core/types` (already there). Building it (`solve_layout`, `build_time_map`, `plan_gap_layout`) stays in `core/timemap`.
4. **Layout rule (owner decision, supersedes spec §5.4 step 5 and refines ADR-0005 §16).** Stretch only when needed; otherwise shift.
   - A note moved into a gap **shifts**: the gap before it is cut, the gap after it is extended (room tone or silence). Its audio and its neighbours' audio are untouched samples.
   - When a moving note would hit the next note (the gap is used up), a second handle is **added automatically at the next transient** and the audio between the two handles is stretched (the Flex behaviour of ADR-0005 §16, now only as the fallback).
   - Explicit `time_handle` edits keep their absolute-output-anchor meaning.
5. **Ripple is bounded by anchors.** Removed time collects before the next anchor; the track start and end are always anchors. Default mode is gate (no time change).
6. **Edit targets generalise to `SegmentId`.** `SegmentId` is string-valued like `NoteId`; a `NoteId` converts to a `SegmentId`. `NoteId` stays its own type. Breath edits target `BreathId`. New edit categories: `gap`, `layout`, `breath`, `breath_chain`, `breath_assignment` (enum values only until their phases).
7. **Render takes the TimeMap explicitly.** `RenderInputs` gains an optional `TimeMap`; when absent it is built from the edits as today. Phase 2 renders slope-1 warps (shifted samples), cuts (nothing) and inserts (digital silence); a warp with slope ≠ 1 stays `not_implemented` until Phase 6. The cache key includes the spans.

## Alternatives considered

- **Keep a continuous TimeMap and time-compress gaps.** Simple, but compressing silence and reverb tails audibly changes their texture, and it can't remove time cleanly. Rejected.
- **Do silence removal as a destructive pre-process.** Breaks the non-destructive rule and undo. Rejected.
- **Model gaps implicitly (no segment objects).** Gap edits would have nothing stable to attach to, and breaths couldn't be addressed. Rejected.
- **Keep Flex for every handle drag (ADR-0005 §16 as written).** Stretches unedited audio and gaps even when there is room to shift. Rejected by the owner: shift first, stretch only when needed.
- **`SegmentId` as `uint64_t` with `NoteId` a subtype.** `NoteId` is a string today and is in the edit-log format; a second string-valued id is the smaller change.

## Consequences

- Every caller of `TimeMap` must handle "no output position" (cut) and "no source" (insert). The return types force this at compile time. There are no callers outside `core/types` and tests today.
- The UI playhead, MIDI export and rendering must treat insert spans as generated audio.
- From Phase 5, `AnalysisResult` gains `timeline`, `noise_floor` and `breaths`. Re-analysis remapping covers gap and breath IDs as well as notes.
- `structure-reviewer` checks for any remaining code that assumes the inverse map always exists.
- Phase 1's `TimeMap` contract tests are rewritten for span invariants now. `build_time_map` keeps emitting an all-warp map until Phase 5, when its signature is replaced.
- ADR-0005 §16 is amended: Flex applies to explicit handles and as the gap-exhausted fallback, not to every move. `dirty_regions`' rule that a timing change dirties `[0, last_end]` is revisited in Phase 5 with the layout solver.
- ADR-0006 is amended: `RenderInputs` takes an optional `TimeMap`; the cache key includes spans; `RenderResult.source_range` is replaced.

## Spec updates

- `annodyne-core-architecture.md` §2: add the three span kinds and the two optional/variant lookups.
- §3: add `Segment`, `Gap`, `SegmentTimeline`, `TimeSpan`, `BreathEvent`; add fields to `AnalysisResult`.
- §4.4: `build_time_map` signature replaced by the one in `silence-and-gaps.md` §3.4 (Phase 5).
- §5.1–5.2: add the new edit categories and generalised `EditTarget`.
- §5.4: replace step 5 with the shift-first rule (decision 4).
- §6: add folders `core/analysis/gaps`, `core/analysis/breaths`, `core/dsp/dynamics`, `core/synthesis/gap`, `core/synthesis/breath`, `tools/gap`, `macros/strip_silence`, `features/breaths`, `ui/breath_lane`.
