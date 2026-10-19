# ADR-0005: Edit model refinements

- **Date:** 2026-10-19
- **Status:** proposed (written while no human was available; confirm or change at structure audit #1)
- **Phase:** 1

## Context
Spec §3 and §5.1 give the edit model in Python-style pseudocode. Turning it into typed C++ (CLAUDE.md
rules 3 and 4) needed decisions the spec does not make: `param: str` and `value: float | Curve` carry
no units; tools cannot know a `seq`; reset is described as an edit but is not an `op`; "append-only"
conflicts with "undo, then add"; `dirty_regions` cannot compute ranges from edits alone.
Full reasoning: `docs/design/phase1-data-model.md`, "Decisions that go beyond the spec".

## Decision
1. `Edit` has no `id`. `seq` is the identity (unique, never reused). `GroupId` is the first `seq` of
   its group.
2. Tools and macros produce `EditSpec`; only `EditList` creates `Edit` (assigns `seq`, `group`, and
   takes `created_at` as a parameter, so the list is pure).
3. `param` is a closed `EditParam` enum; `value` is a typed `EditValue` variant checked against a
   per-parameter table (`ValueKind`, allowed ops, neutral, range). The `Curve` type is not added
   until a parameter needs it.
4. `EditOp::reset` is added. A reset edit clears one parameter, or a whole category when `param` is
   empty, from that point in `seq` order. History is kept.
5. `EditTarget` = `note_ids` + `orphaned_note_ids`. No `source_range` (time handles carry their own
   `SourceTime`) and no `track_id` (an `EditList` is per track).
6. `set` replaces earlier edits of the same **parameter**.
7. `add` after `undo` discards the redo tail (linear undo). Persistence (Phase 7) logs undo/redo as
   events, so the on-disk log stays append-only.
8. `dirty_regions` takes the note list as well as the two edit lists.
9. `EffectiveNote` gains `center_offset`, `onset_shift`, `length_scale` and `handles`, and lacks
   `harmonic_gains_db` until Phase 12.
10. `AnalysisResult` omits `beats`, `tempo`, `key`, `chords` until Phase 8.

## Alternatives considered
- Keep `param: str`. Rejected: units and ops cannot be checked, typos pass silently.
- One `Edit` struct with `seq` filled in by the list. Rejected: a tool could forge sequence numbers.
- Keep dead redo branches in the log. Rejected for Phase 1: cursor semantics become a tree.
- `set` per category. Rejected: a gain `set` would erase a within-note-flatten `set`.

## Consequences
- Adding a parameter is one enum value plus one table row.
- Phases 5, 7 and 12 add parameters for `separation`, `assignment`, `sound`.
- Tools never see `seq`; macros "one group = one undo step" is `EditList::add(group)`.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §3 (`Note`, `AnalysisResult`), §4.4 (`dirty_regions`,
`EditList`) and §5.1 (`Edit`, `EditTarget`) get a pointer to this ADR once it is accepted.
