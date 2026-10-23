# ADR-0029: Rubber Band is the default pitch engine; PSOLA is kept as a supplement

- **Status:** accepted (owner, 2026-10-23, after listening to the Phase 6 packages: "I like the rubberband pitch engine, but occasionally the psola does sound better. I'd like to default to rubberband, but keep psola to supplement if we find a way to programmatically determine when psola is better.")
- **Phase:** 6
- **Supersedes:** ADR-0021 decision 1 and 3 (PSOLA as the pitch engine; the CLI default `passthrough`)

## Decision
1. The CLI default engine is `rubberband` (`RubberBandPitchEngine`, ADR-0027). `psola`, `harmonic_noise` and `passthrough` stay selectable with `--engine`.
2. PSOLA stays in the product as the supplement. A programmatic choice between the two (`--engine auto`) is a candidate under evaluation (see the Phase 6 progress note); until it is accepted by listening, the choice is explicit.
3. Rubber Band's engine refuses nothing PSOLA accepts except where its own limits apply (|shift| above 2400 cents); PSOLA's limits (mono, 700 cents) are unchanged.

## Consequences
- Scenarios that need the old behaviour pass `--engine psola`/`passthrough` (the CLI tests that relied on the passthrough default are unaffected; the suite passes).
- Known weak spots recorded by the owner's listening: at note onsets Rubber Band's phase vocoder can change the waveform for a few tens of milliseconds even for a small shift (e.g. `vocadito_2` at 4.322 s and near 6.61-6.63 s in `macro_correct_pitch_chromatic`); PSOLA keeps the waveform there but clicks in whole-song corrections.
