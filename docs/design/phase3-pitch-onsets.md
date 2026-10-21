# Design note: Phase 3 — pitch (f0) tracking and onset detection

- **Phase:** 3
- **Status:** implemented (reviewed in two rounds; see Review log and Implementation notes)
- **Spec sections:** architecture §3 (`PitchTrack`, `OnsetList`), §4.2 (`detect_pitch`, `detect_onsets`); implementation guide B1, B2; ROADMAP Phase 3
- **Folders / CMake targets:** `src/core/analysis/pitch` and `src/core/analysis/onsets` (layer 1; link `core/types`, `core/dsp`)
- **Depends on:** `phase3-dsp.md`

## Purpose
Produce the two frame-level inputs of note segmentation: a pitch track (f0 in cents, voicing, confidence, octave alternatives) and a list of onsets (attack, consonant, pitch-change). Pitch is the most important analysis stage: every later tool inherits its errors, so it is judged on synthetic ground truth (±3 cents) before any real recording.

## 1. Pitch

### Types
```cpp
/// Generic parameters of any estimator.
struct PitchParams {
    Hz min_f0{50.0};
    Hz max_f0{1600.0};
    Seconds hop{0.005};              // PitchTrack grid hop (a whole number of source samples is not required: frames are at i * hop)
    int n_alternates = 2;
};

/// pYIN-only parameters, held by PYinEstimator (other estimators have their own).
struct PYinParams {
    Hz analysis_rate{24000.0};       // the signal is resampled to this before analysis (never above the source rate)
    Seconds window{0.040};           // YIN integration window; must cover >= 2 periods of min_f0 (checked at estimate())
    double bin_cents = 10.0;         // pitch resolution of the HMM
    double beta_mean = 0.1;          // threshold prior (Beta distribution mean)
    double voiced_to_unvoiced_prob = 0.01;
    double unvoiced_to_voiced_prob = 0.01;
    double max_jump_cents = 80.0;    // f0 change per hop with the smooth (triangular) transition
    double jump_prob = 1e-4;         // floor probability of ANY larger jump, so a legato step (200 cents in one hop) is representable at a cost
    double octave_penalty = 0.5;     // extra cost of jumping by ~1200 cents, in -log units
};

/// Interface so a neural estimator (RMVPE/PESTO/CREPE, Phase 13) can replace the DSP one.
class PitchEstimator {
public:
    virtual ~PitchEstimator();
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;
    /// Errors: invalid_argument (bad params, window shorter than two periods of min_f0),
    /// out_of_range from the source. Pure; thread-safe; not real-time safe.
    [[nodiscard]] virtual Result<PitchTrack> estimate(const AudioSource& src, const PitchParams& params) const = 0;
};

class PYinEstimator final : public PitchEstimator {
public:
    explicit PYinEstimator(PYinParams params = {});   // name "pyin", version 1
};

/// Spec entry point: runs the default (pYIN) estimator.
[[nodiscard]] Result<PitchTrack> detect_pitch(const AudioSource& src, const PitchParams& params);
```
Time axis: the returned `PitchTrack::grid` is on the **source** axis: `start = SourceTime{}`, `hop = params.hop`, `n_frames = floor(duration / hop) + 1` (0 for an empty source); frame i is analysed over a window centred on `i * hop`.
Edge cases: an empty source gives an empty valid track; a source shorter than the window is zero-padded; a source rate below `2 * max_f0` → invalid_argument (cannot represent the range; the message says so); a source rate below `analysis_rate` is analysed at its own rate. Stereo is mixed to mono first (`read_mono`). Units: `f0` is `Cents` absolute (A4 = 6900), NaN when unvoiced; `voicing_prob`, `confidence_prob` in 0..1.

### Method (Mauch & Dixon 2014, pYIN; de Cheveigné & Kawahara 2002, YIN)
1. Mono mix → `resample` to `analysis_rate` (a pure copy if the source rate is lower).
2. Per frame: YIN difference function by FFT autocorrelation (`RealFft`), cumulative-mean-normalised difference `d'(τ)`, for τ in `[rate / max_f0, rate / min_f0]`.
3. Candidates: for each of 100 thresholds weighted by a Beta prior with mean `beta_mean`, take the first local minimum of `d'` below the threshold (smallest-τ rule) and add its weight; unthresholded mass goes to "unvoiced". Each candidate τ is refined by parabolic interpolation, then to **sub-cent precision** by a fractional-lag search that maximises the normalised cross-correlation over the frame with band-limited (sinc) interpolation (the f0 error target on a clean synthetic tone is ≤ 3 cents; measured and recorded).
4. HMM over `bin_cents` pitch bins × {voiced, unvoiced}; observation probability from the candidate masses, transitions limited to `max_jump_cents` per hop with a triangular weight, an extra `octave_penalty` for ±1200-cent jumps, voiced↔unvoiced switching costs from the two probabilities; Viterbi decoding.
5. `alternates`: re-run Viterbi with the bins within ±75 cents of the chosen path(s) zeroed (octave candidates for Note Assignment), `n_alternates` paths, each `grid.n_frames` long; NaN where unvoiced.
6. `voicing_prob` = filtered posterior of the voiced states; `confidence_prob` = voicing times the normalised candidate mass at the chosen bin.

