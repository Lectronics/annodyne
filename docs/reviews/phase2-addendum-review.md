# Phase 2 addendum review: TimeMap spans, SegmentId, render walks spans

- **Status:** CLOSED 2026-10-19 on branch `phase-2-addendum` (the `phase-2` tag is not moved).
- **Design:** [`docs/design/phase2-addendum-timemap-spans.md`](../design/phase2-addendum-timemap-spans.md); **ADR:** [0008](../decisions/0008-segment-timeline-and-timemap-spans.md) (accepted), amendments to ADR-0005 §16 and ADR-0006.

## Built
- `TimeMap` is a finite span model (warp / cut / insert): `make`, `identity(length)`, `from_anchors(anchors, length)`, `to_output` -> `optional`, `to_output_end`, `to_source` -> `variant<SourceTime, GeneratedTime>`, `slope_at`, `spans()`. Real-time safe queries; `GeneratedTime` holds a span index so it never allocates.
- `SegmentId{kind, value}`; `EditTarget` holds `segment_ids` / `orphaned_segment_ids`; gap targets pass through remap. Categories `gap`, `layout`, `breath`, `breath_chain`, `breath_assignment` exist as enumerators only.
- `read_through_map` (`core/synthesis/engine`): the "original" on the output axis. `PassthroughEngine` and the test gain engine use it.
- `render_region`: optional `RenderInputs::time_map` (wins over timing edits), output-axis frame counts, whole-map cache key (format version 2), `RenderResult.source_range` removed.
- Specs, ROADMAP (phases 2, 5, 6, 10) and the three Phase 5/6 design notes updated to the `anno::` names; ADR renumbered 0001 -> 0008.

## Gate results
| Gate | Result |
|---|---|
| debug-asan, release, debug-tsan: all tests | 341 pass (8 audio, 63 contract, 270 unit) in debug-asan; release and tsan passed on the earlier commit e30808b, and release/tsan were not re-run after the review fixes |
| clang-tidy on changed source and test files | clean |
| null test, undo-null test, untouched-region test | bit-exact (audio-verifier) |
| cut + insert render test (`render_spans_test`) | right length (136800 frames), digital silence in the insert, source samples elsewhere, bit-identical outside blend zones, for edited notes before and after the splice |
| no code assumes the inverse map always returns a `SourceTime` | confirmed by structure-reviewer: `src/` calls `to_output`/`to_output_end` only in `render.cpp` (both optionals checked); nothing calls `to_source`; `stretch_ratio` and `anchors()` are gone |
| structure review | design note: APPROVED (after two rounds); implementation: APPROVED WITH FIXES, all fixed except as listed below |

## Deviations and decisions
- `GeneratedTime` is `{span index, offset}`, not `{SegmentId, offset}` (allocation-free; recorded in the design note and ADR).
- `from_anchors` turns a note at source 0 pushed later into a leading insert (silence). Accepted until Phase 5, where the layout solver replaces it (recorded).
- Query domain is a precondition (asserted in debug, clamped in release), not an error value.
- `render_region` refuses an edited note outside the recording (`invalid_argument`).
- Read-only specs `annodyne-implementation-guide.md:358` and `annodyne-features.md:181` still say neighbours are stretched; ADR-0008 overrides them and the owner has not unlocked the files.

## Open items
- The committed `audition/phase2-passthrough/pitch_up_200c/report.md` has a different render id than a fresh rebuild (the cache key changed); the audio, edits and every check row are identical. Refresh the report when the package is next regenerated.
- Gap and breath rendering, the layout solver and `anno-cli` support for supplying a TimeMap are Phase 5/6; until then cut and insert rendering is covered by unit tests only (no audition package).
- `dirty_regions` is still source-axis and Flex-based; revisit with the layout solver.
- Sample-aligned shifts are required by `read_through_map`; fractional shifts are `not_implemented`. Phase 5's layout solver must produce whole-sample shifts.
- Human listening: none needed (no audible change for identity maps).
