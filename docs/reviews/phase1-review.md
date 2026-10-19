# Phase 1 review: Data model and edit contracts

- **Status:** CLOSED locally 2026-10-19. Exit gate (structure audit #1) passed; ADR-0005 accepted by
  the owner (decision 4 changed to Flex-style time handles). Tag `phase-1` set locally; not pushed.
- **Branch:** `phase-1` (local, not pushed)

## Built
Design note: [`docs/design/phase1-data-model.md`](../design/phase1-data-model.md) (see its
"Implementation notes" and "Audit #1 fixes"). ADR: `docs/decisions/0005-edit-model.md` (accepted).

- **`core/types`:** `NoteId`/`TrackId`/`SourceId`/`EditSeq`/`GroupId`, `FrameGrid`, `FrameMatrix`,
  the analysis tracks, `Note`, `AnalysisResult` (with `validate` and `find_note`), the edit
  vocabulary (`EditParam` table, `EditSpec`, `Edit`), `EffectiveNote`, `TimePair`, `TimeMap`.
- **`core/edits`:** `EditList` (add, undo, redo, reset, groups, cursor, `retargeted`,
  `to_parts`/`from_parts`), `effective_state`, `dirty_regions`, `match_notes`,
  `remap_after_reanalysis`.
- **`core/timemap`:** `build_time_map` (Flex-style: attack protection, handles stretch the audio to the next
  changed handle or the file ends, deterministic repair with a repair count).
- **Tests:** 216 CTest tests, including property tests with 300 seeded
  cases each for every ROADMAP contract bullet.

## Gates
| Gate | Result |
|---|---|
| Structure review of the design note | Round 1 CHANGES REQUIRED (7 blocking), round 2 CHANGES REQUIRED (4 blocking, document-only), all fixed |
| Build + tests, ASan/UBSan, TSan, release | Pass locally (216/216 each). CI not run (branch not pushed) |
| clang-format, clang-tidy, include/layer check | Clean on all changed files |
| Objective audio checks, human listening | n/a: nothing renders yet |
| Structure audit #1 | Round 1 CHANGES REQUIRED (2 code, 1 doc blocking); after fixes APPROVED WITH FIXES |

## Bugs the tests found
- `EditList::discarded` lost its seq order when undo went past an older branch point (history
  property test).
- `dirty_regions` missed the stretched last segment of an abutting neighbour (18 of 3000 probe
  scenarios); now checked against `build_time_map` by a contract test.
- `retargeted` could put an id in both the live and orphaned lists.

## Deviations from the specs
All recorded in ADR-0005 (accepted). Summary: `seq` is the edit identity (no `id`); `EditSpec` vs
`Edit`; closed `EditParam` enum with typed values; `reset` op; `set` per parameter; linear undo with
kept branches; `dirty_regions` takes notes; `TimeMap` in `core/types`, `build_time_map` in
`core/timemap`; field renames for units; Phase 8 types deferred.

## Owner decisions (2026-10-19)
Items 1-3 and 5-7 of ADR-0005 accepted as built. Item 4 changed: time handles stretch the audio on
both sides up to the next changed handle or the file ends (like Logic Flex), instead of pushing the
neighbour. Implemented after the audit in `build_time_map` and `dirty_regions` (tests updated; the
dirty-vs-TimeMap contract test still passes unchanged).

**Open for a human before the first renderer that combines timing and pitch:** whether to
time-stretch first and then pitch-correct, the reverse, or choose per case (ADR-0005 §16).

## Open issues (notes from the audit)
`join()` in `build_time_map.cpp` takes a bool (split when next touched); `StrongId` extraction (rule
of three); `DirtyParams::source_end` defaults to the origin; `match_notes` is note geometry living
in `core/edits` (move before Phase 5 adds a second user); re-run the dirty-vs-TimeMap contract test
whenever the repair policy changes.

## Closing
Done: ADR accepted, spec pointers added, ROADMAP current phase set to 2, tag `phase-1` set locally.
Not done: push and CI (needs the owner's go-ahead). The post-audit Flex change was verified by the
test suite and clang-tidy but not re-reviewed by `structure-reviewer`; audit #2 (Phase 5) covers it.
