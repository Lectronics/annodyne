# core/analysis/pitch

Monophonic f0 tracking (layer 1; links `core/types`, private `core/dsp`). Design:
`docs/design/phase3-pitch-onsets.md`; spec §3 `PitchTrack`, §4.2 `detect_pitch`; implementation guide B1.

**Method (pYIN, Mauch & Dixon 2014; YIN, de Cheveigne & Kawahara 2002).** The mono mix is resampled to
24 kHz (never above the source rate). Per frame (window 40 ms, hop 5 ms): YIN difference by FFT
autocorrelation (`yin.*`), cumulative-mean normalisation, and normalised cross-correlation. Candidates
(`pyin_observe.*`): for each of 100 thresholds weighted by a Beta(2, 18) prior the first local minimum
of d' below it gets the weight; lags are refined to fractions of a sample by the parabolic peak of the
NCCF; weak octave candidates are added. HMM (`pitch_hmm.*`): 10-cent pitch bins plus an unvoiced state,
triangular transitions within 80 cents per hop, a small probability for any larger jump (a legato
step), Viterbi decoding. Alternatives (`PitchTrack::alternates`): Viterbi again with bins within 75
cents of the earlier paths removed. YIN measures at the middle of its two windows (frame time + lag/2),
so each voiced run is resampled back onto the frame grid.

**Measured (synthetic, Phase 3):** stationary harmonic tones 80-1200 Hz: maximum error 0.92 cents
(gate: 3 cents). Vibrato (5.5 Hz, +-50 cents), a one-octave-per-second glide and a harmonic tone in
noise at 20 dB SNR: 10-cent accuracy >= 95 %, no octave errors, at 22.05-48 kHz.

**Interface.** `PitchEstimator` is an interface because neural estimators (RMVPE, PESTO, CREPE) are
planned for Phase 13; `PYinEstimator` is the DSP implementation, `detect_pitch` the spec entry point.
`voicing_prob` is the observed voiced mass of the frame, forced to the side of 0.5 chosen by the path;
`confidence_prob` is the chosen candidate's mass (voiced) or the unvoiced mass.

**Limitations / tuning:** `bin_cents` 10, `max_jump_cents` 80, `jump_prob` 1e-4, `beta_mean` 0.1 are the
pYIN defaults and are tuned on the synthetic set first and on real vocals at the Phase 3 review.
Octave alternatives are weak candidates, not a second tracker. Memory: the Viterbi back pointers take
frames x (bins + 1) ints (about 86 MB for a 3-minute file). Compiled with `-ffp-contract=off`.
