# Design note: Phase 3 — pitch front-end filter and the internal activity stage

- **Phase:** 3 (real-vocal tuning slice; ADR-0011)
- **Status:** implemented (structure review: 2 blocking + 12 should-fix, all addressed below)
- **Spec sections:** implementation guide B1 (f0), B3 (segmentation), architecture §3 `FrameClass`; `docs/design/silence-and-gaps.md` §2 (the Phase 5 noise floor this stage will be reused by); ROADMAP Phase 3
- **Folders / CMake targets:** `src/core/dsp` (`filter`, layer 0), `src/core/analysis/pitch` (changed), `src/core/analysis/activity` (new, layer 1), `src/core/analysis/orchestrator` (changed), `tests/support/vocadito.h`, `tests/golden`

## Purpose
Measured on the Vocadito corpus (40 real vocals; `phase3-orchestrator-cli.md` §4): the pitch tracker calls only 60 % of annotated-voiced frames voiced, so 1353 of ~2200 notes are found. Loosening the voicing threshold fixes recall but raises false voiced frames (0.9 % to 4.8-7.5 %) because silence and noise pass it. Three changes, in order of the owner's request ("remove as many false pitches and detect as many real pitches as possible"; breaths are out of scope until later):
1. **Pitch front-end band-pass** (zero phase), cutoffs derived from `PitchParams` (+0.08 voicing recall on its own).
2. **Activity stage**: estimate the recording's noise floor, label every frame `silent` or `active`, and **gate the pitch track**: silent frames are forced unvoiced. This is what makes a looser voicing threshold safe.
3. **Retune** the voicing threshold (`beta_mean`) and the gating margins on the 35 non-committed Vocadito tracks, verified on the committed 5.

The activity result is **internal to analysis** (ADR-0011): it is not added to `AnalysisResult`, and no gap, breath, `SegmentTimeline` or edit exists. Phase 5 builds the public segment timeline on top of this stage rather than beside it.

## 1. `core/dsp/filter` (layer 0)
```cpp
/// Zero-phase 2nd-order Butterworth high-pass: run forward then backward, so no group delay and a magnitude response squared (-6 dB at `cutoff`, -24.6 dB one octave below it).
/// Pre: sample_rate finite and > 0, 0 < cutoff < sample_rate / 2 (invalid_argument otherwise). Output has the input's length; the edges use odd reflection of 3 x (sample_rate / cutoff) samples, bounded by n - 1. Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<std::vector<double>> zero_phase_highpass(std::span<const double> x, Hz sample_rate, Hz cutoff);
/// The same for the low-pass (-6 dB at `cutoff`, -24.6 dB one octave above it).
[[nodiscard]] Result<std::vector<double>> zero_phase_lowpass(std::span<const double> x, Hz sample_rate, Hz cutoff);
```
Two functions, no sentinel arguments (callers skip a side by not calling it). One shared `Biquad` (coefficient design + direct form II transposed, `double`) in `core/dsp/detail/biquad.h`, which `k_weight.cpp` is changed to use (it has its own copy today), so there is a single biquad in the codebase. Deterministic (`-ffp-contract=off`, as the rest of `core/dsp`). Memory: one `double` copy of the signal plus the reflection (about 64 MB for 3 minutes at 44.1 kHz), noted in the dsp README.

## 2. Pitch front end (`core/analysis/pitch`)
`PYinParams` gains `std::optional<double> highpass_min_f0_ratio = 1.0` and `std::optional<double> lowpass_max_f0_ratio = 1.5` (cutoff = ratio x `PitchParams::min_f0` / `max_f0`; `nullopt` skips that side; the low-pass cutoff is clamped to 0.45 x the analysis rate). The zero-phase pair is -6 dB at its cutoff, so a fundamental *at* `max_f0` would lose 6 dB with ratio 1.0; the ratio defaults to 1.5 (a first guess, tuned on the corpus) and a test pins that a tone at `max_f0` and one at `min_f0` are still tracked. Applied to the analysis-rate signal before the YIN frames, also when the source rate is already at or below the analysis rate. The reverted experiments (60 Hz high-pass, 1-2 kHz low-pass) are in `phase3-orchestrator-cli.md` §4.7; a high-pass alone did nothing, so the high-pass may end up `nullopt`.

**Versioning:** `PYinEstimator::version()` becomes 2 once, at the end of the tuning (not per experiment); nothing in the repository stores or compares a pitch version (`AnalysisResult::version` is caller-set), so no cache is invalidated. Any test with an expected hash is searched and updated.

