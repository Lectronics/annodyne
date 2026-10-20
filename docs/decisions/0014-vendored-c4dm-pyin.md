# ADR-0014: pitch tracking with the vendored c4dm/pyin (GPL-2.0-or-later)

- **Date:** 2026-10-20
- **Status:** accepted
- **Phase:** 3

## Context
An outside review of the voicing decision (`docs/design/phase3-voicing-review.md`) compared the annodyne pYIN with c4dm/pyin, the Vamp plugin Tony uses. Porting its probe signals to C++ tests (`tests/unit/voicing_probes_test.cpp`) showed four real failures on the annodyne tracker (a tail below -52 dB 52 % voiced; a lone hum a voiced note; a lone -80 dBFS tone a confident note; a voiced fricative a note) and several review claims that did not reproduce. The owner then asked to switch to c4dm/pyin and chose, when offered the alternatives (rebuild its behaviour in our own code, vendor it, or use it only as a test oracle), to **vendor the GPL code**.

c4dm/pyin is licensed GNU GPL version 2 **or (at your option) any later version**, with a request (upstream README) to credit its authors and Queen Mary, University of London in user-visible documentation. ADR-0009 had rejected FFTW for being GPL; this ADR is the explicit, owner-approved exception.

Measured on the Vocadito corpus (35 tuning / 5 held-out tracks), annodyne pYIN -> vendored c4dm pYIN with the sub-octave guard below:

| Measure | annodyne pYIN | c4dm pYIN |
|---|---|---|
| Voicing recall | 0.959 / 0.967 | 0.989 / 0.997 |
| Pitch within 50 cents (RPA) | 0.943 / 0.948 | 0.982 / 0.995 |
| Octave + harmonic error rate | 0.010 / 0.013 | 0.002 / 0.000 |
| Voicing false alarm | 0.131 / 0.067 | 0.125 / 0.086 |
| Matched-note pitch within 50 cents | 0.820 / 0.803 | 0.878 / 0.818 |
| -10/+15 cent share of the target | 0.615 / 0.530 | 0.694 / 0.578 |
| Onset F at +-20 ms, annotator 1 | 0.570 / 0.515 | 0.540 / 0.519 |
| Onset F at +-50 ms, annotator 1 | 0.775 / 0.710 | 0.729 / 0.694 |

All 17 probe rows pass (the plugin's low-amplitude rule and per-bin unvoiced states fix the four failures). Onset F at +-20 ms on the tuning tracks is slightly lower: the segmentation was tuned on the old tracker's output and is not re-tuned here.

## Decision
1. **Vendor** the pitch-tracking core of c4dm/pyin commit `1d68cac9c5ecb1d81054d25d90158e5838dd78c2` (v1.2) in `third_party/c4dm_pyin/`: `Yin`, `YinUtil`, `MonoPitchHMM`, `SparseHMM`, `MeanFilter`, `COPYING`, `README`, verbatim except removal of an unused Boost include (listed in `third_party/c4dm_pyin/ANNODYNE.md`). Not vendored: the Vamp plugin glue, the note HMM (our segmentation stays), `LocalCandidatePYIN`.
2. **Build:** `cmake/AnnoC4dmPyin.cmake` defines the static target `anno_c4dm_pyin` (warnings off for upstream code, `-ffp-contract=off`, pocketfft for its FFT through a small `vamp-sdk/FFT.h` shim). The external allowlist in `cmake/AnnoLayers.cmake` lets only `core/analysis/pitch` link it, privately.
3. **Adapter:** `C4dmPYinEstimator` (`core/analysis/pitch/c4dm_pyin.{h,cpp}`) implements `PitchEstimator` and re-implements what `PYinVamp.cpp` does around the vendored classes (blocking, the low-amplitude rule, the HMM loop, `nearestFreq`). It is a derivative work under the same licence. Parameters in `C4dmPYinParams` default to the plugin's, except `fixed_lag_frames = 0` (full Viterbi; the plugin's fixed lag of 100 is available).
4. **One addition not in the plugin, off-switchable:** a sub-octave guard (`down_margin3` 0.1, `down_margin2` 0.02). The plugin reports f0 / 3 in half of the frames of Vocadito track 34; with the guard the octave rate there is 3 % and the corpus rate 0.2 %. With both margins negative the adapter equals the plugin's pipeline (test `c4dm_with_the_guard_off_equals_the_plugin_pipeline`, 1e-6 cents).
5. **`analyse()`** runs the vendored tracker by default (`AnalyseParams::engine = PitchEngine::c4dm_pyin`); `PitchEngine::annodyne_pyin` keeps the earlier estimator for comparison until Phase 13 decides. `PitchTrack` and `AnalysisResult` are unchanged. `voicing_prob` follows the decoded path (>= 0.5 voiced) and carries the summed YIN probability *before* the level scaling, so `Note::confidence_prob` still describes periodicity; the level enters through the plugin's rule in the decision.
6. **Licence consequence (owner-approved):** the combined work that links `anno_c4dm_pyin` must be distributable under GPL-2.0-or-later terms. Concretely: no closed or commercial-only edition (any edition limit is removable by recipients); binary distribution needs a source offer; proprietary SDKs and store terms that conflict with the GPL (AAX, some app-store terms) cannot be combined; combined with GPLv3 or AGPL libraries such as JUCE under its open-source terms the effective licence is GPLv3-or-later; contributors' licence terms (a CLA) are affected. The repository has no licence file yet; before any release the owner decides the project licence consistently with this, and the product credits the pYIN authors and Queen Mary, University of London in its about/credits screen. The Annodyne-authored derivative files (`c4dm_pyin.{h,cpp}`, `third_party/c4dm_pyin/vamp-sdk/FFT.h`) carry `SPDX-License-Identifier: GPL-2.0-or-later`. JUCE and the ARA SDK licences must be checked against the decision (ROADMAP carried item).
7. **Dependency rules.** `anno_pocketfft` was linkable only by `core/dsp` (ADR-0009, amended here): the vendored target `anno_c4dm_pyin` links it too, publicly, because its FFT shim includes the pocketfft header. `anno_c4dm_pyin` is a plain target outside `src/`, so `anno_check_layers` governs who links it, not its own links. Layer fixtures prove the rule: `allow_pitch_c4dm_pyin` (PASS), `deny_onsets_c4dm_pyin` and `deny_dsp_c4dm_pyin` (R6); `deny_analysis_pocketfft` still holds. `third_party/` is outside the format, clang-tidy and include-check globs (`src/`, `tests/`) and is compiled with warnings off; it is edited only as `ANNODYNE.md` lists.
8. **Conscious duplicate:** `vamp-sdk/FFT.h` wraps pocketfft next to `core/dsp`'s `RealFft`, because the vendored library cannot link an `anno_` target. A `Vamp::FFTReal` instance mutates a member buffer, so it is not thread-safe per instance; every `YinUtil` is local to one `estimate()` call. `YinUtil::yinProb` can print a warning to `std::cerr` (upstream); `Yin`, `MeanFilter` stay compiled into the vendored target only for the plugin-parity test.

