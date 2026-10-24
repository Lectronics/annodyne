# ADR-0027: Rubber Band pitch scaling as a candidate pitch-edit engine (A/B only)

- **Status:** accepted for the A/B (decided autonomously during the owner's absence, 2026-10-23; reversible: delete the folder and the CLI choice). The pitch-edit engine decision itself stays with the owner (ROADMAP Phase 6/7 item).
- **Phase:** 6
- **Related:** ADR-0026 (Rubber Band vendored), ADR-0021 (default pitch engine), ADR-0019 (SPTK marks), CLAUDE.md rule 9.

## Decision
Add `core/synthesis/rubberband_pitch` (`RubberBandPitchEngine`, a `SynthesisEngine`) so Rubber Band R3's pitch scaling (formant-preserving by default, offline, one thread, any channel count) can be A/B-compared with `PsolaEngine` and `HarmonicNoiseEngine` on the same scenarios: CLI `--engine rubberband`. It realises `center` (and `onset_shift` through the TimeMap) and refuses everything else with `not_implemented`, as the Phase 4 engines do. The whole zone is shifted (voiced and unvoiced alike, including the margin beyond the note, which the renderer fades). No default changes: `psola` stays the default pitch engine (ADR-0021).

## Consequences
- `anno_rubberband` may be linked by `core/synthesis/rubberband_pitch` as well as `core/synthesis/stretch` (AnnoLayers allowlist).
- First numbers (`audition/phase6-pitch-engine/INDEX.md`): no gain in the Phase 4 objective rows on the real voice (pitch round trip off by 3.5 and 14 cents at +200/+700); the decision needs listening.
- Not realised: continuous (time-varying) pitch curves; R3's real-time mode with a changing pitch scale is the way to try that, not offline mode.

## Amendment 1 (2026-10-23): time-varying pitch by the live shifter (modulation and drift scales)
`RubberBandPitchEngine` now also realises `mod_scale` and `drift_scale` (vibrato and drift control, ROADMAP Phase 6 tools `pitch_modulation`, `pitch_drift`) with Rubber Band's **live shifter** (short window, formants preserved, channels together): the pitch change through the zone is the centre offset plus `(scale - 1)` x the note's `pitch_mod` / `pitch_drift` (cents, per frame, zero outside the note), applied as a pitch scale per shifter block. Measured on a 220 Hz voice with a 5.5 Hz, 40 cent vibrato: the shifter applies a scale about 30 ms early and follows it with a gain of about 0.67, so the engine reads the curve 30 ms behind (`curve_lead` -0.030 s) and multiplies the terms by 1.5 (`curve_gain`); `mod_scale` 0 leaves under a quarter of the vibrato depth (tested; the analysed depth of a flat tone is 0.2 cents), and 0.5, 1.5 and 2 land within 0.2-0.35 of the requested ratio. **Limits:** the compensation is calibrated at 5.5 Hz (the live shifter smooths the scale, so faster vibrato is followed less); a pitch step still shows as a ramp over the shifter's window; quality of this path is unlistened. The offline path (static offset) is unchanged.

## Amendment 2 (2026-10-23): formant offset
`formant_offset` is realised on the same live-shifter path with `setFormantScale(2^(cents/1200))` (pitch unchanged). Measured with a crude cepstral envelope alignment on `vocadito_2` (`audition/phase6-formant`): +300 cents moves the envelope by +250..+400 cents, -300 by -310..-340; no unit test (the synthetic voice has no formant structure); unlistened. A combined pitch and formant edit uses the library's convention (the formant scale is relative to the source); not measured.

## Amendment 3 (2026-10-23): a constant formant offset uses the offline stretcher
The owner heard hitches in the whole-file formant shift (live shifter, short window). A constant pitch and formant offset now go through the offline R3 stretcher with `setFormantScale` (standard window); the live shifter stays for time-varying curves (modulation and drift scales, glides). Measured on `vocadito_2`, +-300 cents on every note: the cepstral envelope shifts by +240..+404 / -262..-337 cents in both paths (similar accuracy), the third-difference outliers equal the source's own (no new sample-level discontinuity), so the artifact the owner hears is not one these measures see; the offline files replace the live ones in `audition/phase6-formant-file` for a new listening.