## 3. `core/analysis/activity` (layer 1)
```cpp
enum class FrameActivity : std::uint8_t { silent, active };

struct ActivityParams {
    Seconds window{0.025};              // RMS window, centred on each pitch-grid frame
    Hz highpass{60.0};                  // rumble removed before the level is measured
    double floor_percentile_prob = 0.10;  // 0 < p < 0.5: noise floor = this quantile of the frame levels
    double range_percentile_prob = 0.95;  // top of the dynamic-range test below
    Decibels on_margin{9.0};            // a frame becomes active at floor + on_margin
    Decibels off_margin{5.0};           // ... and stays active down to floor + off_margin (hysteresis)
    Seconds min_active{0.030};          // shorter active runs are silent (clicks, thumps)
    Seconds max_dip{0.120};             // silent runs shorter than this between active runs stay active (plosive closures)
    Decibels min_range{10.0};           // (range percentile - floor) below this: no silence in the file, all frames active
    Decibels absolute_floor{-90.0};     // frames below this (digital silence) are silent whatever the quantile says
};

struct ActivityTrack {
    FrameGrid grid;                         // the grid it was computed for (source axis)
    std::vector<Decibels> level;            // frame level, dBFS of the high-passed signal, one per grid frame
    Decibels noise_floor{-120.0};           // the estimated floor, dBFS
    std::vector<FrameActivity> activity;    // one per grid frame
};
[[nodiscard]] inline bool is_active(const ActivityTrack& t, std::size_t i) noexcept { return t.activity[i] == FrameActivity::active; }

/// Level, noise floor and silent/active label of every frame of `grid`, from the mono signal (read and mixed once by the caller; no re-read). Frames whose window extends past the signal use the part that exists. Errors: invalid_argument (non-finite or non-positive window/highpass/min_active/max_dip/min_range, percentile outside its range, range percentile <= floor percentile, off_margin > on_margin, invalid grid, sample rate below 4 kHz). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<ActivityTrack> detect_activity(const MonoAudio& mono, const FrameGrid& grid, const ActivityParams& params);

/// A copy of `track` with every silent frame unvoiced: f0 NaN, voicing_prob 0, confidence_prob 1 (the confidence of an unvoiced frame is the unvoiced mass, as the estimator defines it), and the same in each alternate. `track` is not modified. Errors: invalid_argument (`activity.grid` differs from `track.grid`, or the arrays do not match the grid). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<PitchTrack> gate_pitch(const PitchTrack& track, const ActivityTrack& activity);
```
Method: frame level = RMS of the high-passed mono signal in a window centred on the frame, in dB. The floor is a low quantile of all frame levels, then two-threshold hysteresis, then two run-length clean-ups (short active runs removed, short silent runs between activity filled). A quiet but sustained note is active as long as it stays above floor + off_margin. **Known weakness:** with less than ~10 % silence in the file (a tightly edited take) the 10th-percentile floor sits inside soft singing; the `min_range` rule only covers the *no-silence* case. A test pins the behaviour, and if it fails the floor estimator gets a minimum-statistics or bimodality check (decided in tuning, recorded here).

**Why a third level measure:** `loudness` computes per-note amplitude curves from note spans and the segmentation HMM works from voicing; neither measures the whole recording's level against its own floor, which needs the whole-file view.

`gate_pitch` is separate from `detect_activity` because the first measures audio and the second edits a track (split rule 4: independently testable). It lives in `activity` because the activity track is its only input.

**Not in this stage:** voiced / consonant / breath classes (`FrameClass` stays unused here), the public segment timeline, noise-floor spectra, gap objects. A consonant is an active, unvoiced frame; its pitch still comes from the following vowel via the existing note logic (`phase3-notes.md`). **Phase 5 prerequisite (recorded here and in ADR-0011):** `silence-and-gaps.md` §2 defines a per-gap `floor_level`; the Phase 5 note must say how it relates to this global quantile floor (extend or replace), not add a second estimator.

## 4. Orchestrator
Order becomes: read mono -> pitch (filtered) -> `detect_activity` on the pitch grid -> `gate_pitch` -> onsets -> segmentation -> per-note stages. `AnalyseParams` gains `ActivityParams activity` (and nothing else; tests default-construct and assign fields, never partially designate); defaults are the tuned values. The CLI plot shades silent frames (plot only). `--json` is **not** extended: a silent-range list would be a public gap-like surface, which ADR-0011 keeps out. The orchestrator reads the mono once and passes it to both `detect_activity` and the K-weighting; the pitch estimator still reads the source itself through its interface.

