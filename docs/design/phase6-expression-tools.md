# Design note: the expression tools (Phase 6)

- **Status:** implemented, as built; decided autonomously during the owner's absence (2026-10-23), reversible (new layer-5 targets only)
- **Folders:** `src/tools/pitch_modulation`, `src/tools/pitch_drift`, `src/tools/formant` (layer 5; link `core/types` only), `tools/amplitude`, `tools/fade` (`phase6-level.md`)

## What they do
Each reads the analysis (to find an active note) and returns one `EditSpec` (op set, origin `<tool>_tool`): `modulation_edit(analysis, note, Ratio)` -> `mod_scale` (1 as analysed, 0 flat, 2 twice as deep, negative inverted); `drift_edit` -> `drift_scale` (1 as analysed, 0 straightened); `formant_edit(analysis, note, Cents)` -> `formant_offset` (envelope shift, pitch unchanged). A neutral value folds to no edit. Errors: `invalid_argument` (unknown or inactive note, non-finite or out-of-range value). Pure; thread-safe; not real-time safe.

## Rendering
`RubberBandPitchEngine` (`--engine rubberband`) realises all three through Rubber Band's live shifter (ADR-0027 amendment 1): vibrato and drift terms scaled per frame, the formant envelope by `setFormantScale`. PSOLA and the harmonic engine refuse them (`not_implemented`), as before. Measured: `mod_scale` 0 leaves under a quarter of the vibrato (unit test); `formant_offset` +-300 cents moves the cepstral envelope by about +250..+400 / -310..-340 cents on `vocadito_2` (`audition/phase6-formant`, crude cepstral measure; unlistened).

## Not here
Continuous (time-varying) amounts, glides between notes, the `sibilant` tool (`noise_gain_*`), and the transitions. The edit parameters exist; the engines take one number per note for centre and formant.
