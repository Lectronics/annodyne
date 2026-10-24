# Design note: the maximum voiced frequency per frame (MVF)

- **Phase:** 6
- **Status:** approved (structure review round 2, ADR-0033 accepted by the owner 2026-10-23); implementing in slices
- **Spec sections:** `docs/specs/annodyne-implementation-guide.md` (voicing, the harmonic + noise model); ADR-0017 (frame classes; item 4 and the amendment of 2026-10-21 that deferred the per-frame MVF), ADR-0018, ADR-0031 item 4, ADR-0033 (written with this note), `docs/design/vocal-resynthesis-research.md` (line 172 defines an MVF from band aperiodicity)
- **Folders / targets:** `src/core/types` (`NoiseTrack`, layer 0), `src/core/analysis/harmonic_noise` (layer 1, produces it), `src/core/dsp` (the band split, layer 0), consumers `src/core/synthesis/harmonic_noise` and `src/core/synthesis/rubberband_pitch` (layer 2)

## Purpose
In a breathy or mixed-voice frame only the low part of the spectrum is periodic. Pitch shifting must move the periodic part and leave the noise as it was (ADR-0017 item 4, ADR-0031 item 4; Melodyne, Cubase and the Auto-Tune patent leave unpitched material alone). The frequency that separates the two is the **maximum voiced frequency (MVF)**. `measure_frames` already computes it for every frame (`FrameMetrics::mvf_hz`, `harmonic_noise.cpp:467`) to decide the `breathy` class and then discards it: `Note` keeps only the class. This note stores a consumer-facing MVF curve and defines two opt-in consumers.

## What the existing metric is (facts checked in review)
- `mvf_hz` is per 5 ms frame: `max(lower edge of the first aperiodic band, 0.75 f0)` (`:324-338`), `edges.back()` (= `min(0.45 rate, 20 kHz)`) when every significant band is periodic, and 0 when `voicing_prob < k_voiced_probability`. It is not floored at 500 Hz.
- It is 0 for probability-unvoiced frames, not for classes: frames classed `silence` or `plosive` can carry a non-zero value, and the median/run smoothing can label a probability-unvoiced frame `voiced`.
- It is tested only by `tests/unit/harmonic_noise_test.cpp:312` (a clean vowel: `mvf_hz > 1500`); there is no accuracy test against a known cut-off. Test 1 below is the first.
- Limit found in implementation: harmonics are masked only up to `PartialsParams::max_harmonics` (60), so a voice whose partials extend past 60 f0 (13 kHz at 220 Hz) reads as aperiodic above that and the metric cannot exceed about 60 f0. Harmless for the 500 Hz-3 kHz cut-offs consumers care about; Test 1's fully-harmonic case uses 50 partials.
- The classes use the **raw** metric; they must go on doing so (S2).

## Public types
| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `NoiseTrack` (existing) | struct | `max_voiced_hz` is empty or has `grid.n_frames` entries | the `Note` | value, inside a shared immutable analysis snapshot |

```cpp
struct NoiseTrack {
    FrameGrid grid;
    FrameMatrix band_energy_linear;  // frames x bands
    std::vector<FrameClass> frame_class;
    std::vector<Hz> max_voiced_hz;   // per frame, source time of the grid: the frequency below which
                                     // the frame is periodic; 0 = no periodic part; empty = not measured
};
```
The vector is a **derived curve for consumers**, not the classifier's metric (S1). Rule that fills it (in `split_harmonic_noise`, after the class routing and smoothing): 0 for frames of class `silence`, `plosive`, `sibilant`, `breath` and for frames whose raw voicing probability is unvoiced; for every other frame the raw `mvf_hz` clamped to `[mvf_floor, analysis ceiling]` (`edges.back()`; the clamp to the ceiling is a no-op, written down so Test 1's "fully harmonic gives the ceiling" is exact at every rate); then a median over `median_frames` (the existing parameter, 5) **within each run of non-zero frames**, so zeros are never mixed into values and the voiced/unvoiced edge stays sharp. A note shorter than the window uses a smaller odd window.

## Public functions
`split_harmonic_noise` fills the field; `validate(const NoiseTrack&)` checks it; nothing else is added to a public header except the dsp function below.

