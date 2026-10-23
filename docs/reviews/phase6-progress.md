# Phase 6 progress (expression tools and macros): autonomous stretch, 2026-10-23

Written while the owner was away; nothing here is a phase close-out. Branch `phase-6` (from `phase-5`); no push, PR or tag has been made for it. Each slice is its own commit; every decision below is reversible (a new folder, an optional engine, or a default that is one line).

## Done
| Slice | What | Records |
|---|---|---|
| Pitch-engine A/B | `RubberBandPitchEngine` (`--engine rubberband`): Rubber Band R3 offline pitch scaling, formants preserved, any channel count | ADR-0027, `phase6-rubberband-pitch.md`, `audition/phase6-pitch-engine/INDEX.md` |
| Level | gain, fades and within-note flattening applied by the renderer as one gain curve; engines see the pitch part only; works with any engine | ADR-0028, `phase6-level.md`, `tools/amplitude`, `tools/fade`, `audition/phase6-level` |
| Vibrato, drift, formant | `mod_scale`, `drift_scale`, `formant_offset` realised by the live shifter (calibrated lead -30 ms, gain 1.5); tools `pitch_modulation`, `pitch_drift`, `formant` | ADR-0027 amendments 1-2, `phase6-expression-tools.md`, `audition/phase6-formant` |
| Scales and Correct Pitch | `ScaleGrid`, `target_pitch(Cents, ScaleGrid)`, `macros/correct_pitch` (centre offset toward a scale, min deviation, optional drift scale) | `phase6-correct-pitch.md` |
| Leveling | `macros/leveling` (gain toward the mean loudness, within-note flatten) | `phase6-leveling.md` |
| Sibilant | per-class noise gains (sibilant, breath, plosive) in the renderer gain curve; `tools/sibilant` | ADR-0028 amendment 1 |
| CLI | scenario key `macros` (correct_pitch, leveling) for end-to-end demos | `audition/phase6-macros` |

## After the owner's listening round (2026-10-23)
- Rubber Band is the default pitch engine (CLI and ADR-0029); PSOLA stays available. A programmatic choice between them is not built: the objective measures do not separate the engines note by note, so it waits for per-note listening labels (`audition/phase6-macros-rb/NOTES.md`).
- Demos asked for: `audition/phase6-fade` (1.2 s fade up, 0.64 s fade down, then a cut back to full level), `audition/phase6-formant-file` (formant +-300 cents on every note of the file; scenario macro `formant`).
- Hitches in the Rubber Band Correct Pitch render at 4.322 s and 6.61-6.63 s traced to the boundary between two corrected notes with different offsets; touching notes now glide (ADR-0030, `TouchingNotes`, key version 8). 4.322 s is clear of cycle-similarity dips; 6.61-6.63 s is not (both notes are breathy): next step is shifting voiced frames only and/or a longer glide.

## Tests
debug-asan: 1023/1023 on the build before the sibilant slice and the CI fixes (the later slices and fixes were run as unit tests: `level_test`, `expression_tools_test`, `stretch_span_test`, `scenario_test`; clang-tidy and `check_includes` clean). PR #4 CI: see the PR.

## Measured, not listened
- Rubber Band against PSOLA for pitch edits (`audition/phase6-pitch-engine`): no clear objective gain on the single real-voice notes of Phase 4; on a whole-song Correct Pitch (`audition/phase6-macros`, 49 notes edited) PSOLA leaves 5-6 click detections and Rubber Band 0, and Rubber Band's pitch round trip is within 0.5 cents on most notes.
- Level: +6 dB lands at 6.00 dB; a fade, mute and flatten change only the note and its 10 ms margins (`audition/phase6-level`).
- `mod_scale` 0 leaves under a quarter of the vibrato depth; scales 0.5, 1.5, 2 land within 0.2-0.35 of the ratio (unit test, synthetic 5.5 Hz vibrato). Formant +-300 cents moves the cepstral envelope by about +250..+400 / -310..-340 cents.

## Needs the owner
- Listen: `audition/phase6-pitch-engine`, `phase6-level`, `phase6-formant`, `phase6-macros`, `phase6-macros-rb` (and decide the pitch-edit engine, ROADMAP Phase 6/7).
- `pitch_target_mode` / ADR-0015 (how Correct Pitch reads a glide or ornament note) and glides between notes (`pitch-glide-transitions.md`) are not started.
- The drift correction (`drift_amount`) is realised only by the Rubber Band engine.

## Not started
`tools/pitch` transitions and continuous curves (needs an `AmountCurve` type and an ADR), the voiced noise gain, the breath chain and macros, and the ADR-0019 SPTK trial.
