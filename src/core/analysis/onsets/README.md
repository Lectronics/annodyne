# core/analysis/onsets

Onset detection (layer 1; links `core/types`, private `core/dsp`). Design:
`docs/design/phase3-pitch-onsets.md` §2; spec §3 `OnsetList`, §4.2 `detect_onsets`; implementation guide B2.

**Method (SuperFlux, Bock & Widmer, DAFx 2013).** STFT (window about 43 ms, hop 5 ms rounded to samples)
-> 24-bands-per-octave log-spaced triangular filterbank -> log(1 + 1000 |X|) -> frequency max filter
over 3 neighbouring bands of the lagged reference frame (a partial gliding in vibrato does not look
like new energy) -> positive difference at a 15 ms lag, averaged over the bands (`superflux.*`) ->
adaptive-threshold peak picking with a 30 ms minimum gap (`peak_picking.*`; threshold = local mean +
2 x local spread, floor 0.15) -> refinement of each peak to where the 2 ms short-window energy rises
through its halfway point (dB) within +-25 ms; peaks where the signal is clearly louder at the start
of that region than at its end are note ENDS (flux responds to offsets too) and are dropped.
`strength` is activation / threshold (>= 1).

**Pitch overload** (`pitch_change.*`): a step of >= 80 cents between two voiced frames with at most 60 ms of
unvoiced frames between them (the f0 estimate blends across a step), each side voiced and stable for
40 ms, is a legato note change (`OnsetKind::pitch_change`, strength = step / 80 cents). A spectral onset
WITHOUT an energy rise (legato: timbre or pitch changed under a steady level) that meets a pitch change
within the minimum gap is that pitch change; a spectral onset WITH an energy rise wins. Spectral onsets
are `consonant` when the pitch frame 20 ms after them is unvoiced (voicing < 0.5), else `attack`; without
a pitch track every spectral onset is an attack. `phoneme` onsets are not produced (Phase 3).

**Limitations / tuning:** `delta`, `min_activation`, `log_lambda` and `lag` are calibrated on synthetic
bursts, a quiet-vs-loud pair (40 dB apart) and a wide-vibrato tone, and need real-vocal tuning at the
Phase 3 review. SuperFlux's vibrato advantage is demonstrated on +-200 cents with 25 partials (5 false
onsets for plain flux, 0-1 here); +-300 cents defeats both. Soft attacks (a slow fade-in) are found at
the start of the energy rise only if the rise is >= 6 dB inside the search window. Compiled with
`-ffp-contract=off`.