```cpp
/// ... also fills NoiseTrack::max_voiced_hz as described in the design note (derived curve).
[[nodiscard]] Result<NoiseTrack> split_harmonic_noise(...);

/// ... Errors: invalid_argument if max_voiced_hz is neither empty nor n_frames long, or holds a
/// value that is not finite or is negative. (The Nyquist limit is not checked here: a NoiseTrack has
/// no sample rate; split_harmonic_noise guarantees it.)
[[nodiscard]] Result<void> validate(const NoiseTrack& track);

/// core/dsp: splits `x` at a time-varying cut-off. low = STFT low-pass with a smooth transition
/// (raised cosine in frequency, `transition_hz` wide; the caller chooses it, the Rubber Band engine uses at least 1.5 x the note's pitch), high =
/// x - low (exact by construction, so low + high == x to float rounding: amplitude-complementary
/// masks, M_lo + M_hi = 1, not power-complementary). 2048-point Hann analysis, hop 512, synthesis
/// window and overlap-add normalised by the window sum. cutoff_hz has one value per sample, a raw-double span
/// because it is a per-sample kernel buffer (the unit is in the name; the caller interpolates the
/// frame curve from the `Hz` vector); a cut-off of 0 gives low = 0.
struct BandSplit { std::vector<float> low; std::vector<float> high; };
[[nodiscard]] Result<BandSplit> split_band(std::span<const float> x, Hz sample_rate,
                                           std::span<const double> cutoff_hz, Hz transition_hz);
```
New parameter: `HarmonicNoiseParams::mvf_floor` 500 Hz (range-checked in `validate_params`: finite, > 0, below the ceiling). The median reuses `median_frames`. The ceiling is the analysis ceiling, not a new parameter (S3). Pure; thread-safe; not real-time safe. No extra analysis pass.

