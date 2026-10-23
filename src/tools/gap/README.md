# tools/gap

The Gap Tool's edits: `gap_gain_edit` (set or change the gain of a gap's silence), `gap_mute_edit` (set to `k_gap_mute_db`) and `gap_unmute_edit` (reset of `gap_gain`). Each returns the one `EditSpec` the gesture means (origin "gap_tool", category `gap`, param `gap_gain`); none touches audio.

- Layer 5; links only `core/types`. Allowed includes: `core/types`, `core/edits`, `core/tuning`.
- The gate (`core/render`) scales a gap's `silence` regions only; tails, the guard before a note and every consonant or breath region are never touched (ADR-0024 amendment 1).
- Errors: `invalid_argument` (no segment timeline, an id that is not a gap of it, a non-finite gain, a set outside [-120 dB, +24 dB]).

Design: `docs/design/phase5-gaps-core.md`. Tests: `tests/unit/gap_tools_test.cpp`.
