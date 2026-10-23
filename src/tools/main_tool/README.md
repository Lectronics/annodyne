# tools/main_tool

The Main Tool's pitch drag: `pitch_drag_edit(analysis, PitchDrag{note, target})` returns the one
`EditSpec` (origin "main_tool", category pitch, op set, param `center_offset`) that puts the note's
pitch target at `target`. It never touches audio and never snaps: the caller snaps with
`core/tuning::target_pitch` and passes the final target.

- Layer 5; links only `core/types`. Allowed includes: `core/types`, `core/edits`, `core/tuning`.
  Never `dsp`, `synthesis` or `render`.
- Baseline: the analysed `pitch_target.target` (not `pitch_center`), so a second drag replaces the
  first and lands on its own target. The offset is `target - pitch_target.target`.
- Glide and ornament notes are accepted; the edit is one uniform offset (the start of a glide and
  the other pitch of an ornament follow by the same amount; the engines shift the whole curve).
- Errors: `invalid_argument` (non-finite target, unknown or inactive note, kind multi or unpitched,
  a malformed note), `out_of_range` (|offset| above `k_max_pitch_offset`, ADR-0016). The result has
  passed `validate_for_add`.
- Pure; thread-safe; not real-time safe (allocates the spec).

Design: `docs/design/phase4-pitch-edit.md` (R1, R2, R3, R12); ADR-0016. Tests:
`tests/unit/main_tool_test.cpp`.
