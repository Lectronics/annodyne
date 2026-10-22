# ADR-0023: Stretched TimeMap spans render as part of the unedited base

- **Date:** 2026-10-22
- **Status:** proposed 2026-10-22 (owner: work through the Phase 5 timing items in order); structure review round 1 of the design note answered
- **Phase:** 5
- **Amends:** ADR-0008 §7 (a warp with slope != 1 renders from Phase 5, not Phase 6), ADR-0006 (`RenderResult` gains `repaired_notes`; the pristine base is `render_unedited`)

## Context
`build_time_map` stretches everything between two anchors: the edited note, unedited notes, gaps and the tail. `render_region` takes `read_through_map` as "the original" and refuses any slope != 1 span, so no timing edit that stretches can render. Design: `docs/design/phase5-stretch-render.md`.

## Decision
1. **The unedited base is `render_unedited`**: slope-1 warps and inserts exactly as `read_through_map`; slope != 1 warps through `stretch_span` (TD-PSOLA time scaling for voiced frames, power-complementary grains for the rest; pitch and timbre kept). "No edits, same audio" is unchanged for slope-1 spans and holds for stretches as "stretch only".
2. **Edited notes** (pitch, level, formant, ...) are rendered by the engine passed to `render_region` as today and blended over the base. A note whose only edits are timing edits is part of the base, not an edited note. A pitch/formant/level edit on a note whose zone meets a slope != 1 span returns `not_implemented` naming the note (routing engine: Phase 5 slice 3).
3. **Whole-sample anchors.** `TimeMapParams::sample_rate` (default 0 = off) rounds every output anchor to a whole sample, so every slope-1 span stays a whole-sample shift and rule 5 holds. `render_region` sets it. Caller-supplied maps must satisfy the same.
4. **New targets** `core/synthesis/grain` (marks, fractional reader, grain overlap-add, extracted from PSOLA, output bit-identical, `-ffp-contract=off`) and `core/synthesis/stretch` (`stretch_span`), both layer 2. `render_unedited` lives in `core/render`.
5. **Cache:** a stretch invalidates every block after it (the key includes the map; `dirty_regions` already returns origin-to-end for timing). Accepted for now.
6. `RenderResult::repaired_notes` reports `TimeMapBuild::repaired_notes`.

## Alternatives considered
- **Stretch only the edited note's zone**: the map also stretches neighbours and gaps, so the base would still be undefined there. Rejected.
- **Interpolate slope-1 spans at fractional shifts**: not bit-exact, breaks rule 5 for the whole tail. Rejected for rounding anchors (an error of under one sample in the stretch ratio).
- **A `SynthesisEngine` for stretching**: it is not chosen per note and would have one caller. Rejected.

## Consequences
- ADR-0008 §7, `read_through_map` and `render.cpp` mention Phase 6 for stretching; they say Phase 5.
- The layout solver of ADR-0008 §4 (shift first) later reduces how much is stretched; it needs no change here.
- Vibrato rate changes with the stretch until `regenerate_pitch_curve` exists; plosive/sibilant protection until the segment weights (slice 2).
