# Design note: the Leveling macro (Phase 6)

- **Status:** implemented, as built; decided autonomously during the owner's absence (2026-10-23), reversible (a new layer-5 target)
- **Spec:** features spec 8: gain_dB = (target - level) x amount per note (no compressor artifacts), and a within-note smoothing amount
- **Folder:** `src/macros/leveling` (links `core/types` only)

`leveling_edits(analysis, {between_notes, within_notes, target, max_gain, min_gain})` returns a `gain` edit (op set, origin "leveling") per active note with a finite loudness (`Note::loudness`, K-weighted, one value per note): `(target - loudness) x between_notes`, limited to +-`max_gain` (12 dB), skipped when smaller than `min_gain` (0.05 dB); the target is the mean loudness of the active notes unless given. `within_notes > 0` adds a `within_note_flatten` (op set) per active note (the renderer pulls each frame toward the note's mean: ADR-0028). Errors: `invalid_argument` for amounts outside 0..1, `max_gain` not > 0, a negative `min_gain`, a non-finite target. Rendering needs no engine (ADR-0028), so the macro works end to end today.

Not here: a loudness target in LUFS with the 400 ms gating of the standard (the analysis' per-note value is used as given); ducking sibilants and breaths separately (the `noise_gain_*` edits); linking several tracks (Phase 7+).