### Cost
≈ one FFT autocorrelation (N = 2 × window × rate) per hop plus the Viterbi pass: far faster than real time in release (the ASan debug build is slower; tests use ≤ 3 s signals).

## 2. Onsets
```cpp
struct OnsetParams {
    Seconds hop{0.005};                     // rounded to whole samples; onset times use the actual hop (hop_samples / rate)
    Seconds window{0.043};                  // STFT window; n_fft = the power of two nearest to window * rate (2048 at 44.1/48 kHz)
    int filterbank_bands_per_octave = 24;   // log-spaced triangular filters 27.5 Hz..16 kHz (SuperFlux)
    double log_lambda = 1000.0;             // log(1 + lambda * |X|) on magnitudes of a [-1, 1] signal: strong compression, so quiet and loud onsets give comparable activation
    int max_filter_bins = 3;                // frequency max filter (vibrato suppression)
    Seconds lag{0.015};                     // difference lag (>= 2 frames)
    Seconds threshold_window{0.5};          // adaptive threshold: local mean + delta * local spread
    double delta = 2.0;
    Seconds min_gap{0.030};
    Cents pitch_change_threshold{80.0};     // f0 step between stable regions counted as an onset
    Seconds pitch_stable{0.040};            // each side of a step must be stable this long
    Seconds refine_window{0.025};           // the energy-slope refinement searches +- this around the activation peak (the peak lags the true onset by up to half the STFT window)
    double min_activation = 0.15;           // absolute floor on the activation (mean positive log-difference per band) so silence and noise ripple cannot trigger
};

/// Errors: invalid_argument (bad params), out_of_range from the source.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<OnsetList> detect_onsets(const AudioSource& src, const OnsetParams& params);          // spectral onsets only
[[nodiscard]] Result<OnsetList> detect_onsets(const AudioSource& src, const PitchTrack& pitch, const OnsetParams& params);  // plus pitch-change onsets and voicing-based kinds
```
Method (Böck & Widmer 2013, SuperFlux): STFT (hop = `hop`) → log-filterbank magnitude → frequency max filter over `max_filter_bins` → positive lagged difference summed over bands → **level-independent** normalisation (divide by a local floor, so quiet passages are not missed) → adaptive-threshold peak picking with `min_gap` → refinement of each peak to the point of steepest short-window log-energy rise within `refine_window` around it (sample-accurate attacks). The activation is the **mean** (not sum) positive log-difference over the bands, so its scale does not depend on the filterbank size. **Pitch-change onsets**: from the pitch track, a step of ≥ `pitch_change_threshold` between two regions each stable for `pitch_stable` is a legato note change (`OnsetKind::pitch_change`). Strength scale: **both kinds are normalised so 1.0 is the detection threshold** (spectral: ODF / adaptive threshold; pitch change: step_cents / `pitch_change_threshold`), which makes "the stronger is kept" meaningful when a spectral onset lies within `min_gap` of a pitch-change onset (the spectral kind wins ties because it carries the sample-accurate time). `OnsetKind::attack`/`consonant` is decided by the voicing probability of the pitch frame 20 ms AFTER the onset (the sound that follows): < 0.5 (unvoiced, noisy start such as s, t, sh) → consonant, else attack (a silent frame before the onset says nothing about the note); **without a pitch track every spectral onset is `attack`** (no voicing data; `consonant` classification needs the pitch overload). The interaction with the note HMM (which has its own 33-cent bins) is tested end to end: a legato step must give two notes. Phoneme cues (`OnsetKind::phoneme`) are not produced in Phase 3.

Output: `OnsetList` sorted by time, parallel arrays valid per `validate`; `strength` is the normalised ODF value at the peak (≥ 0).

## Effect on audio
None (analysis only).

## Split / join decisions
- `PitchEstimator` is an interface although only one implementation exists now: allowed explicitly by coding-standards §5 (join rule 5, estimators) because the neural estimators are planned for Phase 13.
- The octave-alternative search stays inside the estimator (same state space) — keep-together rule 1.
- Onsets are a separate target from pitch (different cues, different tuning); pitch-change onsets take the pitch track as an optional input rather than recomputing it (split function rule 4).

## Dependencies
`core/types`, `core/dsp` (`read_mono`, `resample`, `RealFft`, `stft`). No external libraries. Each module has a `README.md` (purpose, method, references, limitations, tuning parameters); `-ffp-contract=off` on both targets.

