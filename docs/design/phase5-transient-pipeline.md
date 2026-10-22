# Design note: Onset pipeline as testable stages (timing backend, stage 1)

- **Phase:** 5 (Timing). Started 2026-10-22 on branch `phase-5` (from `phase-4`). The owner asked to begin Phase 5 while the Phase 4 close-out is open (see the end); `ROADMAP` "Current phase" stays 4 until that is done.
- **Status:** draft, revised after structure review rounds 1 and 2 (round 1: B1-B4, S1-S9 answered; round 2: B1-B2 and S1-S7 answered below)
- **Spec sections:** architecture spec §3 `OnsetList`, §4.2 `detect_onsets`, §7 layout (`onsets/` holds the detection functions); implementation guide B2; ROADMAP Phase 3 carried item ("thresholds ... to be replaced by a more accurate transient and consonant analysis") and ADR-0010a (transient-driven segmentation depends on onset strength); ROADMAP Phase 5; ADR-0018 timing items
- **Research input:** `docs/design/onset-detection-research.md` (owner, 2026-10-22; all numbers synthetic) and `docs/design/onset_test/` (generators, scorers, reference SuperFlux in Python and C++)
- **Folder / CMake target:** `src/core/analysis/onsets/` (existing, layer 1; links `core/types`, private `core/dsp`). **No new target and no new folder.** The stages become the single implementation of what `detect_onsets` already does.

## Purpose
`detect_onsets` (Phase 3) does its pre-processing, detection function, peak picking and refinement inside two private functions, so no stage can be tested alone, and the research's pre-processing options are missing. This note splits the pipeline into public stage functions, each with its own tests, adds the research's pre-processing (level normalisation, adaptive whitening) and its peak-picking rule, and builds the research's test bench in C++. `detect_onsets` keeps its signature and its numbers: it is re-pointed to call the stages with the Phase 3 parameters, and a bit-exact comparison of its activations and onset lists before and after the refactor (on the synthetic families and the golden fixtures) gates the change, so the Phase 3 baselines (35-track onset F20 0.582, note pitch 0.853, APC 0.655; held-out APC 0.552) cannot move.

Whether the research-style pipeline (normalised level, windowed picking) should *replace* the Phase 3 chain in `analyse()` was decided by [ADR-0022](../decisions/0022-analyse-uses-the-staged-onset-pipeline.md) (proposed) on numbers (Phase 3 golden numbers next to the new ones, at the annotators' F20 window as well as the research's +-25 ms; `segment_notes` depends on onset strength through `merge_onset_strength` 1.5, so a changed strength scale also means re-tuning ADR-0010a). Not decided here.

## Scope of this stage
In: public stages P0 level normalisation, P1 band spectrogram, P2 adaptive whitening, P3 log compression, D SuperFlux activation, K windowed peak picking, L localisation; the composed `detect_transients`; the test bench (families A-G, greedy scorer, level sweep); a baseline run of the existing `detect_onsets` on the research's level sweep, *before* anything is claimed about a gap.
Out (own notes later): HFC, spectral flux as a separate function (it is `superflux` with `max_filter_bins = 1`), Scheirer/Klapuri band layouts (no consumer in this phase; the research itself finds HFC fails on legato and noise), tonal/transient separation, phase and complex-domain functions, event labelling beyond Phase 3's attack/consonant/pitch_change, any rendering. Note stretching and the TimeMap work consume these events and are separate stages.

## Conventions (fixed by this note, B3)
- **STFT magnitude:** `core/dsp` `stft()` as is: periodic Hann, no 1/N scaling, so a full-scale sine of amplitude A has |X| about A * n_fft / 4 (A * 512 at n_fft 2048). Input is `float` samples in [-1, 1].
- **Band value:** unit-sum triangular filters applied to |X| (a weighted mean of magnitudes, not a sum).
- **Activation scale:** mean over bands of the positive log-difference, with `log(1 + log_gain * value)` (natural log). It is not on the research's scale (sum over bands, log10, unnormalised triangles); the research's threshold 1.1 does not transfer, and `delta_activation` below has its own default, measured on the bench (first-run value to be recorded in this note; the placeholder in the struct, 0.15, is arbitrary).
- **Whitened path:** after P2 the input of P3 is the dimensionless ratio y in (0, 1], so `log_gain` 1000 no longer means "per unit STFT magnitude" and any near-silent steady band reads 1. `TransientParams` therefore carries a *second* `LogParams` default for the whitened path (`log_gain` to be measured on the bench; the research quotes 0.01-20 for whitened data) and `floor_magnitude` is stated relative to the STFT scale (|X| is about A * 512 for a sine of amplitude A) with a bench test that quiet-band noise does not fire. Until Open question 1 is closed the pipeline tests do not claim that whitening helps; P2 is implemented and unit-tested as a stage only.
- **Names carry units:** `log_gain` (per unit STFT magnitude), `floor_magnitude`, `delta_activation` (activation units).

