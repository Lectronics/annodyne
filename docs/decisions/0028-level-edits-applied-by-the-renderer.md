# ADR-0028: level edits (gain, fades, within-note flattening) are applied by the renderer

- **Status:** accepted (decided autonomously during the owner's absence, 2026-10-23; reversible: the engines can be made to realise level again by passing `e.eff` unchanged in `render_zone`)
- **Phase:** 6
- **Related:** ADR-0006 (render API, blend zones), ADR-0016 (Phase 4 engines realise `center` only), `docs/design/phase6-level.md`

## Context
The Phase 4 engines refuse every edit but `center` (and `onset_shift`); the only thing that ever applied a gain was the test engine `GainTestEngine`. Level is the simplest edit (a gain curve) and does not depend on how pitch is synthesised, so every engine would repeat the same code.

## Decision
`render_zone` shows the engine the **pitch part** of the effective state (`without_level(eff)`: gain, flatten and fades neutral) and then multiplies the engine's zone by one gain curve (`core/render/level.h`: `level_gain`, `apply_gain`). A note with only level edits needs no engine: its zone is the source through the map, with the curve applied. The curve is `gain x fade_in x fade_out x flatten`: the fades run from the note's edges (shapes `linear`, `equal_power`, `log` = 60 dB), fades longer than the note shrink in proportion, flattening moves each frame toward the note's mean level by `within_note_flatten` (clamped to +-12 dB, frames 30 dB below the mean left alone). Samples in the zone's margins hold the gain of the nearest note edge; the renderer's ramp (ADR-0006) is the fade to the unedited audio.

## Consequences
- Gain edits now change the output with any engine, including `PassthroughEngine`; the contract test "the passthrough engine renders the source whatever the edits" became "whatever the pitch edits", and a new contract test checks that a gain edit leaves samples outside every note zone bit-identical.
- A level edit on a note whose zone meets a stretched or cut part of the map is still `not_implemented` (the existing check in `make_plan`).
- `GainTestEngine` is no longer reached for gain edits; it stays for the tests that use it as an engine stand-in.
- Not done: gain applied per frame class (sibilants and breaths separately: the `noise_gain_*` edits, ADR-0018), loudness-based leveling (the macro), and the zone's gain at a note that touches another.