## Tests
- Gate (ROADMAP, recorded precisely): **stationary pure tones 80-1200 Hz: every voiced frame within ±3 cents** (target ≤ 1 cent measured and recorded); **vibrato (5.5 Hz ±50 cents), a linear glide (1 octave/s) and harmonic tones (10 partials; clean and in white noise at 20 dB SNR): 10-cent RPA ≥ 95 % against the instantaneous truth at frame centres** (±3 cents is not demanded of moving pitch: the frame window averages the curve, a measurement effect recorded in the review). The clean sets run at source rates 22.05, 32, 44.1 and 48 kHz and mono/stereo. **`octave_errors == 0` on the chosen f0** for the harmonic and the weak-fundamental tone (partial 1 at −20 dB), and the octave appears in `alternates`.
- Other: silence and noise → unvoiced (false-alarm rate reported); `alternates` shape (each `grid.n_frames` long); params validation (window shorter than two periods, rate below 2×max_f0); edge inputs (empty source → empty valid track, shorter than the window); determinism. The synthetic generator is a deliverable: `tests/support/synth_signals.h` (harmonic tone with vibrato/glide/steps/bursts and seeded noise), tolerances in `tests/tolerances.h`.
- Onsets: tone-burst train → onsets ±5 ms; vibrato tone (±50 cents) produces no more false onsets than allowed (≤ 1 over 3 s) while plain flux on the same signal produces more (the SuperFlux effect is checked, not assumed); legato pitch step (A4→B4, no level dip) → one `pitch_change` onset ±15 ms; quiet burst 40 dB below a loud one still detected; empty/short signals return empty lists.
- Contract: `validate` passes on every output; times strictly increasing.

## Open questions
0. Naming: `_prob` suffixes on unit-less probabilities per the naming rule.
1. 10-cent bins vs 20: 10 cents is the pYIN default; revisit only if debug-build speed forces it.
2. Whether `analysis_rate` 24 kHz is high enough for 1600 Hz with a good sub-cent refinement; verified by the synthetic tests (raise to 32 kHz otherwise; recorded in the review).

## Review log
*(structure-reviewer findings go here)*

## Implementation notes
**Pitch**
- Files: `pitch_estimator` (interface, `detect_pitch`), `pyin` (orchestration, lag-centre correction), `pyin_observe` (candidates), `pitch_hmm` (Viterbi), `yin` (difference function by FFT autocorrelation). `PYinParams` is held by `PYinEstimator`; `PitchParams` is generic.
- `octave_penalty` was **dropped**: any jump larger than `max_jump_cents` costs -log(`jump_prob`) = 9.2 nats, which already makes an octave jump improbable, and a separate penalty on the "any jump" path would need an O(bins^2) loop. Octave alternatives come from weak octave candidates (mass 1e-3 of the strongest) and the exclusion-based alternate paths.
- `voicing_prob` is the observed voiced mass of the frame (sum of candidate masses), forced to the side of 0.5 chosen by the Viterbi path (>= 0.5 on voiced frames, < 0.5 on unvoiced); `confidence_prob` is the chosen candidate's mass (voiced) or the unvoiced mass.
- YIN measures at the middle of its two windows (frame time + lag / 2); each voiced run is resampled back onto the frame grid (`compensate_lag_centre`), which keeps vibrato and glides within the 10-cent gate at low pitches.
- Refinement is the parabolic peak of the normalised cross-correlation (no fractional-lag search was needed). **Measured:** stationary harmonic tones 80-1200 Hz at 44.1 and 48 kHz: maximum error 0.92 cents (gate 3 cents).
- Debug-ASan cost: the `pitch_test` binary takes about 25 s (every case runs 0.6-1 s of audio at 200 frames/s); release is far faster than real time.

**Onsets**
- `OnsetParams::window` (43 ms) replaced `n_fft`; `min_activation` (0.15) was added; `log_lambda` is 1000 (magnitudes of a [-1, 1] signal) and `refine_window` 25 ms (the activation peak lags the true onset by up to half the STFT window).
- A peak where the signal is clearly louder at the start of the refinement region than at its end is an **offset** and is dropped (spectral flux responds to the end of a note). A spectral onset **without** a 6 dB energy rise (legato) that meets a pitch-change onset within `min_gap` is that pitch change; one **with** an energy rise wins.
- Pitch-change onsets are found between two voiced frames separated by at most 60 ms of unvoiced frames (the f0 estimate blends across a step: a 200-cent legato step is unvoiced for about 35 ms), each side voiced and stable for 40 ms; strength = step / 80 cents.
- SuperFlux's frequency max filter is demonstrated on +-200 cents vibrato with 25 partials (plain flux: 5 onsets, SuperFlux: at most 1); +-300 cents defeats both.