## Public types (namespace `anno`, header `core/analysis/onsets/onset_stages.h`)
`Activation` and `ActivationPeak` are moved from `detail/` into this public header (same members and order as today) so there is exactly one definition; the detail headers include it. The research-style picker gets its own parameter type name (`WindowedPeakParams`), distinct from the existing `PeakPickParams`.

```cpp
struct BandSpectrogram {            // frame i is centred at i * hop from the start of the analysed signal
    Seconds hop;                    // the ACTUAL hop (whole samples / rate)
    std::int64_t n_frames = 0;
    std::int64_t n_bands = 0;
    std::vector<Hz> centres;        // strictly increasing, size n_bands
    std::vector<double> data;       // frame-major, n_frames * n_bands, >= 0
};
[[nodiscard]] Result<void> validate(const BandSpectrogram&);

enum class LevelNorm { peak, rms_active };
struct LevelNormParams { LevelNorm mode = LevelNorm::rms_active;
                         Decibels target{-23.0};                     // peak mode: peak dBFS; rms mode: RMS dBFS of the active frames
                         Decibels active_floor{-60.0};               // frames more than this below the loudest do not count
                         Decibels max_gain{40.0}; };                 // the largest gain applied (a very quiet file is not amplified without bound)
struct NormalisedSignal { std::vector<float> samples; Decibels gain; };

struct BandParams { Seconds window{0.043}; Seconds hop{0.005}; int bands_per_octave = 24;
                    Hz f_min{30.0}; Hz f_max{16000.0}; };            // = Phase 3 (min(f_max, 0.45 rate), linear fallback under 4 bands)
struct WhitenParams { Seconds relax{0.5}; double floor_magnitude = 1e-4; };
struct LogParams { double log_gain = 1000.0; };                      // = OnsetParams::log_lambda
struct FluxParams { Seconds lag{0.015}; int max_filter_bins = 3; };  // odd; 1 = plain spectral flux
struct WindowedPeakParams { Seconds pre_max{0.030}, post_max{0.030}, pre_avg{0.100}, post_avg{0.070}, min_gap{0.030};
                            double delta_activation = 0.15; };       // provisional and arbitrary (an increment above the mean, NOT Phase 3's min_activation, which floors a threshold); measured on the bench
struct PeakList { Seconds hop; std::vector<ActivationPeak> peaks; }; // peaks keep their strength
struct LocaliseParams { Seconds search{0.025}; Seconds energy_window{0.002};
                        Decibels min_rise{6.0}; Decibels end_drop{3.0}; };   // = k_min_rise_db, k_falling_db of Phase 3
enum class LocaliseOutcome { rose, unchanged };                              // unchanged: the energy did not rise by min_rise
struct LocalisedOnset { std::int64_t sample; SourceTime time; double strength; LocaliseOutcome outcome; };
// time = SourceTime{} + Seconds{sample / rate}, the expression Phase 3 uses; `outcome` is what detect_onsets' merge step reads (energy_rise).
```

## Public functions
Each is pure, thread-safe, not real-time safe (allocates), `[[nodiscard]]`, returns a new object (inputs are never written), and works on the **source axis**. A stage that takes an empty signal returns an empty result, not an error (`stft` rejects empty input, so the stage checks first).

