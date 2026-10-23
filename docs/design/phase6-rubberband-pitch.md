# Design note: RubberBandPitchEngine (Phase 6, A/B candidate)

- **Status:** implemented, as built (ADR-0027)
- **Folder / target:** `src/core/synthesis/rubberband_pitch` (layer 2; links `core/synthesis/engine`, `core/types`; `anno_rubberband` privately)

## Inputs, outputs, errors
`render_note(src, note, eff, time_map, out_range)` as `SynthesisEngine` (ADR-0002): `out_range` is the renderer's blend zone on the output axis; the result has exactly its samples, the source's channel count and rate. `eff.center - note.pitch_center` (cents) is the shift; within `k_neutral_snap_offset` the original samples come back exactly. Units: cents in, samples out; time axis: output (through the map; the zone must be one slope-1 warp with a whole-sample shift, `zone_shift`).
Errors: `invalid_argument` (bad params, negative range, non-finite center), `not_implemented` (other edits, inactive note, |shift| > `max_shift` 2400 cents, a zone that is not slope 1). Thread safety: const, one stretcher per call; not real-time safe.

## Method
Read the zone through the map plus `context` (100 ms) on each side, run Rubber Band R3 offline with time ratio 1 and pitch scale 2^(shift/1200), `OptionFormantPreserved` unless `PitchFormants::shifted`, one thread, channels together, silent logger; cut the middle `zone` samples. Context keeps the stretcher's start-up ramp outside the zone (learned in ADR-0026's stretch work).

## Tests
Identity and key, neutral offset exact, pitch of 220 Hz shifted by -400/+200/+700 cents within 1 %, deterministic, refuses gain/inactive/too-large shifts/bad params, stereo coherence (correlation < -0.98).

## Limits
Everything in the zone is shifted (no class routing); no time-varying pitch; the Phase 4 objective rows are not better than PSOLA's on the real voice.

## Time-varying curves (ADR-0027 amendment 1)
`mod_scale` and `drift_scale` other than 1 switch to `RubberBandLiveShifter`: pitch change per sample = centre offset + (scale - 1) x gain x the note's modulation/drift (cents, linear between frame centres, zero outside the note), one pitch scale per shifter block read `curve_lead` (-30 ms) behind the block centre, start delay discarded and a zero tail flushed so the result is exactly zone-long. Tests: vibrato depth at scales 0, 0.5, 1.5, 2; a drift scale renders; bad `curve_gain`.
