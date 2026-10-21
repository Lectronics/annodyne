# core/analysis/partials

`analyse_partials`: per-frame harmonic tracks (frequency, amplitude, phase) of one note, guided by
the note's own f0 curve (a harmonic sieve). Design: `docs/design/phase4-pitch-edit.md`; ADR-0016.

**Method.** For every voiced frame a Blackman-Harris window of `window_periods` (4) periods of
f0 is centred on the frame, zero-phase, zero-padded 5x. Harmonic k is the largest FFT peak within
+-40 cents (at least one bin each side) of k * f0, refined by quadratic interpolation of the log
magnitude (QIFFT; Smith, SASP "Optimal Peak Finding"). Amplitude is `2 * peak / sum(window)`; the
phase is the cosine phase at the frame centre, so `x(t_i) ~ sum amp_k cos(phase_k)`.

**Why Blackman-Harris at four periods.** Hann at four periods leaked a neighbouring harmonic into
the peak: 3.8 cent worst error on a steady tone. Blackman-Harris puts the neighbour at the edge of
its main lobe and its -92 dB sidelobes leave 0.2 cent. Three periods cannot resolve harmonics, so
`window_periods` is 4..8.

**Measured (unit tests, 44.1 kHz, harmonic tones with 1/k amplitudes).** Steady tones: worst
0.2 cent, amplitude within 3 %. Vibrato (5.5 Hz, +-40 cent), glide (one octave per second): mean
under 3 cent, worst under 6 cent. Tone in 20 dB SNR white noise: mean under 3 cent. The
frequency-slope correction of the plan (Röbel) is not built because these gates pass; it returns
only if a real-voice residual shows bias.

**Limits.** The sieve trusts the supplied f0 curve; a wrong octave gives wrong partials. A harmonic
that does not exist still gets the strongest peak in its window (noise): consumers judge a frame by
its amplitudes, not by presence. Mono only. Offline: one FFT per voiced frame.