```cpp
/// P0. A scaled COPY (and the gain applied). The source is never touched. peak: max|x| -> target;
/// rms_active: RMS over the 20 ms frames within active_floor of the loudest -> target. Silence: gain 0 dB,
/// samples unchanged. gain is limited to max_gain. Errors: invalid_argument for non-finite samples/params.
Result<NormalisedSignal> normalise_level(std::span<const float> mono, Hz rate, const LevelNormParams&);

/// P1. STFT magnitudes reduced to log-spaced triangular bands (unit sum, centres on distinct bins, Phase 3 filterbank).
/// Errors: invalid_argument for non-positive/non-finite window, hop, rate; bands_per_octave < 1; f_min >= f_max. Shorter than the window: 0 frames.
Result<BandSpectrogram> band_spectrogram(std::span<const float> mono, Hz rate, const BandParams&);

/// P2 (optional). Per band p(n) = max(x(n), decay * p(n-1), floor), y(n) = x(n) / p(n), decay = exp(-hop/relax), on the
/// LINEAR band values, before P3. y is in (0, 1]; the first frame reads 1 in every band above the floor (p starts at x), which
/// the first `lag` activation frames ignore. NOTE: this is the decaying-maximum form of Stowell & Plumbley / aubio; the research
/// quotes Bock 2012 ("m = 10, r = 0.005") and the exact form there was NOT checked: provisional. Errors: relax <= 0, floor <= 0, invalid input.
Result<BandSpectrogram> adaptive_whiten(const BandSpectrogram&, const WhitenParams&);

/// P3. log(1 + log_gain * x). Monotone, 0 -> 0. Errors: log_gain <= 0 or non-finite, invalid input.
Result<BandSpectrogram> log_compress(const BandSpectrogram&, const LogParams&);

/// D. SuperFlux: sum_b H(x(n,b) - max_{|b'-b| <= (w-1)/2} x(n-mu, b')) / n_bands, mu = max(1, round(lag/hop)); the first mu frames are 0.
/// Errors: max_filter_bins even or < 1, lag <= 0, invalid input.
Result<Activation> superflux(const BandSpectrogram&, const FluxParams&);

/// K. Frame n (counts = round(seconds / hop), >= 0) is a peak when act(n) > 0 AND act(n) is the maximum over [n-pre_max, n+post_max]
/// (clamped at the ends) AND act(n) >= mean(act over [n-pre_avg, n+post_avg]) + delta_activation, the mean taking zero for frames
/// outside the signal and dividing by the full window width (as the reference), AND n - last_kept_frame > min_gap (first wins, against
/// the previous KEPT peak: the reference compares with the previous raw detection, a deliberate difference listed in the cross-check). Ties in the
/// maximum test: the first frame wins (a strictly greater value to its left disqualifies, an equal value to its left disqualifies, as detail/peak_picking.cpp does).
/// With min_gap <= the max window and strict ties the gap condition cannot fire; it is kept for other parameter sets.
/// strength = act / max(mean + delta_activation, 1e-9); it is >= 1 only when act >= mean + delta (always true for a picked peak), so ActivationPeak::strength is documented as
/// 'detector output, >= 1 for a picked peak' for both pickers. Offline only. Errors: negative windows, delta < 0, hop <= 0.
Result<PeakList> pick_peaks_windowed(const Activation&, const WindowedPeakParams&);

/// L. The Phase 3 refinement (`refine_to_energy_rise`), moved here unchanged: each peak is moved to where the 2 ms energy rises
/// through its halfway point (dB) within +-search; peaks where the signal is clearly louder at the start of the region than at its
/// end are note ENDS (flux responds to offsets) and are dropped; a rise under min_rise dB leaves the frame time. Errors: invalid params, a peak beyond the signal.
Result<std::vector<LocalisedOnset>> localise(std::span<const float> mono, Hz rate, const PeakList&, const LocaliseParams&);

/// The composed pipeline with the optional stages: P0 (when level is set), P1, P2 (when whiten is set), P3, D, K, L; mono mix of the source.
struct TransientParams { std::optional<LevelNormParams> level; BandParams bands; std::optional<WhitenParams> whiten; LogParams log; FluxParams flux; WindowedPeakParams peaks; LocaliseParams localise; };
Result<OnsetList> detect_transients(const AudioSource&, const TransientParams&);   // kind attack; strength from K
```

