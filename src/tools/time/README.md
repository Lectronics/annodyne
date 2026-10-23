# tools/time

The Time Tool's edits (features spec §11): `note_move_edit` (onset shift), `note_length_edit` (body length, the attack is protected), `note_handle_edit` and `note_handle_remove_edit` (time handles). Each returns the one `EditSpec` the gesture means (origin "time_tool", category time); none touches audio.

- Layer 5; links only `core/types`. Allowed includes: `core/types`, `core/edits`, `core/tuning`.
- A move is a shift relative to the analysed onset (op set: a second move replaces the first). A length is the body's output duration; `length_scale = body_length / (source_end - attack_end)`.
- Handles are absolute output anchors (ADR-0005 §16): moving a note after placing a handle leaves the handle where it was and `build_time_map` repairs the conflict (counted in `repaired_notes`); re-issue handles after a move, length or attack change.
- A time edit and a pitch/level/formant edit on the same (stretched) note do not render together yet (ADR-0023); stretches render for mono sources only.
- Errors: `invalid_argument` (unknown/inactive note, non-finite or non-positive values, no body, a handle outside the body or with a negative output), `out_of_range` (a scale below `k_min_time_scale`, checked first).

Design: `docs/design/phase5-time-tools.md`. Tests: `tests/unit/time_tools_test.cpp`.
