# Phase 1 review: Data model and edit contracts

- **Status:** DRAFT. The exit gate (structure audit #1) is passed, but the phase is **not closed**:
  ADR-0005 is `proposed` (no human was available), so ROADMAP "Current phase" is unchanged and the
  `phase-1` tag is not set. A human closes it after confirming the decisions below.
- **Branch:** `phase-1` (local, not pushed)

## Built
Design note: [`docs/design/phase1-data-model.md`](../design/phase1-data-model.md) (see its
"Implementation notes" and "Audit #1 fixes"). ADR: `docs/decisions/0005-edit-model.md` (proposed).

- **`core/types`:** `NoteId`/`TrackId`/`SourceId`/`EditSeq`/`GroupId`, `FrameGrid`, `FrameMatrix`,
  the analysis tracks, `Note`, `AnalysisResult` (with `validate` and `find_note`), the edit
  vocabulary (`EditParam` table, `EditSpec`, `Edit`), `EffectiveNote`, `TimePair`, `TimeMap`.
- **`core/edits`:** `EditList` (add, undo, redo, reset, groups, cursor, `retargeted`,
  `to_parts`/`from_parts`), `effective_state`, `dirty_regions`, `match_notes`,
  `remap_after_reanalysis`.
- **`core/timemap`:** `build_time_map` (attack protection, gap stretch, handles, deterministic
  overlap repair with a repair count).
- **Tests:** 211 CTest tests (164 unit, 47 contract), including property tests with 300 seeded
  cases each for every ROADMAP contract bullet.

## Gates
| Gate | Result |
|---|---|
| Structure review of the design note | Round 1 CHANGES REQUIRED (7 blocking), round 2 CHANGES REQUIRED (4 blocking, document-only), all fixed |
| Build + tests, ASan/UBSan, TSan, release | Pass locally (211/211 each). CI not run (branch not pushed) |
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
All recorded in ADR-0005 (proposed). Summary: `seq` is the edit identity (no `id`); `EditSpec` vs
`Edit`; closed `EditParam` enum with typed values; `reset` op; `set` per parameter; linear undo with
kept branches; `dirty_regions` takes notes; `TimeMap` in `core/types`, `build_time_map` in
`core/timemap`; field renames for units; Phase 8 types deferred.

## Decisions a human must confirm (most expensive to change first)
1. `Edit` has no `id`; `seq` is the identity.
2. `set` replaces per parameter, not per category (the ROADMAP says "category").
3. Linear undo with superseded branches kept in `discarded()`.
4. Overlap policy (later note pushed rigidly vs spec §5.4 "stretch the neighbour"), absolute time
   handles, `min_segment` 1 ms, `k_min_time_scale` 1e-3.
5. Field renames and absolute cents for `f0`.
6. `EditSpec` vs `Edit` as the tool return type; `created_at` as an argument.
7. Match thresholds 0.8 / 0.5; neutral fade shape `linear`.

Until accepted: no `EditList` persistence format (Phase 7), no edits to `docs/specs/`.

## Open issues (notes from the audit)
`join()` in `build_time_map.cpp` takes a bool (split when next touched); `StrongId` extraction (rule
of three); `DirtyParams::source_end` defaults to the origin; `match_notes` is note geometry living
in `core/edits` (move before Phase 5 adds a second user); re-run the dirty-vs-TimeMap contract test
whenever the repair policy changes.

## To close the phase
1. Human decides items 1–7 (edit the ADR, set it `accepted`, adjust code if any answer differs).
2. Add pointers in the specs per the ADR.
3. Set ROADMAP "Current phase" to 2; tag `phase-1`; push and check CI.