`detect_onsets` is re-pointed: it calls P1, P3, D with its `OnsetParams` and keeps its own adaptive-threshold picker (`detail/peak_picking`, local mean + delta x spread), energy-step and pitch-change code and merging, then L. `OnsetParams` stays the public parameter type of `detect_onsets`. **It keeps its own `validate(OnsetParams)` and its current errors** (the fields of other stages, such as threshold_window, delta, min_gap, refine_window and energy_step_*, are checked there, not in the stages). Behaviour that must not change:
- An EVEN `max_filter_bins` is accepted today and truncates to radius (w-1)/2; D rejects even widths, so `detect_onsets` passes `2 * ((w - 1) / 2) + 1` (the same radius) and the gate includes an even width.
- Bit-exact details, stated so the split cannot change arithmetic: P1 keeps `nearest_pow2` (minimum exponent 8), `hop_samples = min(hop, n_fft)`, the per-band accumulation `s += w[k] * |X|` from 0.0 in bin order, and returns the actual hop with 0 frames for a signal shorter than n_fft; P3 is `std::log1p(log_gain * s)`; D starts the reference maximum at 0.0, uses `mu = max(1, lround(lag / actual_hop))` and divides by the band count; no compile-flag change follows from the move (`-ffp-contract=off` stays).
- The gate compares the activation (`memcmp` of the doubles) and the final `OnsetList` (times, strengths, kinds) before and after, on the families A..G, the level sweep and the committed Vocadito fixtures; the "before" values are captured from the current code first (pinned in the test as hashes/values) and the refactor must reproduce them exactly.

## Effect on audio
None. Analysis only; the source is never modified (P0 returns a copy; a whole-file copy costs one float per sample).

## Test bench (`tests/support/onset_bench.h`, `onset_score.h`; fixed 44.1 kHz as in `gen.py`)
Ports `gen.py` families A, A0, B, C, D, E, F, G. The vowel is `psola_sim.py`'s `make_vowel` ported as written: an impulse train (pitch-modulated for vibrato, fractional positions split over two samples) -> one-pole tilt (0.95) -> four resonators (700/80, 1100/90, 2500/120, 3500/150 Hz centre/bandwidth), peak-normalised; no jitter, shimmer or noise. Not sample for sample the research's signals: the noise of the consonants and of family F comes from the repo's seeded generator and zero-phase high-pass, not numpy, so numbers will differ slightly and a difference is recorded, not hidden.

**Scorer (exact port of `run_all.py`):** for each truth onset in order, take the nearest unused detection with `|d - t| <= tol` (inclusive, greedy, not an optimal assignment); offset = d - t (positive = late); FP = detections - TP; FN = truths - TP; also the median offset over matched detections. Default tol = 25 ms; F20 is also reported. Family E is scored per syllable as in `score.py` (consonant start C, vowel start V, each within tol; "other" detections counted) because its truth has two onsets per syllable; the generator's `truth` field for E is the vowel start.

