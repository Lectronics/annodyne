# core/edits

The non-destructive edit model (layer 2). Nothing here touches audio.

- `edit_list.h` — `EditList`: append-only history of one track's edits, cursor-based undo/redo,
  reset, `retargeted` for re-analysis. Superseded redo branches are kept in `discarded()`.
- `effective_state.h` — the fold: a note's edits applied in seq order onto neutral values.
- `effective_gap.h` — the same fold for a gap's `gap_gain` edits (ADR-0024); `detail/fold` is the
  accumulator both folds share.
- `dirty_regions.h` — source ranges that need re-rendering between two edit lists.
- `remap.h` — `match_notes` and `remap_after_reanalysis`: re-attach edits after re-analysis; nothing
  is dropped, unmatched edits are orphaned and reported.

Types (`Edit`, `EditSpec`, `EffectiveNote`, `TimeMap`, `Note`, ...) live in `core/types`.

References: architecture spec §4.4, §5; `docs/design/phase1-data-model.md`; ADR-0005 (proposed).
Known limitations: copying an `EditList` is O(n); `effective_state` is O(edits x targets) per note;
match thresholds are untuned (Phase 7).
