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
7. `add` after `undo` moves the redo tail to `EditList::discarded()` (linear undo, nothing deleted).
   `next_seq_` is an explicit member, so seqs are never reused and the persisted log stays
   append-only.
8. `dirty_regions` takes the note list as well as the two edit lists.
9. `EffectiveNote` gains `onset_shift`, `length_scale` and `handles`, and lacks
   `harmonic_gains_db` until Phase 12.
10. `AnalysisResult` omits `beats`, `tempo`, `key`, `chords` until Phase 8 (amends ROADMAP "all
    types from §3").
11. `TimeMap` (the pure mapping) lives in `core/types` so layer-2 synthesis can use it;
    `build_time_map` lives in `core/timemap`, not `core/edits` as spec §4.4 lists it. Anchors and
    time handles share one `TimePair` type; the builder returns a `TimeMapBuild` with a repair count.
12. Offsets accept `set`/`add`/`reset`; multipliers `set`/`scale`/`reset` (not all three on every
    parameter as spec §5.1 reads). `within_note_flatten` is an amount: `set`/`reset` only, neutral 0.
    The fold clamps to legal ranges and snaps float dust to neutral (`k_neutral_snap_*`).
13. Tools and macros return `EditSpec` groups, not `Edit` (spec §5.1, CLAUDE.md rule 2 wording).
14. Categories `separation`, `assignment`, `sound` exist without parameters on purpose (spec §5.1
    names them; their parameters arrive in Phases 5, 7, 12).

15. Signatures that differ from spec §4.4/§4.6:
    - `remap_after_reanalysis` returns `Result<RemapResult>` (the re-attached `EditList` plus a
      report with one entry per old note id: kept, split, merged_kept, merged_away, orphaned), not
      a bare `EditList`. Edits whose notes cannot be re-attached are kept with the ids in
      `EditTarget::orphaned_note_ids`; a later mapping that makes such an id live again removes it
      from the orphan list.
    - `build_time_map(notes, edits, TimeMapParams) -> Result<TimeMapBuild>`.
    - `dirty_regions(notes, old, new, DirtyParams)`; it is content-dirty and deliberately coarse for
      timing edits (previous active note's start to the end of the source), verified against the
      TimeMap by a contract test.
16. TimeMap policy choices that need a human (see design note, Open questions 1 and 6): the later
    note is pushed rigidly on overlap (spec §5.4 step 5 says to stretch the neighbour's boundary
    region), time handles are absolute output anchors, handles outside a note's body are ignored,
    `min_segment` = 1 ms, `k_min_time_scale` = 1e-3.
17. Naming. Units go in the type or name (ADR-0003), so spec field names changed:
    `loudness_lu` → `loudness` (LUFS), `amp_db` → `amp`, `confidence` → `confidence_prob`,
    `tuning_ref_hz` → `tuning_ref`, `f0_cents` → `f0` (absolute cents, A4 = 6900),
    `center_offset_cents` → `center_offset`, `in_time_scale`/`out_time_scale` →
    `transition_in_scale`/`transition_out_scale`, `gain_db` → `gain`, `noise_gain_db[class]` → four
    `noise_gain_*` parameters, `shape` → `fade_shape`, `harmonic freq/amp` → `freq_hz`/`amp_linear`.
    `Note` gains `grid` (the spec leaves the curves' grid implicit).
18. Public additions not in the spec: `validate(...)` for every track/Note/AnalysisResult/EditSpec,
    `validate_for_add`, `find_note`, `frame_time`, `match_notes`, `NoteMatching::id_map`,
    `EditListParts`/`EditList::to_parts`/`from_parts`, `param_info`, `noise_gain(NoiseGains, FrameClass)`.

## Alternatives considered
- Keep `param: str`. Rejected: units and ops cannot be checked, typos pass silently.
- One `Edit` struct with `seq` filled in by the list. Rejected: a tool could forge sequence numbers.
- Keep dead redo branches as redoable branches (a tree). Rejected for Phase 1; they are kept as
  inert history in `discarded()`.
- `set` per category. Rejected: a gain `set` would erase a within-note-flatten `set`.

## Consequences
- Adding a parameter is one enum value plus one table row.
- Phases 5, 7 and 12 add parameters for `separation`, `assignment`, `sound`.
- Tools never see `seq`; macros "one group = one undo step" is `EditList::add(group)`.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §3 (`Note`, `AnalysisResult`), §4.4 (`dirty_regions`,
`EditList`) and §5.1 (`Edit`, `EditTarget`) get a pointer to this ADR once it is accepted.

## Decisions a human must confirm
In order of how expensive they are to change after Phase 7 persistence exists:
1. `Edit` has no `id`; `seq` is the identity (§1).
2. `set` is per parameter, not per category (§6).
3. Linear undo with superseded branches in `discarded()` (§7).
4. Overlap policy and absolute handles (§16).
5. Renames (§17) and absolute cents for `f0`.
6. `EditSpec` vs `Edit`, `created_at` as an argument (§2).
7. Match thresholds (0.8 / 0.5), neutral fade shape `linear`.

## Provisional status
Accepted by nobody yet. Until a human accepts it: no `EditList` persistence format (Phase 7), no
edits to `docs/specs/`, and the Phase 1 exit gate is conditional. Decisions 1, 6 and 7 (seq as
identity, per-parameter `set`, `discarded()` history) are the ones that must not be frozen without a
human. Product policies left open: absolute time handles, and pushing a neighbour on overlap.