## Tests (written first; synthetic, known answers; each stage tested alone)
- **P0 normalise_level** (rms_active: RMS = sqrt of the mean of the squares of the 20 ms frames within `active_floor` of the loudest frame; the bench families are peak 0.5 so the needed gain is at most about 6 dB + 40 dB is never the limit): the same signal at 0/-20/-40 dB returns RMS (rms mode) or peak (peak mode) within 0.1 dB of the target, and the returned `gain` equals the applied dB within 1e-9; when the needed gain exceeds `max_gain` the output is `max_gain` louder than the input and its level stays below the target (a very quiet file is not amplified without bound); silence gives gain 0 and bit-identical samples; the input is unchanged (bitwise); NaN -> invalid_argument.
- **P1 band_spectrogram**: frame count = floor(N / hop) + 1; shorter than the window -> 0 frames and empty data; empty signal -> empty; centres strictly increasing; a 1 kHz sine has its largest value in the band whose centre is nearest 1 kHz; the value there exceeds bands >= 4 bands away by an amount computed for the chosen n_fft in the test (written after the first run, from the numbers, with a margin); the result equals the Phase 3 `superflux_activation` internals bit for bit once P3 and D are applied (the gate).
- **P2 adaptive_whiten**: hand-computed 3-frame, 1-band case (x = 1, 0.5, 2 with decay d, floor f) equals p/y by the formula to 1e-12; y <= 1 and > 0 everywhere; silence gives y = 0 / floor-bounded values with no NaN/Inf; after n quiet frames p = p0 * decay^n while that exceeds max(x, floor), so y = x / p is exact to 1e-12 on a hand-computed case; a steady tone and an onset after silence both read 1 by construction, which tests the formula and is *not* evidence that whitening helps (that is the pipeline test with its pinned numbers).
- **P3 log_compress**: log_compress of 0 is 0, of e-1 / log_gain is 1 (hand values), monotone on a grid, equals `log1p(g x)` to 1e-12.
- **D superflux**: first `mu` frames 0; a constant spectrogram gives all 0; a single band stepping up by a known amount gives exactly step / n_bands at the step frame; a single energy shifting one band up (a "vibrato" move of one bin) gives 0 with max_filter_bins 3 and a positive value with 1; the Phase 3 vibrato test (+-200 cents, 25 partials) is reproduced through the stage API with the Phase 3 numbers (5 plain flux vs 0-1 SuperFlux false onsets).
- **K pick_peaks_windowed**: hand-made activations check each condition alone (not a maximum; below mean + delta; inside the gap after a kept peak (first wins); zero activation never a peak even with delta 0; first and last frame; windows at the edges with zero padding); strength = act / (mean + delta).
- **L localise**: a 5 ms-attack onset at a known sample lands within 1 ms of it from a coarse peak 10 ms away; an 80 ms attack lands inside the ramp, at or after its start and before its half-amplitude point (the oracle is the halfway point of the ramp in dB, computed in the test); a note-end peak is dropped; a constant signal and a rise under `min_rise` leave the frame time; a peak beyond the signal -> invalid_argument.
- **Baseline before any claim (S4):** `detect_onsets` on families A, A0, B, C, D, F, G and the level sweep 0..-40 dB (the research's `level.py` sweep, pooled over A, A0, B, D, F, G): TP/FP/FN and median offset recorded in the test as pinned bounds with a stated margin after the first run. This is the evidence whether level normalisation or whitening is needed at all.
- **Pipeline per family**: assertions are numbers of *unmatched detections* (FP) and matched truths on the localised times, with bounds pinned after the baseline above; B is scored on localised times and on frame times separately (a halfway-in-dB localiser can move an 80 ms attack by more than 25 ms; that is a localisation question, not a detection one). Hypotheses to test, not claims: SuperFlux gives no unmatched detection inside the sustained notes of A and D; all truths found in B and C; level-normalised pooled F stays within a pinned margin of its 0 dB value from 0 to -40 dB.
- **Cross-check vs the reference** (`scripts/phase5_superflux_check.py`, not a ctest): writes the families, runs `onset_test/cpp/sf_cli` and the pipeline, prints detection-time differences. The reference's lag is 2 frames (10 ms) from its window shape; the script sets `lag` to 10 ms for this comparison (Phase 3 uses 15 ms), and its `combine` argument is milliseconds (the research's note on the seconds/ms quirk does not apply here: `min_gap` is a `Seconds`). Frame counts differ (reference ceil(N / 220.5), here floor(N / 220) + 1) and its Hann is symmetric (here periodic): the script reports the offsets rather than expecting zero.

## Split / join decisions
- Stages are separate functions, not one `preprocess()`: coding standards §5 split rule 4 (independently testable), the purpose of the stage.
- No new target: the stages are the existing implementation made public (join rule: "the pieces always change together" with `detect_onsets`); this removes the duplicated filterbank, flux and refinement and the ODR hazard of a second `Activation`.
- Adaptive whitening before log, `std::optional` for "off" in `TransientParams` (one mechanism for "stage off"); `max_filter_bins = 1` is plain flux, so there is no separate enum.
- `LevelNormParams::mode` is an enum (`peak`, `rms_active`), no `none` value: absent optional means off.
- Peak picking: two pickers coexist deliberately (`pick_peaks` local mean + spread for `detect_onsets`, `pick_peaks_windowed` for the research rule); they differ in rule and params. Revisit when the ADR decides which one `analyse()` uses.

## Dependencies
`core/types`, private `core/dsp`; no new third-party code; `-ffp-contract=off` (already on the target). New public header `onset_stages.h`; `onsets/README.md` updated; `src/CMakeLists.txt` unchanged (target exists); spec §7 already lists detection functions under `onsets/`, so no spec change and no ADR is needed for the folder. An ADR is needed later only for replacing the `analyse()` parameters.

## Open questions
1. Bock 2012's exact whitening form (see P2): read the paper before treating the defaults as more than provisional.
2. Which of the two pickers/parameter sets `analyse()` should use: ADR with the Phase 3 golden numbers and annotator F20 as well as +-25 ms.
3. Real recordings: the research is synthetic-only; the committed Vocadito subset is the only real material, and its annotations are notes, not every consonant.

