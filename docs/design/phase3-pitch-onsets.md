# Design note: Phase 3 — pitch (f0) tracking and onset detection

- **Phase:** 3
- **Status:** draft
- **Spec sections:** architecture §3 (`PitchTrack`, `OnsetList`), §4.2 (`detect_pitch`, `detect_onsets`); implementation guide B1, B2; ROADMAP Phase 3
- **Folders / CMake targets:** `src/core/analysis/pitch` and `src/core/analysis/onsets` (layer 1; link `core/types`, `core/dsp`)
- **Depends on:** `phase3-dsp.md`

## Purpose
Produce the two frame-level inputs of note segmentation: a pitch track (f0 in cents, voicing, confidence, octave alternatives) and a list of onsets (attack, consonant, pitch-change). Pitch is the most important analysis stage: every later tool inherits its errors, so it is judged on synthetic ground truth (±3 cents) before any real recording.

## 1. Pitch

### Types
```cpp
struct PitchParams {
    Hz min_f0{50.0};
    Hz max_f0{1600.0};
    Seconds hop{0.005};              // PitchTrack grid hop
    Hz analysis_rate{24000.0};       // the signal is resampled to this before analysis (never above the source rate)
    Seconds window{0.040};           // YIN integration window; must cover >= 2 periods of min_f0 (checked)
    double bin_cents = 10.0;         // pitch resolution of the HMM
    double beta_mean = 0.1;          // pYIN threshold prior (Beta distribution mean)
    double voiced_to_unvoiced = 0.01, unvoiced_to_voiced = 0.01;
    double max_jump_cents = 80.0;    // largest f0 change per hop the HMM allows (80 cents / 5 ms)
    double octave_penalty = 0.5;     // extra cost of jumping by ~1200 cents, in -log units
    int n_alternates = 2;
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

class PYinEstimator final : public PitchEstimator { /* name "pyin" */ };

/// Spec entry point: runs the default (pYIN) estimator.
[[nodiscard]] Result<PitchTrack> detect_pitch(const AudioSource& src, const PitchParams& params);
```
Time axis: the returned `PitchTrack::grid` is on the **source** axis: `start = SourceTime{}`, `hop = params.hop`, `n_frames = floor(duration / hop) + 1`; frame i is analysed over a window centred on `i * hop`. Units: `f0` is `Cents` absolute (A4 = 6900), NaN when unvoiced; `voicing_prob`, `confidence_prob` in 0..1.

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
    Seconds hop{0.005};
    int n_fft = 2048;                       // at 44.1/48 kHz; scaled with the sample rate to ~43 ms
    int filterbank_bands_per_octave = 24;   // log-spaced triangular filters 27.5 Hz..16 kHz (SuperFlux)
    double log_lambda = 1.0;                // log(1 + lambda * |X|)
    int max_filter_bins = 3;                // frequency max filter (vibrato suppression)
    Seconds lag{0.015};                     // difference lag (>= 2 frames)
    Seconds threshold_window{0.5};          // adaptive threshold: local mean + delta * local spread
    double delta = 2.0;
    Seconds min_gap{0.030};
    Cents pitch_change_threshold{80.0};     // f0 step between stable regions counted as an onset
    Seconds pitch_stable{0.040};            // each side of a step must be stable this long
    Seconds refine_window{0.010};           // energy-slope refinement search before the peak
};

/// Errors: invalid_argument (bad params), out_of_range from the source.
/// `pitch` may be null (spectral onsets only). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<OnsetList> detect_onsets(const AudioSource& src, const PitchTrack* pitch, const OnsetParams& params);
```
Method (Böck & Widmer 2013, SuperFlux): STFT (hop = `hop`) → log-filterbank magnitude → frequency max filter over `max_filter_bins` → positive lagged difference summed over bands → **level-independent** normalisation (divide by a local floor, so quiet passages are not missed) → adaptive-threshold peak picking with `min_gap` → refinement of each peak to the point of steepest RMS rise within `refine_window` before it (sample-accurate attacks). **Pitch-change onsets**: from the pitch track, a step of ≥ `pitch_change_threshold` between two regions each stable for `pitch_stable` is a legato note change (`OnsetKind::pitch_change`); spectral onsets within `min_gap` of a pitch-change onset are merged (the stronger kept, spectral kind wins when energy rises). `OnsetKind::attack`/`consonant` is decided by whether the frame before the onset is unvoiced/low level (consonant) or not. Phoneme cues (`OnsetKind::phoneme`) are not produced in Phase 3.

Output: `OnsetList` sorted by time, parallel arrays valid per `validate`; `strength` is the normalised ODF value at the peak (≥ 0).

## Effect on audio
None (analysis only).

## Split / join decisions
- `PitchEstimator` is an interface although only one implementation exists now: allowed explicitly by coding-standards §5 (join rule 5, estimators) because the neural estimators are planned for Phase 13.
- The octave-alternative search stays inside the estimator (same state space) — keep-together rule 1.
- Onsets are a separate target from pitch (different cues, different tuning); pitch-change onsets take the pitch track as an optional input rather than recomputing it (split function rule 4).

## Dependencies
`core/types`, `core/dsp`. No external libraries.

## Tests
- Unit (synthetic ground truth): pure tones 80-1200 Hz → f0 within 3 cents (clean; target ≤ 1 cent recorded); vibrato 5.5 Hz ±50 cents → track follows within 5 cents rms; linear glide (1 octave/s) within 5 cents; harmonic tone (10 partials) in white noise at 20 dB SNR → 10-cent RPA ≥ 95 %; weak-fundamental tone (partial 1 at −20 dB) → no octave error (alternates contain the octave); silence and noise → unvoiced (false-alarm rate reported); `alternates` shape; params validation; determinism.
- Onsets: tone-burst train → onsets ±5 ms; vibrato tone (±50 cents) produces no more false onsets than allowed (≤ 1 over 3 s) while plain flux on the same signal produces more (the SuperFlux effect is checked, not assumed); legato pitch step (A4→B4, no level dip) → one `pitch_change` onset ±15 ms; quiet burst 40 dB below a loud one still detected; empty/short signals return empty lists.
- Contract: `validate` passes on every output; times strictly increasing.

## Open questions
1. 10-cent bins vs 20: 10 cents is the pYIN default; revisit only if debug-build speed forces it.
2. Whether `analysis_rate` 24 kHz is high enough for 1600 Hz with a good sub-cent refinement; verified by the synthetic tests (raise to 32 kHz otherwise; recorded in the review).

## Review log
*(structure-reviewer findings go here)*