## How the MVF is derived (ADR-0031 asks for the choice)
| Option | What | Evidence | Cost and risk |
|---|---|---|---|
| **A. The in-repo estimate** | harmonic bins masked, the rest of each mel band is noise density; a band is aperiodic when noise exceeds `aperiodicity_threshold`; MVF = the lower edge of the first aperiodic band | already decides `breathy`; 18.7 % of `vocadito` frames are `breathy` by this rule (a calibration item in the README); thresholds are guesses | free; resolution is a mel band, wider with frequency (check `n_bands`, `k_band_max_hz` = 20 kHz before promising a tolerance: S6) |
| B. WORLD D4C | coarse aperiodicity in 3 kHz-spaced bands; the research doc line 172 defines the MVF as the lowest band whose aperiodicity exceeds 0.5 (the author's guess) | tested code, modified BSD | a dependency for a 3 kHz grid |
| C. Drugman and Stylianou (arXiv:2006.00521) | a dedicated MVF estimator | paper only | our own code |

**Choice: A, stored now. B as an offline cross-check, C only if A fails.** The cross-check (B7) stays **outside the CMake build**: a Python script with `pyworld` run by hand over the committed `vocadito` tracks and the synthetic signals, and only the resulting table is committed (`docs/reviews/`); no WORLD/SPTK code enters `third_party` or any target until ADR-0019's trial or a failure of A makes it necessary (then an ADR and an AnnoLayers entry, bench-only, linked by no `src/` target).

## Consumers (each behind an option that is off by default until the owner has listened)
### 1. `HarmonicNoiseEngine` (breathy frames)
Per frame the harmonics `k` with `k f0 < MVF - f0/2` are shifted in full, those within `MVF +- f0/2` by a raised-cosine weight, those above not at all; the residual stays pinned as today. The harmonic count changes from frame to frame; the existing `class_edge_fade` cross-fade applies where the shifted set changes between frames (S8).

### 2. `RubberBandPitchEngine`: `ShiftScope::below_mvf` (S4 version bump, `settings_key` carries the scope)
`out = LP(shift(LP(x))) + (x - LP(x))` with `LP` = `split_band(..., MVF(t), width).low` at the same cut-off: the low band is shifted, then low-passed again at the MVF so harmonics that the shift pushed above it are removed (no double count with the untouched high band; B4); the high band is the original, exactly. A downward shift leaves the region between `MVF x ratio` and `MVF` without periodic content; that region is by definition near noise and is left empty (documented limit; test below).
- **Pitch edits only.** `formant_offset != 0` with `below_mvf` is `invalid_argument` (the formant estimate would run on a band-limited signal; B5). Modulation/drift scales and constant offsets are both wrapped (live-shifter path included).
- **Zone and context.** The MVF curve is the note's, interpolated linearly between frame centres per sample, held from the edge frame beyond the note's first/last frame (same convention as the pitch curves), 0 where the note has no frames. A zero cut-off gives low = 0, so frames with no periodic part are returned unchanged and the cut-off sweeping to 0 over one hop is the fade between shifted and unshifted audio (no extra cross-fade; B5). Where two touching notes' zones overlap (ADR-0030) each uses its own MVF; the renderer's ramp blends two signals that agree above their cut-offs and below (same pitch curve); measured, not assumed (Test 8).
- **Empty `max_voiced_hz`** (not measured): the engine falls back to the full-band shift (the default scope behaviour); it is not an error.
- A shift below `k_neutral_snap_offset` returns the original samples exactly in this scope too.
- **Alignment.** The shifted low band is aligned to the input exactly as the full-band path aligns its output (same context and start-delay handling), so the sum with `x - LP(x)` does not comb. Test 7 includes an alignment test: the onset of the scoped render's low band lands within 2 ms of the full-band shift's (waveform equality is not expected: a phase vocoder's output phases depend on its whole input; measured -3.9 dB difference with no lag fixing it; also a shift of the whole voice against its own original cannot be a null test: a 10-cent shift moves a 2 kHz partial by 12 Hz).

## Effect on audio
None until an option is on. The field is analysis data; no default render reads it; an unedited note never reaches an engine (rule 5).

## Data model, JSON and cache effects
- Fingerprint: `render_key.cpp:184-186` hashes the noise members, so the new vector enters the note hash (empty and non-empty differ; `put_all` writes a length prefix). `k_key_format_version` goes 8 -> 9 with its changelog line, and the engines' `version()` / `settings_key()` change. The withdrawn branch `phase-6-core-only` also used 9; it is not merged, noted so a revival renumbers.
- `AnalyseParams::version` is stamped by the caller and is not in the render key; no golden hashes an analysis (`golden_vocals_test.cpp` scores voicing recall, pitch accuracy, onsets and note pitch), so the Phase 4 guard is unaffected provided the classes stay byte-identical (Test 3).
- JSON (B1): `cli/analysis_json` only writes and emits sound-class runs; there is no reader. The writer adds `mvf_hz` (the median of the stored curve over the run) to each `sound_classes` entry whose curve is non-empty. Write-only; its test checks the key and the value on a synthetic note.
- Nothing builds a `NoiseTrack` with designated initialisers except `harmonic_noise.cpp:636` (it will name the new field); tests assign members, and the field defaults to empty.

## Tests (first)
1. **Accuracy.** A harmonic tone (f0 200 Hz, 40 partials) low-passed at a known cut-off plus white noise above it at -10 to -20 dB (not below the 30 dB `band_significance` edge, which would read as fully voiced: S7), cut-offs 1.5, 3 and 5 kHz: the stored MVF is within the width of the mel band at the cut-off (computed from the band edges, S6) or one harmonic, whichever is larger. A second case places the noise at -35 dB and expects the fully-voiced ceiling. Fully harmonic gives exactly the ceiling; white noise and silence give 0.
2. **Rule.** The vector has `n_frames` entries, is finite, within `[mvf_floor, ceiling]` where non-zero, and 0 exactly on silence/plosive/sibilant/breath frames and on probability-unvoiced frames, including a frame the class smoothing relabelled `voiced`; a note shorter than the median window works.
3. **Classes unchanged.** Frame classes are byte-identical before and after the change on the synthetic scenes and the committed `vocadito` tracks (the classifier reads the raw metric).
4. **`analyse()`** end to end: `note.noise.max_voiced_hz.size() == n_frames`.
5. **`validate`:** accepts empty; rejects a wrong length, NaN and a negative value. **Fingerprint:** two notes differing only in `max_voiced_hz` hash differently; the key version is 9.
6. **`split_band`** (dsp): `low + high == x` to float rounding for constant and for moving cut-offs; a cut-off of 0 gives low = 0 and high = x exactly; the Hann/OLA normalisation gives a unit gain on a steady tone below the cut-off; a wider transition leaves a steady tone below the cut-off at unit gain. (The 1.5 x f0 floor is the engine's choice, tested in item 7.)
7. **Rubber Band `below_mvf`:** a below-snap shift is bit-exact; for shifts of +-100, +-300 and +-1200 cents on a harmonic-plus-noise tone the band above `MVF + transition` is unchanged within a stated dB (set from the first measurement, then fixed), the shifted harmonics below the cut-off land at the right pitch (autocorrelation), no energy appears above the cut-off from shifted harmonics (upward case), and the downward gap is the documented size; `formant_offset` with the scope is `invalid_argument`; an empty field falls back to the full-band shift; stereo; the live-shifter path; a click check at a cut-off change.
8. **Neighbours:** two touching notes with different MVFs and offsets render without a level step larger than the full-band render's at the boundary.
9. **Harmonic-noise engine:** the noise above the MVF stays within 1 dB of the original in a breathy synthetic frame; the harmonic weights at `MVF +- f0/2` are as specified.
10. **Bench (outside the build):** A against D4C (pyworld) on `vocadito` and on the synthetic signals; the table goes to `docs/reviews/` and the owner decides whether A is good enough.

## Open questions
- MVF accuracy on real singing is unknown (A's thresholds are guesses); the bench answers it. If A is off by more than about 500 Hz, re-tune A or implement C before any consumer is switched on.
- A single cut-off is a step; real voices fade. The transition width (>= 1.5 f0) is the engineering answer; listening decides if it must be wider. A per-band aperiodicity matrix is the alternative, not needed by these consumers.
- Interaction with the withdrawn core window (ADR-0032): none; the MVF is a frequency split, the core window was a time split.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §3 (`NoiseTrack`) and ADR-0017's amendment (the per-frame MVF clause) are updated when this is implemented; ADR-0033 records the data-model decision and the choice of option A over ADR-0031's candidates.
