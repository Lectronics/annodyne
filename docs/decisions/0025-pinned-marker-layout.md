# ADR-0025: Pinned markers and the absorb-first layout

- **Date:** 2026-10-22
- **Status:** proposed 2026-10-22 (owner's model and answers of the same day; structure review round 2 pending)
- **Phase:** 5
- **Amends:** ADR-0008 §4 (fallback order) and §5 (ripple wording); ADR-0005 §16 (the default mode); ADR-0023 §3 (`TailPolicy::shift` becomes local to an interval in the ripple modes)

## Context
`build_time_map` is Flex: moving or lengthening a note stretches everything between the changed markers, including unedited notes and gaps. ADR-0008 §4 chose "shift first, stretch when the gap is used up" but never defined it; the owner (2026-10-22) fixed the model: markers are pinned positions; moving one changes only the two intervals next to it; the gap next to the edit absorbs first; whatever it cannot take is stretched Flex-style (default) or rippled (optional); the cumulative map is kept by the span list. Design: `docs/design/phase5-layout.md`.

## Decision
1. **Markers are the existing anchors** (a note's start, attack end, handles, end), absolute output times, derived from the effective edit state; they are never stored. `onset_shift` keeps its meaning (a requested position against the source).
2. **`LayoutMode`** `{absorb_then_stretch (default), absorb_then_ripple, ripple, stretch}` in `core/types/layout_mode.h`; `RenderParams::layout` holds it; it is hashed into the cache key (version 5).
3. **Per interval, the one gap adjacent to the edited marker absorbs first** (a cut in the longest `silence` region, centred, or an insert), never `tail` or `protected_preroll`, never below `min_gap` and the splice margin; the remainder is stretched over the rest of the interval (default), rippled (ripple modes), or, in `stretch`, the whole change is stretched as today.
4. **Whole samples:** marker shifts are quantised as in ADR-0023 §3a, so a gap absorbs a whole number of samples exactly; no remainder is carried.
5. **Rendering:** a cut gets an equal-power splice inside its silence region (post-pass in `render_unedited`), an insert a fade into digital silence until room tone (slice 9); an edited note whose zone meets a cut or insert is `not_implemented`, named. "Bit-identical outside dirty regions" (rule 5) is read as: notes and silence outside the splice zones are the source, shifted by whole samples.
6. **Without a timeline** the mode is ignored (`stretch`; the ripple modes are the pure shift); `RenderResult::layout_mode_used` reports it.
7. **Retired:** ADR-0008 §4's "second handle at the next transient": the stretch domain is the whole interval between markers.
8. `dirty_regions` stays an over-approximation (gap before the earliest changed note to the end; origin-to-end for `stretch`); block reuse is decided by the cache key.

## Alternatives considered
- Pure carry/delta markers: contradicts `onset_shift` and the Time Tool contract, and needs a second repair story. Rejected.
- Absorb in every gap down the line: changes gaps the user did not touch. Rejected by the owner.
- Ripple as the default: the owner wants Flex behaviour with absorbing first. Rejected as default; kept as a mode.

## Consequences
- New code in `core/timemap` (`layout_markers`, `solve_layout`, `build_layout_map`, `build_layout_time_map`); `render_region` and `RenderResult` change; `dirty_regions` gains the timeline parameter (shared with ADR-0024).
- ADR-0008 §4 and ADR-0005 §16 read through this ADR.
