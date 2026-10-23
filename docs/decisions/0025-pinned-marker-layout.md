# ADR-0025: Pinned markers and the absorb-first layout

- **Date:** 2026-10-22
- **Status:** proposed 2026-10-22, revision 2 (owner's model and answers of the same day; updated after the owner's review of ADR-0023 and the clips conversation)
- **Phase:** 5
- **Amends:** ADR-0008 §4 (fallback order) and §5 (ripple wording); ADR-0005 §16 (the default mode); ADR-0023 §3 (the layout path ignores `TailPolicy` and keeps the length; `shift` stays for callers without a timeline)

## Context
`build_time_map` is Flex: moving or lengthening a note stretches everything between the changed markers, including unedited notes and gaps. ADR-0008 §4 chose "shift first, stretch when the gap is used up" but never defined it; the owner (2026-10-22) fixed the model: markers are pinned positions; moving one changes only the two intervals next to it; the gap next to the edit absorbs first; whatever it cannot take is stretched Flex-style; ripple is **dropped** (owner, 2026-10-22: with notes as independent objects, pinned markers are the right model); the cumulative map is kept by the span list. Design: `docs/design/phase5-layout.md`.

## Decision
1. **Markers are the existing anchors** (a note's start, attack end, handles, end), absolute output times, derived from the effective edit state; they are never stored. `onset_shift` keeps its meaning (a requested position against the source).
2. **`LayoutMode`** `{absorb_then_stretch (default), stretch}` in `core/timemap/layout.h`; `RenderParams::layout` is a `LayoutParams` hashed whole into the cache key. **Ripple is dropped**: it has no coherent meaning with absolute markers (owner confirmed 2026-10-22). Moving a group of notes is the same pinned rule (decision 9).
3. **Per interval, the one gap adjacent to the edited marker absorbs first** (a cut in the longest `silence` region, centred, or an insert), never `tail` or `protected_preroll`, never below `min_gap` and the splice margin; the remainder is stretched over the rest of the interval, or, in `stretch`, the whole change is stretched as today. The absorbing gap is the one after the left marker when it is a note end, else the leading gap at the origin, else the one before the right marker when it is a note start, else the trailing gap; the start and the end of the recording are **silent anchors** (fixed): the layout always keeps the recording's length, and `render_region` defaults to `keep_length` (ADR-0023 amendment 1, 3).
4. **Shift grid and whole samples:** marker shifts are quantised to the shift grid of ADR-0023 amendment 1 (3a), a step of 1/256 or 1/512 note from the tempo, normalised to a whole number of samples. A gap therefore absorbs a whole number of grid steps exactly; no remainder is carried. Source marker positions round to a whole sample, so a stretched span maps N whole source samples to M whole output samples (amendment 1, 7). The cut size `room` is also floored to a whole number of grid steps.
5. **Rendering:** a cut gets an equal-power splice inside its silence region (post-pass in `render_unedited`), an insert a fade into digital silence until room tone (slice 9); an edited note whose zone meets a cut or insert is `not_implemented`, named. "Bit-identical outside dirty regions" (rule 5) is read as: notes and silence outside the splice zones are the source, shifted by whole samples.
6. **Without a timeline** the mode is ignored (`stretch`); `RenderResult::layout_mode_used` reports it.
7. **Retired:** ADR-0008 §4's "second handle at the next transient": the stretch domain is the whole interval between markers.
8. **Local invalidation** (ADR-0023 amendment 1, 5): in `absorb_then_stretch` a marker move changes only the two intervals next to it, so `dirty_regions` returns exactly the intervals whose spans differ between the old and the new map, and block cache keys hash only the spans a block reads. Slice 8h is therefore part of the layout work, not a later refinement. `stretch` mode keeps the origin-to-end behaviour of the Flex builder.
9. **Group moves** need no new solver: a group of notes moved by one delta is one `note_move_edit` per note with the same shift, so their inner markers move together and only the intervals at the two ends of the group change, absorbing in their gaps first (`docs/design/clips-layers-and-groups.md`).
10. **The base clip** (ADR-0023 amendment 1, 2) is the layout's output: `build_layout_map` plus `render_unedited` produce it; pitch, level and formant edits never recompute it.

## Alternatives considered
- Pure carry/delta markers: contradicts `onset_shift` and the Time Tool contract, and needs a second repair story. Rejected.
- Absorb in every gap down the line: changes gaps the user did not touch. Rejected by the owner.
- Ripple, as default or as a mode: dropped by the owner (2026-10-22).

## Consequences
- New code in `core/timemap` (`layout_markers`, `solve_layout`, `build_layout_map`, `build_layout_time_map`); `render_region` and `RenderResult` change; `dirty_regions` gains the timeline parameter (shared with ADR-0024).
- ADR-0008 §4 and ADR-0005 §16 read through this ADR.
