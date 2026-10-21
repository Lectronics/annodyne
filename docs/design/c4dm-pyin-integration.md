# Design note: the vendored c4dm/pyin tracker

- **Phase:** 3 (closing slice)
- **Status:** implemented
- **Spec sections:** implementation guide (pitch: pYIN), core architecture §4.2 `detect_pitch` / `PitchEstimator`
- **Folder / CMake target:** `third_party/c4dm_pyin` (`anno_c4dm_pyin`, vendored, GPL-2.0-or-later), `src/core/analysis/pitch` (layer 1; adapter `C4dmPYinEstimator`), `core/analysis/orchestrator` (engine choice)
- **Decision record:** ADR-0014. Evidence: `docs/design/phase3-voicing-review.md`, `tests/unit/voicing_probes_test.cpp`

## Purpose
Replace the annodyne pYIN as the default pitch tracker by the reference implementation (c4dm/pyin v1.2, the Vamp plugin behind Tony), behind the existing `PitchEstimator` interface, so analysis uses the published, validated observation model and pitch HMM: Beta threshold prior (mean 0.15), per-bin voiced and unvoiced states, `yinTrust` 0.5, the low-amplitude rule, and 5-bins-per-semitone triangular transitions.

## Public types
```cpp
struct C4dmPYinParams {            // defaults are the plugin's, except fixed_lag_frames and the guard
    Hz max_analysis_rate{44100};   // resampled down to this, never up
    int threshold_distribution = 2;  // 0 uniform, 1..4 Beta mean .10/.15/.20/.30, 5..7 single values
    double low_amp_rms = 0.1;      // block RMS (full scale 1) below which candidate mass is scaled; 0 = off
    double down_margin3 = 0.1;     // sub-octave guard (not in the plugin); negative = off
    double down_margin2 = 0.02;
    int fixed_lag_frames = 0;      // 0 full Viterbi; plugin default 100
};
class C4dmPYinEstimator final : public PitchEstimator { name() "c4dm_pyin", version() 1, estimate(...) };
enum class PitchEngine { c4dm_pyin, annodyne_pyin };   // AnalyseParams::engine, default c4dm_pyin
```
`PitchTrack`, `AnalysisResult`, `Note` are unchanged.