## 5. Golden harness (revised ADR-free test-only change)
`tests/support/vocadito.h` reads the native Vocadito CSVs (f0, notes A1/A2, metadata). `golden_vocals_test.cpp` is rewritten: per track onset F vs each annotator (±20 and ±50 ms), pitch metrics vs the f0 CSV (voicing recall, false voiced, RPA/RCA at 50 cents, octave-error rate), matched-note pitch error; a **report mode** (`ANNO_VOCADITO_DIR=<path>`, all tracks, prints a table, never fails) for tuning, and the **gate mode** on the committed `tests/golden/vocals/vocadito/` subset. Gate thresholds are set from the measured result and the annotator-agreement ceiling and confirmed by the owner (ADR needed: the ROADMAP wording "onset F >= 0.9, no octave errors" is replaced; a blocker for *closing* Phase 3, not for this stage). Such a gate is a **regression guard** on 5 tracks, not a quality measure: the measured values, the margin and the 35-track report are recorded beside it, and the subset selection (sex, range, language, agreement high and low) is documented. Report mode never runs in CI (only gate mode is a ctest). Portability: parse with `strtod` in the C locale, strip `\r`, `.gitattributes` marks the WAV and CSV files `-text`, `_CRT_SECURE_NO_WARNINGS` for the `ANNO_*` getenv calls on MSVC, `tests/golden/CMakeLists.txt` points at `vocals/vocadito` and drops nlohmann. The subset is committed before the gate is relied on.

## Effect on audio
None (analysis only). The analysis result changes for every input: more notes on real singing, fewer false pitches in silence. Existing synthetic gates (3 cents on tones, 10-cent RPA >= 95 %, onsets, notes) must keep passing; if one needs loosening, stop and report.

## Split / join
- `zero_phase_bandpass` is shared by pitch and activity: reuse is the reason it is in `core/dsp` (rule of three not needed; two callers now, more later).
- `gate_pitch` is separate from `detect_activity`: one measures audio, the other edits a track (different reasons to change; separately testable).
- Level/floor/hysteresis/run-length helpers are anonymous-namespace functions inside `activity.cpp` (about 100 lines); split into `detail/` if the file passes ~250 lines.

## Dependencies
CMake: `activity` links `PUBLIC anno::core_types` and `PRIVATE anno::core_dsp`, with `-ffp-contract=off`; the orchestrator links it `PUBLIC` (its params embed `ActivityParams`); `pitch` adds `core_dsp` filter (already linked). The activity header includes no pitch header (`scripts/check_includes.py` must pass). No new third-party code.

## Tests (first)
- `filter`: gain within 0.1 dB of 0 at 4 x the high-pass cutoff (and 0.25 x the low-pass); -6 dB (+-0.3) at the cutoff; **>= 24 dB attenuation one octave outside** (the true value is 24.6 dB; the earlier "30 dB" was unreachable and is not a gate); zero delay (a symmetric pulse stays symmetric, peak position unchanged); DC removed; stable at cutoff = 0.001 x rate; inputs shorter than the reflection length; errors; determinism; `k_weight` output unchanged (bit-exact) after switching to the shared biquad.
- `pitch`: existing accuracy gates pass with the front end on; tones at `min_f0` and at `max_f0` are tracked within 3 cents; a tone plus loud 20 Hz rumble and plus loud 6 kHz hiss tracks within 3 cents; stereo; source rates 22.05 / 44.1 / 48 kHz and a source below the analysis rate; 20 dB SNR tone-in-noise RPA >= 95 %.
- `activity`: tone bursts in noise with a known timeline labelled within one hop of the truth; floor within 2 dB of the true noise level; noise only, a held note only, and digital silence (all active / all active / all silent); an 80 ms dip inside a phrase stays active, a 200 ms gap does not; a 15 ms click is silent; hysteresis does not chatter on a decaying tail; **a take with only 3 % silence** (pins the weakness above); slow attack/decay; mono, stereo, 4 / 22.05 / 44.1 / 48 kHz; parameter validation; determinism; the source is untouched. `gate_pitch`: the input is unchanged, the output has exactly the silent frames unvoiced (alternates too), mismatched grids are an error.
- Orchestrator and regression: a melody with silences and noise yields no note in the silent parts; **with the gate on, rerun every existing synthetic gate** (onsets +-5 ms, note onset +-20 ms, pitch +-10 cents, soft-start, repeated and legato notes, the 150-track short-note property test, the determinism hash). A gate that fails is reported, never loosened.
- Golden: the loader parses the committed tracks (LF and CRLF); report mode prints the per-track and total table; **targets after this slice** (report-only until the owner sets the gate): voicing recall >= 0.9, false-voiced <= 3 %, pitch accuracy at 50 cents >= 0.85, onset F (+-50 ms) >= 0.6 against the better annotator; they define "done" for this slice.

## Open questions
1. Gate values for the golden set (owner). 2. Whether the breath-vs-consonant split should reuse `ActivityTrack` (Phase 6, deferred by the owner).

