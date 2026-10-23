# Phase 5: vibrato-preserving stretch (design note)

Status: draft for structure review, 2026-10-23. Amends ADR-0023 (stretch render). Owner requirement (2026-10-23, after listening to `layout_a_*`): stretching or squishing a note must not change its vibrato rate by more than 0.5 Hz, from any edit or chain of edits; depth should stay as close to the source as possible (unless the edit is a vibrato control).

## Problem (measured)
`stretch_span` re-times the whole pitch contour, so the vibrato rate scales with 1/slope. Measured on `gap_fricative.wav` (source vibrato 5.7 Hz, 37 cents; DFT of the f0 trace, drift removed): slope 0.6 gives 9.1 Hz, slope 1.4 gives 3.9 Hz, slope 2.0 gives 3.0 Hz. Depth stays 35-41 cents; a slower rate at the same depth is what sounds like "more depth". The README already listed this as a known limit.

## Method
Per voiced run, the output pitch contour is `slow(tau(p)) + vib(p)`:
- `slow` = `pitch_center + pitch_drift` of the note frames, re-timed as today (it is the part that should stretch with the note).
- `vib` = `pitch_mod`, **regenerated** at the source rate instead of re-timed.
- The audio grains are still taken from the analysis mark nearest the source time `tau(p)` (so the timbre, formants and noise stay those of the source); only the *synthesis spacing* follows the new contour: the period at output position `p` is the period of `slow + vib`. This is TD-PSOLA pitch scaling by `r(p) = f_out(p) / f_src(tau(p))`; the vibrato terms differ by at most `2 * depth` (about 75 cents, 4 %), well inside TD-PSOLA's range.

**Vibrato model.** Complex demodulation of `pitch_mod` at the run's vibrato rate `f_v` (DFT peak in 3..9 Hz over the run's source interval; none found, or depth under 5 cents: no regeneration, today's behaviour). A Hann window of 1.5 cycles gives amplitude `A(sigma)` and unwrapped phase `phi(sigma)` per frame. Output:

    vib(p) = A(tau(p)) * cos(Phi(p)),   Phi(p) = phi(tau(begin)) + (1 + eps) * integral over [begin, p] of 2*pi*f_v(tau(q)) dq

so the rate is the source's local rate and the amplitude is the source's, played at slope 1. `Phi` starts on the source phase at the run start.

**Joins.** Where the run continues voiced into the neighbouring slope-1 audio (`join_begin`, `join_end`: the source frame just outside the span is voiced) the vibrato phase must agree there or the pitch steps. At the begin it does (`Phi` starts on `phi(tau)`). At the end `eps` is chosen so `Phi(end)` equals `phi(tau(end))` modulo `2 pi`, rounding to the nearest whole cycle: the rate then differs from the source by at most `0.5 / L` Hz for a run of `L` seconds (under 0.5 Hz for `L >= 1 s`). When the run ends inside the span (a note end: silence follows), `eps = 0` and the rate is exactly the source's. A run shorter than one second with a join at both ends may exceed 0.5 Hz by up to `0.5/L - 0.5` Hz; this is reported in the README as the limit, not hidden.

**Determinism and range independence.** The vibrato clock is a function of the run (its begin, end, the notes' frames) only, never of the requested output range; a run is rendered whole per call as today. Same inputs, same samples.

**Chains of edits.** Every edit is a map from the source, so the vibrato is regenerated from the source's `pitch_mod` each time; there is no accumulation.

## Interface
- `core/synthesis/stretch/detail/vibrato.{h,cpp}` (new): `struct VibratoSource` (frame times in source samples, `pitch_mod` cents), `class VibratoClock` built from it and the run (`SpanGeometry`, `begin`, `end`, `join_begin`, `join_end`), method `double cents(double out_position) const` (0 when not present), `bool present() const`, `double rate_change_hz() const` (the `eps * f_v` actually applied, for tests). Pure; thread-safe; not real-time safe.
- `pitch_points` gains a sibling `slow_points` (centre + drift) and `vibrato_source`; `PeriodCurve` for the *analysis* stays on the full contour.
- `synthesise_voiced_run` takes the output pitch as a function `period_out(out_position)` built from `slow` and the clock; the first and last synthesis marks still sit on analysis marks.
- `StretchParams::preserve_vibrato` (enum `VibratoMode {preserve, retime}`, default `preserve`) so tests and the golden path can compare; hashed through `StretchParams` where it is already hashed.

## Errors and limits
No new error. Mono only, as before. No vibrato found: unchanged output (bit-identical to the old path). Real vibrato is not exactly sinusoidal and its amplitude coupling (tremolo) is not regenerated: the model carries the pitch only; the amplitude and timbre come from the grains. Plosives and the noise path are unchanged.

## Tests (first)
1. Synthetic voice with 5.5 Hz, 40 cents vibrato: after slope 0.6, 1.4, 2.0 and 3.0 the output's analysed vibrato rate is within 0.5 Hz of 5.5 and depth within 25 % (run at least 1 s long).
2. `VibratoClock`: with slope 1 the clock reproduces `pitch_mod` (within a few cents); `rate_change_hz` is 0 with no join at the end and at most `0.5/L` with one; continuity of `Phi` at the begin.
3. No vibrato (steady tone): output bit-identical between `preserve` and `retime`.
4. Chain: stretch by 1.5 then the same warp inverse composes through the source only (render of map `m` is independent of how it was reached); same samples for two ranges of one run.
5. Existing stretch tests with `retime`; the golden/audio scenarios re-measured (rate and depth reported in the audition INDEX).

## Verification
`audio-verifier` re-runs the four layout scenarios and reports the vibrato rate and depth of source and edited (script: DFT of the f0 trace). Human listening remains required; the objective bound is 0.5 Hz.