## Phase 4 close-out still open
Listening sheet `docs/reviews/phase4-listening.md`, the default-engine ADR (waits for the owner's listening), `docs/reviews/phase4-review.md`, ROADMAP Phase 4 section and "Current phase". The owner asked to begin Phase 5 on 2026-10-22; the close-out is not done.

## Results (2026-10-22, debug-asan; implemented in `onset_stages.{h,cpp}`)
Built: P0-L and `detect_transients` as specified; `detect_onsets` re-pointed (P1, P3, D, L) with its own validation and picker. Tests: `onset_stages_test` (30 cases, 1161 assertions), `onset_bench_test` (bench, scorer, the bit-exact gate, baseline pins). Tests for the new stages were written after the code moved (the gate was captured first, before any change); the stage oracles are hand-computed cases.

**Gate:** the 40 fingerprints of `detect_onsets` and the activation (every family, family A at -30 dB, a 12 s excerpt of Vocadito track 2, four parameter sets incl. an even filter width and plain flux) are identical before and after the refactor (FNV-1a over the doubles, `memcmp`-equivalent).

**Baseline of `detect_onsets` on the bench (S4, before any claim of a gap):** family A, A0, B 8/0/0 (median +3 ms); C 2/9/6; D 1/23/0 (23 false positives inside the one note); E 3/13/5; F 8/8/0; G 6/24/10 (median -22 ms). Pooled A, A0, B, D, F, G: TP 39 FP 55 FN 10, F 0.545 at 0, -10, -20, -30 dB and 0.563 at -40 dB: level independent (adaptive threshold). So **level normalisation is not needed for `detect_onsets`**; the research's level finding concerns a fixed threshold, which the new picker has.

**Sweep of the new pipeline (pooled A, A0, B, C, D, F, G; log_gain x lag x delta_activation):** best F 0.807 at log_gain 1, lag 15 ms, delta 0.02-0.05 (plateau); log_gain 1000 gives 0.62-0.78 and a 10 ms lag collapses G (1 of 16). Chosen defaults: `TransientParams.log.log_gain = 1.0`, `flux.lag = 15 ms`, `peaks.delta_activation = 0.05`. A sweep of picker window sets (LGD 30/30/100/70, CPJKU 10/50/150/0, two hybrids) moved the pooled F by at most 0.02 (best 0.825 for 10/30/100/0 and 10/50/100/70): the picker windows are not what separates this from the reference on C and G. Tuned on the signals scored: optimistic.

**Level (pooled A, A0, B, D, F, G), new pipeline:** without P0 F 0.878 / 0.851 / 0.844 / 0.876 / 0.000 at 0 / -10 / -20 / -30 / -40 dB (nothing found at -40 dB); with P0 0.887 at all five. P0 is what makes the fixed threshold level independent.

**Per family, new pipeline with P0 (TP/FP/FN at +-25 ms, F at +-20 ms, median offset):** A 8/0/0, 1.000, +3.0 ms; A0 8/0/0, 1.000, +3.0; B 7/0/1, 0.933, +4.0; C 3/5/5, 0.375, -13.9; D 1/0/0, 1.000, +3.8; E 8 of 8 consonant starts found (7 of 8 vowel starts, 9 extra detections are the consonants); F 8/0/0, 1.000, +3.2; G 11/5/5, 0.062, -22.9 ms.

**Cross-check against the research's C++ reference** (`scripts/phase5_superflux_check.py`; its scale differs, lag set to its 10 ms): activation correlation 0.77 (A), 0.78 (A0), 0.93 (B), 0.96 (C), 0.996 (D), 0.73 (E), 0.76 (F), 0.94 (G): the same curve where the notes are soft or sustained, less so for sharp attacks (the reference uses unnormalised triangles and a symmetric Hann, 220.5-sample frames). Detections against the truth at +-25 ms: reference A, A0, B, D 8/0/0 (-5/+5 ms), C **8/0/0**, G **16/0/0**, E 8/8/0, F 8/1/0; ours A, A0, B, D, F 8/0/0, **C 2/6/6, G 1/15/15 at lag 10 ms** (11/5/5 at 15 ms). So the reference finds the legato and fast families that this pipeline does not; the picker windows are ruled out above. Suspects, untested: the filterbank normalisation (unit-sum mean against unnormalised triangle sums changes the relative weight of low and high bands and the effective log compression), the STFT frame offset and window shape. Left as an open item.

## Open items after this stage
1. C (legato) and G (fast notes) are worse than the reference's; find out whether the filterbank normalisation closes the gap.
2. G is localised about 23 ms early by the Phase 3 refinement; the research's aubio variants are within +-13 ms.
3. P2 (adaptive whitening) is implemented and unit-tested as a stage only; its form against Bock 2012 is unchecked, no pipeline default uses it, and the whitened path needs its own `log_gain` and a quiet-band-noise test.
4. The `analyse()` decision (new pipeline or not) needs an ADR with the Phase 3 golden numbers at the annotators' F20 window and at +-25 ms, and a re-tune of `merge_onset_strength` (ADR-0010a), since the strength scales differ.
5. Real material: everything above is synthetic. The Vocadito subset is the only real audio; its annotations are notes, not every consonant.
6. The Phase 4 close-out (see above) is still open.

## Corrections and later work (2026-10-22, after the structure review of ADR-0022)
- **The false-positive attribution above was wrong.** The 23 false positives in family D and 8 in F belong to the energy-step detector (ADR-0010a), which `detect_onsets` and `analyse()` run after either spectral method. With it off, both the Phase 3 chain and the pipeline give D 1/0/0 and F 8/0/0. The pooled F of 0.887 above is the pipeline *without* energy steps and with `detect_transients`; the fair comparisons are in ADR-0022 (full `detect_onsets`: 0.601 against 0.545; spectral only: 0.887 against 0.804). The pipeline's real bench advantage is fast notes (G: 11 of 16 against 6) and legato (C: 3 against 2), at the cost of one soft attack (B: 7 of 8 against 8).
- **`delta_activation` 0.05 is what ships.** 0.07 (a real-data plateau value) misses all 8 soft attacks of family B on the bench.
- **Structure after the review:** `onset_stages.cpp` was split into `stages/{band_spectrogram,level_norm,spectral_flux,peak_pick,localise}` (header and source each) and `transient_pipeline`; `onset_stages.h` is now an umbrella; `onsets.cpp` (109 lines) keeps the glue, with `detail/{raw_onset,spectral_onsets,merge_onsets,onset_params_check,stage_check}`; one `check_onset_params` validates both methods (the shared fields always, the Phase 3 fields for the Phase 3 chain); the adaptive picker is `pick_peaks_adaptive`. `band_spectrogram` and `pick_peaks_windowed` were split into named helpers.
- **Tools:** `vocadito_report` takes `ANNO_TUNE` keys `method` (0 = Phase 3, 1 = pipeline), `pgain`, `pdelta`, `plag`, `ppremax`, `ppostmax`, `ppreavg`, `ppostavg`, `plevel` and `estep` (energy-step threshold in dB; a large value turns the steps off).
- **Engine tests** use a fixture pinned to the Phase 3 chain (one-frame grid shift, see ADR-0022 Consequences).

## The legato and fast-note gap (2026-10-22)
Cause found, not by tuning but by looking at where the detections were lost. The activation curves of this pipeline and of the research's reference are proportional (same relative peak heights at every true onset of family G: 10-15 times the mean), and the pipeline's raw peaks hit every onset of G (16 of 16, median -2.7 ms at a 10 ms lag, +1.2 ms at 15 ms) and of C (8 of 8, median -14.5 ms: the start of the 20 ms cross-fade). The loss was in `localise`: it scanned for the halfway crossing from the start of its +-25 ms window, and with the previous note's 40 ms release tail still above the halfway level (between a -100 dB floor and a -9 dB top) it reported the window start, about 20-25 ms early; at a 10 ms flux lag the displacement pushed 15 of 16 onsets outside +-25 ms. Fix: `LocaliseScan::from_quietest_point` starts the scan at the last quietest point at or before the peak (default for the pipeline; `from_window_start` stays for the Phase 3 chain, whose output is pinned bit for bit). Result on the bench (spectral only): G 16/0/0 (+3 ms), C 7/1/1, pooled A, A0, B, D, F, G F 0.990; on the real corpus onset F20 +0.03 on both sets (ADR-0022). A filter-normalisation option (`FilterNorm::unit_peak`, the reference's unnormalised triangles) was tried first and did not change the picture; it stays as a documented option (default `unit_sum`, Phase 3). `localise` was cut into `measure_region` and `refine_to_energy_rise`.

Still open: the legato family is found at the start of its cross-fade (-14 ms), one soft attack (B) is missed at `delta_activation` 0.05, and the energy-step detector false-fires on synthetic tremolo and noise (D: 23, F: 8) in both methods.