## Review log
Structure review (first pass): CHANGES REQUIRED. Blocking: B1 `gate_pitch` mutated its argument (now returns a new `PitchTrack`); B2 the "30 dB at one octave" filter gate was unreachable for a zero-phase 2nd-order Butterworth (24.6 dB; gate corrected, not loosened). Should-fix, all applied: `--json` extension dropped; `detect_activity` takes the mono once; explicit CMake links; `FrameActivity` enum; renamed `_prob` fields and the percentile named; ratios as optionals; two single-purpose filter functions; missing tests added (low-silence take, stereo/rates, soft starts, existing-gate rerun, tones at `min_f0`/`max_f0`); one shared biquad; global-vs-per-gap floor flagged for Phase 5; ROADMAP and spec §6 updated with ADR-0011; golden-harness portability and the regression-guard wording; README work (activity, pitch front end, dsp memory) is part of the slices.

## Implementation notes (what changed from the reviewed text)
- **Floor estimator:** the 10th-percentile floor was replaced by "the lowest 3 dB band of frame levels holding at least 2 % of the frames" (`floor_mass_prob`, `floor_band`), after the low-silence design concern: a take with 3 % silence now finds its floor (test `a_take_with_little_silence_still_finds_its_floor`). Digital silence (< -90 dBFS) is excluded from the estimate and never active. `min_active` default is 40 ms (a 2 ms click smears to ~27 ms over the 25 ms window).
- **Activity link:** `activity` links `core_types` and `core_dsp` PUBLIC (its header includes `dsp/mono.h`).
- **Filter ratios** ended at 1.0 / 1.0 (`highpass_min_f0_ratio`, `lowpass_max_f0_ratio`); `beta_mean` 0.1 -> 0.3; `SegmentParams::preroll_max` 120 -> 30 ms; `OnsetParams::pitch_stable` 40 -> 65 ms. `PYinEstimator::version()` is 2.
- **Pitch-change detector** (`onsets/detail/pitch_change.cpp`) now finds two stable stretches (spread within 20 cents, each >= 65 ms) separated by an arbitrary transition of at most 60 ms (unvoiced gap or the blended ramp a looser voicing threshold draws across a step). The old detector looked for an f0 jump across an unvoiced gap and went blind once the tracker stopped dropping out. A vibrato turnaround stays inside 20 cents for ~50 ms, hence the 65 ms.
- **Real-vocal measurements** (Vocadito, `vocadito_report`; 35 tuning tracks / 5 held-out tracks never tuned on):

  | | tuning set before | tuning set after | held-out before | held-out after |
  |---|---|---|---|---|
  | voicing recall | 0.59 | 0.91 | 0.66 | 0.92 |
  | pitch accuracy (50 c) | 0.59 | 0.87 | 0.65 | 0.86 |
  | notes found (A1 / A2 annotated) | 1185 (1923 / 1626) | 1936 | 168 (314 / 250) | 293 |
  | onset F, +-20 ms (A1 / A2) | 0.29 / 0.31 | 0.49 / 0.48 | 0.25 / 0.23 | 0.39 / 0.38 |
  | onset F, +-50 ms (A1 / A2) | 0.51 / 0.54 | 0.73 / 0.73 | 0.41 / 0.46 | 0.62 / 0.65 |
  | octave-error rate | 0.0005 | 0.019 | 0.009 | 0.054 |

  Targets of this note's test plan: voicing recall >= 0.9 met (0.91 / 0.92); false voiced <= 3 %: held-out 3.4 %, tuning 7.1 % (most of it within 50 ms of an annotated voiced stretch, i.e. boundary placement: 41 % within 15 ms, 39 % within 15-50 ms, 20 % farther); pitch accuracy >= 0.85 met; onset F (+-50) >= 0.6 met (0.73 / 0.65).
- **The activity gate adds ~0 on this corpus** (disabling it changes pitch metrics by < 0.3 points): the tracker already called silence unvoiced. It labels 3-22 % of each track silent and wrongly silences 0.00-0.5 % of annotated-voiced frames. Kept for noisy material, Phase 5, and because it costs nothing here.
- **What still limits onsets** (annotator 1, tuned settings): 50 % of annotated notes have one of our starts within 20 ms (median offset 1 ms), 37 % lie 20-100 ms away (legato transitions; our starts are a median 25 ms later there), 13 % are missed (median length 93 ms), 15 % of our notes are extra. Annotator agreement caps any system near F 0.75 at +-20 ms.
- **Open for the owner:** (1) the gate wording (ADR needed; `tests/tolerances.h` holds a regression guard measured on the held-out five); (2) whether a note should start at the consonant (editing convenience) or at the vowel (annotators' convention): the pre-roll is now 30 ms, which mostly aligns with the annotators; (3) octave errors are accepted for now (the alternates hold the other octave); the held-out set has 5.4 %, concentrated in track 2 (a high voice, 19 %).
