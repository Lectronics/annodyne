# core/analysis/harmonic_noise

`split_harmonic_noise`: per-frame residual band power and sound class of one note (ADR-0017,
ADR-0018; research: `docs/design/vocal-resynthesis-research.md`). Three functions: `measure_frames`
(the metrics), `classify_frames` (the routing, pure on metrics so it is testable without audio) and
`split_harmonic_noise` (both, into a `NoiseTrack`).

**Metrics (per 5 ms grid frame).** 40 ms Hann spectrum: level, centroid, harmonic-to-noise ratio,
maximum voiced frequency (MVF), mel band residual. 10 ms spectrum: positive spectral flux
(normalised by the magnitude sum, so level independent), share of power above `hf_edge`, frame RMS.
Zero-crossing rate (10 ms). Octave flag from the second-best pitch path.

**Harmonic / noise split.** Bins within 2.5 window bins of a measured harmonic count as harmonic.
A band's noise density is the mean power of its unmasked bins, extended over the band; a band with
no unmasked bin takes the nearest band's density. HNR = harmonic / noise power over the frame; a
band is aperiodic when noise exceeds `aperiodicity_threshold` of it, counted only if within
`band_significance` of the frame power; MVF is the lower edge of the first aperiodic band.

**Routing (first match wins).** silence, plosive, sibilant (unvoiced, centroid above 4 kHz), breath,
rough (octave flag), breathy (HNR below 10 dB or MVF below 2 kHz), voiced. Labels are median
filtered over 5 frames, runs under 20 ms join their longer neighbour; plosive frames are never
smoothed away. A burst is a flux peak above median + 3 MAD, 30 ms apart, with a 6 dB closure dip
before it and a 15 point rise in the share of power above 4 kHz (this rejects vowel onsets).

**Real voices (Vocadito, 35 tracks).** 81 % voiced, 18.7 % breathy, rest under 0.5 %; the breathy share comes from the MVF rule (below 2 kHz) and is a calibration item, not a verdict that the singers are breathy. Rumble under the voice is excluded from MVF and HNR.

**Limits.** All thresholds are the research's starting values or guesses (see the params comments);
they are provisional parameters for the later fuzzy layer, calibrated per singer in Phase 8. HNR
counts window leakage as noise when f0 is below about 125 Hz (the harmonic masks cover the whole
spectrum); `band_significance` is 30 dB so Hann leakage tails do not read as breathy. SHR, CPPS,
GNE, H1-H2, jitter and shimmer are Phase 6. Tested on synthetic scenes only; real-voice behaviour
is measured at stage 4 with the orchestrator.
