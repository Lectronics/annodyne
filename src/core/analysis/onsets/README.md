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

## Stages (Phase 5; `onset_stages.h`, design `docs/design/phase5-transient-pipeline.md`)

The pipeline of `detect_onsets` is public as testable stages, following the onset research
(`docs/design/onset-detection-research.md`): **P0** `normalise_level` (a scaled copy; peak or RMS of
the active frames; the gain is returned) -> **P1** `band_spectrogram` (STFT magnitudes in the log-spaced
unit-sum triangular bands of Phase 3) -> **P2** `adaptive_whiten` (optional; per-band decaying maximum;
the form of Bock 2012 was not checked, provisional) -> **P3** `log_compress` (log(1 + gain x)) ->
**D** `superflux` (max filter over neighbouring bands of the lagged frame) -> **K** `pick_peaks_windowed`
(the research's pre/post max and average windows, ties to the first frame, first kept peak wins inside
the gap) -> **L** `localise` (the Phase 3 energy-rise refinement, note ends dropped). `detect_transients`
composes them (optionally P0 and P2); `detect_onsets` calls P1, P3, D and L with `OnsetParams` and keeps
its own adaptive-threshold picker, so its output is **bit-identical** to Phase 3 (pinned by
`onset_bench_test` "[onset_gate]": 40 signal / parameter combinations incl. a Vocadito recording).

Measured (ADR-0022, `docs/design/phase5-transient-pipeline.md`): on the 35 tuning tracks the pipeline and the
Phase 3 chain are equal within noise (onset F20 0.588 / 0.596 against 0.582 / 0.587), on the 5 held-out tracks the
pipeline is slightly better (F20 0.570 / 0.549 against 0.552 / 0.528, APC 0.586 against 0.552). `analyse()` uses the
pipeline (`analysis_onset_params()`); `OnsetParams{}` is the Phase 3 chain. On the research's synthetic families
(parameters chosen on them: optimistic) the pipeline finds more fast notes and one soft attack fewer; the false
positives on synthetic tremolo and noise come from the energy-step detector, which both methods run. Known weak
cases: legato pitch changes (bench C, 3 of 8) and fast notes (G: 11 of 16, about 23 ms early from the localiser).

Layout: `stages/` holds one file pair per stage group (`band_spectrogram`, `level_norm`, `spectral_flux` = whitening,
log, SuperFlux, `peak_pick`, `localise`), `transient_pipeline` composes them, `onset_stages.h` includes all of them;
`detail/` holds the Phase 3 glue (`spectral_onsets` method switch, `merge_onsets`, `energy_step`, `pitch_change`,
`peak_picking` = `pick_peaks_adaptive`, `onset_params_check`).
