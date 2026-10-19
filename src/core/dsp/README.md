# core/dsp

Signal maths reused by more than one module (layer 0). Phase 2 holds only the crossfade kernel;
STFT, windows and resampling arrive in Phase 3.

`crossfade_gains` / `crossfade`: equal-power, linear and log fades between two aligned signals,
computed in double and cast once. `crossfade` fades a *slice* of a longer ramp, so a caller that
renders a region cutting through a ramp uses the same formula as one that renders it whole.
Compiled with `-ffp-contract=off` so results are bit-reproducible on one build.

Known limitations: `log` never reaches zero (-60 dB floor); equal-power adds up to +3 dB at the
midpoint for correlated signals (use `linear` there; see ADR-0006 / design note phase 2).
