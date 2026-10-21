# core/analysis/envelope

`estimate_envelope`: the smooth spectral envelope (formants) of each frame of a note, from its
harmonic peaks, by True Envelope (Röbel & Rodet, DAFx-2005; implementation guide B7). Used by the
Phase 4 engines to re-read harmonic amplitudes after a pitch shift (ADR-0016 item 2). Design:
`docs/design/phase4-pitch-edit.md`.

**Method.** Per frame, the peak levels (dB) of the measured harmonics are interpolated linearly in
frequency onto a 513-point grid (0..rate/2) and held flat outside the first and last harmonic.
The True Envelope loop then repeats: V = cepstral low-pass of A; A = max(A, V), until no peak is
more than `tolerance` (0.2 dB) above V. The cepstral order is floor(rate / (2 * highest f0)), the
IRCAM rule, so a high voice gets a smoother envelope that cannot weave between its widely spaced
harmonics. A frame without a measured harmonic is the floor level (-120 dB).

**Measured (unit tests, synthetic source-filter signals at f0 = 150, 220, 400 Hz).** The envelope
lies on or above every harmonic peak (within 1 dB of the grid lookup); the shape follows the true
two-formant curve within 2 dB at the harmonics; the first formant peak is within 35 Hz of 700 Hz.

**Limits.** The frequency grid (43 Hz at 44.1 kHz) is coarser than a sharp formant, so a lookup
between grid points can sit about 1 dB under a peak: engines interpolate between points. Above the
last measured harmonic the envelope is flat (extrapolated), not measured. There is no smoothing
over time yet (B7: smooth a little, never across phoneme boundaries); frames are independent.
Tested on synthetic signals only; real voices are measured at stage 4.