## Alternatives considered
- **Match the plugin's behaviour in our own code** (no GPL): keeps the licence free, but the observation model and HMM would drift from the reference and we would re-validate every detail; recommended by me, declined by the owner.
- **Reference oracle only** (build the plugin outside the repo): no licence effect, no gain in the product.

## Consequences
- Pitch accuracy and voicing improve on the corpus (table above); the earlier tuning of `beta_mean` 0.3, subharmonic margins and `stay_relief` belongs to the old tracker only.
- The plugin's low-amplitude rule is an absolute level on the 2048-sample block RMS: a clean lone tone is voiced down to -50 dBFS RMS and unvoiced from -55 dBFS. A DAW tool would want a take-relative reference (review P1.2); carried, not changed (the owner chose the reference behaviour).
- Known upstream quirks kept for parity: the running power term in `YinUtil::fastDifference` adds `in[tau + N]` where the exact sum needs `in[tau - 1 + N]`; `parabolicInterpolation` calls `abs` on a double (resolves to the integer overload on some libraries; only matters for |adjustment| > 1); frame 0 is not decoded in fixed-lag mode.
- Memory: full Viterbi keeps one row of 690 int back-pointers (2.8 KB) per frame, about 55 MB per 100 s of audio (2 GB per hour). `fixed_lag_frames` bounds that; a source longer than `full_viterbi_max_duration` (300 s) gets a lag of 100 frames automatically. The adapter still holds O(frames) data whatever the lag (the signal copy, the candidate lists, the per-frame states: roughly 300 bytes per frame).
- The effective lowest pitch is max(`min_f0`, 61.7 Hz): the plugin's HMM starts at 61.735 Hz, so a lower `min_f0` is accepted and has no effect (a vocal range assumption; notes below B1 are not tracked). `PitchParams` defaults to 50 Hz.
- Nothing in `AnalysisResult` records which tracker ran (`PitchEstimator::name()` / `version()` exist but are not stored); the engine choice is part of `AnalyseParams`.
- Tests: `c4dm_pyin_test` (shim exactness, 3-cent accuracy, vibrato/glide, octave cases, silence, fixed lag, validation, determinism, low-amplitude rule, plugin parity, sub-octave guard), the 17 probe rows, and all golden guards.
- ADR-0012's pitch front end (band-pass, subharmonic check, `stay_relief`) applies to `annodyne_pyin` only. The `pitch_target_mode` ADR formerly numbered 0014 is now ADR-0015.

## Amendment to ADR-0009
pocketfft may also be linked by the vendored `anno_c4dm_pyin` target (item 7 above). `analysis` targets other than through that vendored target still go through `core/dsp`.

## Spec updates
`docs/specs/annodyne-implementation-guide.md` (pitch estimation: pYIN) is satisfied by the reference implementation instead of ours; the architecture spec's folder list gains `third_party/` (vendored upstream sources, never edited except as listed in ANNODYNE.md).
