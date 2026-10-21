# core/synthesis/harmonic_noise

`HarmonicNoiseEngine`: the Phase 4 pitch-shift engine (ADR-0016, ADR-0017). Design:
`docs/design/phase4-pitch-edit.md`.

**Method.** `out = x - (1 - cos(pi u / 2)) H_original + sin(pi u / 2) H_shifted`. `x` is the original
zone, `H_original` the additive resynthesis of the note's measured harmonics (McAulay-Quatieri
cubic phase between analysis frames), `H_shifted` the same harmonics at `ratio * frequency` with
amplitudes re-read from the note's envelope at the new frequency (formants stay), and `u` the
weight: 1 on shifted-class frames that have a measured harmonic, 0 elsewhere, raised-cosine edges of
`class_edge_fade` (4 ms). The residual `x - H_original` (breath, rasp, consonant noise) is never
moved, and where `u = 0` the original samples are copied without evaluating the formula.

**Why subtract the model instead of synthesising from scratch.** Everything the harmonic model does
not explain stays exactly where it was, including the sound classes the research says must not be
pitch-shifted, and the output is bit-identical outside shifted runs.

**Measured (unit tests, 44.1 kHz).** Re-detected note pitch within 3 cents of the request (median
over the note) for +200, -300 and +37 cents; the worst single frame is under 6 cents (it was 3.2-4
cents: the original partial is cancelled to about 1 % of its level and beats against the shifted
one). Vibrato extent within 10 % after +400 cents; first formant within 5 % after +700 cents; the
original partial left over after a +1200 cent shift is at -46 dB. Pitch error of the ideal shifted
signal measured the same way is 0.2 cent, so the remaining error is the cancellation, not the
measurement.

**Limits.** Mono only. Only the pitch offset (and timing through the time map) is realised; any other
edit is `not_implemented`. A breathy frame shifts all its measured harmonics (the "below the maximum
voiced frequency" clause of ADR-0017 needs MVF stored per frame). The class-boundary fade is not
aligned to a pitch period yet (research: 2-5 ms, aligned when voiced follows). Harmonic phases
integrate the shifted frequencies from the measured phase at each run start, so the waveform shape of
a long shifted run drifts from the original's (no claim about how that sounds: listening gate #1).
Shifts above the 0.40-0.45 sample-rate band taper away smoothly. Cost: about 0.1 s per second of
note at 60 harmonics (offline).
