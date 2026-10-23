# Design note: scale grids and the Correct Pitch macro (Phase 6)

- **Status:** implemented, as built; decided autonomously during the owner's absence (2026-10-23), reversible (two new targets and no change to an existing contract)
- **Folders / targets:** `src/core/tuning` (`scale_grid.{h,cpp}`, layer 1), `src/macros/correct_pitch` (layer 5)
- **Spec:** features spec 5d (Pitch Center and Pitch Drift sliders, a minimum deviation, a scale target); ROADMAP Phase 6 (`macros/correct_pitch`, `core/tuning`)

## Scale grids
`ScaleGrid {a4_reference, root (pitch class, C = 0), degrees}` and `target_pitch(Cents, const ScaleGrid&) -> Result<Cents>`: the nearest scale pitch on the equal-tempered grid of the reference, ties toward +infinity (as `ChromaticGrid`); a chromatic scale equals the chromatic grid. `ScaleType` names chromatic, major, natural/harmonic minor, major/minor pentatonic, dorian, mixolydian; custom degree sets are allowed. A chord grid is a `ScaleGrid` with the chord tones as degrees (the Chord Track is Phase 8). Errors: `invalid_argument` for a non-finite pitch, a bad reference, a root outside 0..11, degrees empty/unsorted/outside 0..11. Pure; thread-safe; real-time safe.

## Correct Pitch
`correct_pitch_edits(analysis, {grid, center_amount, drift_amount, min_deviation})` returns, in note order, one `center_offset` (op set, origin "correct_pitch") per active note with a single pitch target (kinds multi and unpitched are skipped) whose distance to its grid pitch is at least `min_deviation` and not zero: `offset = (grid pitch - target) x center_amount`. Moving the note's centre keeps vibrato and scoops (features spec). `drift_amount > 0` adds a `drift_scale` (op set, `1 - amount`) per such note; **no engine realises `drift_scale` yet**, so rendering it is `not_implemented`; the default is 0. Errors: `invalid_argument` (bad grid, amounts outside 0..1, negative `min_deviation`), `out_of_range` (an offset above `k_max_pitch_offset`).

## Not here
- `pitch_target_mode` and ADR-0015 (how a glide or ornament note is read): the macro uses `pitch_target.target`, the Main Tool's baseline. Decided when a listening test shows the need.
- Glides between notes (continuous curves, `docs/design/pitch-glide-transitions.md`): a corrected note is a static offset, so neighbours step; the engines take one number per note.
- The CLI and scenario files cannot call the macro yet (only `pitch_drag` tools).

## Tests
`scale_grid_test` (degrees, snapping in C major / D major / A minor / C minor, ties, chromatic equals the chromatic grid on three references, errors) and `correct_pitch_test` (offsets, amount, min deviation, folding into the effective state, drift specs, errors, inactive notes).