## What `estimate` does
Inputs: `AudioSource` (any channels, mixed to mono), `PitchParams` (min_f0, max_f0, hop 5 ms, n_alternates). Time axis: source; frame i is at `i * hop`. Units: f0 in absolute cents (A4 = 6900), NaN when unvoiced.
1. Mono mix; resample down to at most `max_analysis_rate`.
2. Block size = 2048 samples at 44.1 kHz, scaled with the rate and made even; frame i's block starts at `round(i * hop * rate) - block / 4` (the plugin reports a block's time as its start plus block / 4), zero-padded at the ends.
3. Per block (the plugin's `process`): block RMS; YIN difference by FFT (pocketfft through the `Vamp::FFTReal` shim), cumulative mean normalised difference, `yinProb` peak probabilities from the threshold distribution; **sub-octave guard**: the strongest candidate's mass moves to a dip at 1/3 (margin 0.1) or 1/2 (margin 0.02) of its lag that is nearly as deep, repeated up to 4 times; candidates in [min_f0, max_f0] with their parabolic-interpolated frequency; if RMS < `low_amp_rms` the masses are multiplied by `(rms + 0.01 * low_amp_rms) / (1.01 * low_amp_rms)`; `MonoPitchHMM::calculateObsProb` and the Viterbi step.
4. Decode (`track()`, with the plugin's fixed-lag emission order if `fixed_lag_frames > 0`), `nearestFreq` maps a voiced state back to the YIN candidate.
5. Output: voiced frame: `f0` the candidate, `voicing_prob = max(0.5, summed YIN mass)`, `confidence_prob` that mass; unvoiced frame: f0 NaN, `voicing_prob < 0.5`, `confidence_prob = 1 - mass`. The mass is the periodicity *before* the level scaling, so level affects the decision and not the number. `alternates` are NaN (the plugin has one path).

Errors: invalid_argument (min_f0/max_f0/hop not finite and positive, min_f0 >= max_f0, n_alternates < 0, source rate below 2 x max_f0, threshold_distribution outside 0..7, low_amp_rms or margins not finite or low_amp_rms negative, fixed_lag_frames negative, max_analysis_rate below 8 kHz); anything the source read returns. Pure apart from an upstream warning to `std::cerr` in a degenerate case; thread-safe (no shared mutable state between calls; the upstream threshold tables are read-only; pocketfft's plan cache is mutex-protected); not real-time safe (allocates).

## Limits
- Range: the HMM covers 61.7 Hz upward over 69 semitones; `min_f0` and `max_f0` only drop candidates (default 50 Hz minimum is effectively 61.7 Hz).
- The low-amplitude rule is absolute (see ADR-0014): clean lone tones are voiced to -50 dBFS RMS, not from -55; a take-relative reference is a carried item.
- Memory: full Viterbi keeps 690 int back-pointers per frame, about 55 MB per 100 s (2 GB per hour); a source over `full_viterbi_max_duration` (300 s) uses a lag of 100 frames automatically (`long_source_lag_frames`). The adapter's own data (signal copy, candidate lists, per-frame states, about 300 bytes per frame) is O(frames) whatever the lag.
- Frame 0 in fixed-lag mode is not decoded by the plugin; the adapter copies frame 1's decision.
- The guard only moves mass to a local CMND minimum; if the shifted lag maps above `max_f0` the frame becomes unvoiced.
- Time alignment: measured on a 1200 cents/s glide, the median pitch error corresponds to a time offset within 5 ms (test `c4dm_frames_are_time_aligned_on_a_glide`).
- A lone clean tone is voiced down to -50 dBFS RMS and not from -55 dBFS (the plugin's absolute low-amplitude rule).
- The sub-octave guard leaves 0.2 % octave/harmonic errors on the tuning corpus (track 34: 3 %, track 40: 2 %), 0 on the held-out tracks. The owner's rule is no octave errors; this is not yet met.
- Voicing is the path decision, not a posterior (review P4 is not adopted).

## Dependencies
`anno_c4dm_pyin` (private), `core_dsp` (resample, mono). No new dependency for any other target.

## Tests
`tests/unit/c4dm_pyin_test.cpp` (shim exactness, accuracy, octave cases, silence and gaps, fixed lag, validation, determinism, low-amplitude rule, equality with the plugin pipeline, sub-octave guard); `tests/unit/voicing_probes_test.cpp` (17 rows); the golden guards with the new engine; `vocadito_report` (`ANNO_TUNE=engine=1` runs the old tracker, `thr`, `lag`, `lowamp`, `cdown3`, `cdown2`).

## Review log
Structure review done after implementation (the owner's switch request arrived with the voicing slice in progress; the order of CLAUDE.md step 2 was not kept). Verdict CHANGES REQUIRED, 2 blocking, resolved:
1. Memory numbers wrong by 100x and the lag not applied by default: corrected here and in ADR-0014; `analyse` now bounds the lag for sources over 300 s; the O(frames) adapter data is stated.
2. ADR-0009 contradicted without amendment, no layer fixtures: amendment added to ADR-0009 and ADR-0014, three fixtures added.
Should-fix, done: licence consequences completed and SPDX headers added (ADR-0014 item 6); effective `min_f0` floor documented; frame 0 in fixed-lag mode copied from frame 1; analysis-rate validation against `max_f0`; the guard requires a local minimum; timing, lag-vs-full, odd rates, invariants, filtering and validation tests added; named constant for the unvoiced margin; the conscious FFT duplicate recorded. Not done: `threshold_distribution` as an enum and a unit type for `low_amp_rms` (kept as plain values mirroring the plugin's parameters); the `docs/specs` / CLAUDE.md folder-list update for `third_party/` (specs are read-only and CLAUDE.md is the owner's; ADR-0014 records it); storing the engine name in `AnalysisResult` (needs a core-contract change).
