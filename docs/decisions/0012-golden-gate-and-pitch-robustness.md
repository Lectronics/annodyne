# ADR-0012: Golden gate wording and pitch-robustness changes

- **Date:** 2026-10-19
- **Status:** accepted 2026-10-19 (owner: keep chasing onset F > 0.9; no octave errors allowed)
- **Phase:** 3

## Context
The ROADMAP gate "onset F >= 0.9, no octave errors" could not be met on the Vocadito vocals: the two human annotators agree at only ~0.75 F at +-20 ms. The owner reviewed the plots of tracks 13 and 28 and found a third-harmonic error (a note an octave and a fifth too high), missed low staccato notes, long scoops split into two notes, and a note placed before the transient it belongs to.

## Decision
1. **The goal stands:** onset F > 0.9 and no octave errors remain the target and will be pursued through the project. The automated golden test is a **regression guard** whose thresholds rise with each measured improvement (`tests/tolerances.h`); it is not a claim that the goal is met.
2. **Octave errors count strictly:** the metric counts an estimate within 50 cents of +-1200, +-1902 (third harmonic) or +-2400 cents of the reference as an octave/harmonic error. Octave errors are no longer treated as acceptable.
3. **Subharmonic check** (`PYinParams::subharmonic_margin`, `subharmonic_margin2`): when the strongest candidate has a CMND dip at 3x (2x) its lag that is deeper by more than 0.1 (0.05), its mass moves there. Fixes weak-fundamental frames tracked at a harmonic.
4. **Stay relief** (`PYinParams::stay_relief` 0.5): half of the cost of a voiced-to-voiced step is removed. The smooth pitch transition spreads probability over +-80 cents, which cost every voiced frame ~2.2 nats that an unvoiced frame did not pay, so short or quiet voiced stretches (staccato, low notes) lost to unvoiced.
5. `PYinEstimator::version()` becomes 4.

## Measured (35 tuning tracks / 5 held-out), before -> after
Voicing recall 0.905 / 0.916 -> 0.959 / 0.967; pitch accuracy 0.873 / 0.856 -> 0.943 / 0.948; octave+harmonic errors 0.028 / 0.056 -> 0.010 / 0.013; onset F at +-20 ms (annotator 1) 0.494 / 0.389 -> 0.531 / 0.439. False-voiced frames rise 0.07 -> 0.13 (mostly annotation boundary slop).

## Alternatives considered
- Raising `beta_mean` further: more recall but octave errors rise (0.058 at 0.5).
- A longer YIN window for low voices: recall and onset F fell.
- Checking 4x lags: more octave-down errors.

## Open (next tuning round)
Scoops split into two notes, transient-weighted note starts, staccato notes below ~90 ms, the remaining octave errors on track 2 (high voice), and the consonant start convention.

## Spec updates
None; the specs are unchanged. ROADMAP Phase 3 gate wording is read as above.
