# Design note: the maximum voiced frequency per frame (MVF)

- **Phase:** 6
- **Status:** draft (for structure review; nothing implemented). Origin: ADR-0031 decision item 4 ("store a per-frame MVF in the analysis; a design note chooses how to derive it"), owner request 2026-10-23.
- **Spec sections:** `docs/specs/annodyne-implementation-guide.md` (voicing and the harmonic + noise model, B-sections on aperiodicity); ADR-0017 (frame classes; item 4 "harmonics below the maximum voiced frequency", amendment 2026-10-21 deferring the per-frame MVF), ADR-0018 (vocal resynthesis research adoption), ADR-0031 (research outcome), `docs/design/vocal-resynthesis-research.md`
- **Folder / CMake target:** `src/core/types` (`NoiseTrack`, layer 0), `src/core/analysis/harmonic_noise` (layer 1, produces it), consumers `src/core/synthesis/harmonic_noise` and `src/core/synthesis/rubberband_pitch` (layer 2)

## Purpose
In a breathy or mixed-voice frame only the low part of the spectrum is periodic; above some frequency the sound is noise. Pitch shifting must move the periodic part and leave the noise where and as it was (ADR-0017 item 4, ADR-0031 item 4; Melodyne, Cubase and the Auto-Tune patent likewise leave unpitched material alone). The frequency that separates the two is the **maximum voiced frequency (MVF)**. The analysis already measures it per frame to decide the `breathy` class (`measure_frames`, `FrameMetrics::mvf_hz`: the lower edge of the first aperiodic band), but it throws it away: `Note` stores only the class, so no engine can shift "below the MVF only". This note stores it and says how the engines use it.

## Public types
Only one field is added.

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `NoiseTrack` (existing) | struct | `max_voiced_hz` is empty or has `grid.n_frames` entries | the `Note` | value, inside a shared immutable analysis snapshot |

```cpp
struct NoiseTrack {
    FrameGrid grid;
    FrameMatrix band_energy_linear;  // frames x bands
    std::vector<FrameClass> frame_class;
    std::vector<Hz> max_voiced_hz;   // per frame: the highest frequency still periodic;
                                     // 0 Hz = no periodic part (unvoiced); empty = not measured
};
```
Units: Hz. Time axis: the grid's (source time, frame i at `frame_time(grid, i)`). Values are finite and in `[0, 0.5 x sample rate]`; a fully voiced frame carries the analysis ceiling (`HarmonicNoiseParams::mvf_ceiling`, see below), not infinity.

## Public functions
No new public function. `split_harmonic_noise` fills the field; `validate(const NoiseTrack&)` checks it. Everything below is a change of contract of existing functions, so the headers' doc comments change with the code.

```cpp
/// ... also fills NoiseTrack::max_voiced_hz from FrameMetrics::mvf_hz after the class routing:
/// 5-frame median, clamped to [mvf_floor, mvf_ceiling] for frames that have a periodic part, 0 for
/// frames of class silence, plosive, sibilant, breath.
[[nodiscard]] Result<NoiseTrack> split_harmonic_noise(...);

/// ... Errors: invalid_argument if max_voiced_hz is neither empty nor n_frames long, or holds a
/// value that is not finite, is negative, or exceeds the Nyquist frequency of the analysis.
[[nodiscard]] Result<void> validate(const NoiseTrack& track);
```

| | |
|---|---|
| Inputs | the mono signal, its rate, the pitch track (unchanged) |
| Parameters (new, in `HarmonicNoiseParams`) | `mvf_floor` 500 Hz, `mvf_ceiling` 0.45 x rate capped at 12 kHz, `mvf_median_frames` 5 |
| Returns | the same `NoiseTrack` with one more vector |
| Errors | none new for `split_harmonic_noise`; `validate` as above |
| Pure? | yes |
| Thread safety | thread-safe (no shared state) |
| Real-time safe | no (analysis) |
| Cost | no extra pass: the value is already computed per frame |

## How the MVF is derived (the choice ADR-0031 asks for)
| Option | What | Evidence | Cost and risk |
|---|---|---|---|
| **A. The in-repo estimate** (`measure_frames`) | harmonics are masked, the rest of each mel band is the noise density, a band is aperiodic when noise exceeds `aperiodicity_threshold` of the band's power; MVF = the lower edge of the first aperiodic band | unit-tested on synthetic scenes; already decides `breathy`; on `vocadito` 18.7 % of frames are `breathy` by the MVF/HNR rule, which the README calls a calibration item | no new code to derive it; thresholds are provisional guesses; the resolution is a mel band (about 100-200 Hz at low frequency, wider above) |
| B. WORLD D4C band aperiodicity | coarse aperiodicity in 3 kHz-spaced bands, interpolated; MVF where it crosses a threshold | D4C is tested code (modified BSD; also in SPTK, ADR-0019); designed for speech | a vendored dependency for a 3 kHz grid that is coarse for an MVF around 1-3 kHz; "MVF from D4C" is not defined in any source read (ADR-0031 marks it "proposed here") |
| C. Drugman and Stylianou (arXiv:2006.00521) | a dedicated MVF estimator; the abstract says earlier methods hurt singing synthesis and theirs helps high-pitched voices | paper only; no code found (ADR-0031 F4) | would be our own implementation, against CLAUDE.md rule 9 unless no code appears |

**Choice: A, stored now; B as an independent cross-check, C only if A fails.** Reasoning: the value exists and is tested, so storing it costs nothing and unblocks the engines; the only open question is whether it is accurate on real singing, which is an measurement, not a design. Rule 9 is met by measuring against D4C (tested code) before trusting A on real voices; D4C stays out of the product unless A fails the checks below, because it is coarser than what is needed. A bench (not part of the render path) compares A with D4C on the committed `vocadito` tracks and on synthetic harmonic-plus-noise signals with a known cut-off.

## Smoothing and limits
- 5-frame median per note (the frame class is already median-filtered over 5 frames, so the two agree on short dropouts); never across a class change from voiced to unvoiced (that edge stays sharp: the unvoiced side is 0).
- Floor 500 Hz: below it a "periodic part" is only the fundamental and its first harmonics, and a band split there would remove the voice. Ceiling: 0.45 of the rate, capped at 12 kHz.
- The first and last frame of a note are held beyond the note's edges when an engine reads past them (same convention as the pitch curves).

## Consumers (each behind an option that is off until the owner has listened)
1. **`HarmonicNoiseEngine`** (ADR-0017 amendment 2026-10-21): in a `breathy` frame shift only the harmonics `k f0 < MVF` (soft edge of one harmonic); the residual above stays pinned, as it already is.
2. **`RubberBandPitchEngine`**: `ShiftScope::below_mvf`. The zone is split by a linear-phase crossover at `MVF(t)` (an STFT mask, 2048-point Hann, hop 512, a transition width of at least 1.5 x f0 so no harmonic is cut in half); the low part goes through Rubber Band, the high part is delay-matched and added back. With no shift the sum equals the input (the mask pair is power complementary and the delay is compensated), and frames with MVF 0 are returned untouched. This replaces "shift voiced frames only" (ADR-0031 item 4) for the voiced-breathy case where no commercial precedent exists.
3. Not a consumer: the frame class itself still routes sibilants, breaths and plosives (unchanged).

## Effect on audio
None until a consumer's option is switched on: the field is analysis data and no default render reads it. When an option is on, only frames with an MVF below the ceiling change (the part above the MVF stays as it was; the part below is shifted as before). "No edits, same audio" is unaffected (rule 5): an unedited note never reaches an engine.

## Data model and cache effects
- `NoiseTrack` gains the vector, so the note fingerprint (`render_key.cpp`, `put(w, n.noise...)`) changes; every cache key and every golden analysis fingerprint changes once; `AnalyseParams::version` is bumped by the caller (ADR-0004 convention: the caller bumps on re-analysis).
- JSON (`cli/analysis_json`): key `max_voiced_hz` (array of numbers, absent when empty); the reader accepts its absence (older files).
- `validate(const NoiseTrack&)` and the fixtures that build a `NoiseTrack` by hand (tests) keep working with the field empty ("not measured").

## Tests (first)
1. Synthetic: a harmonic tone (f0 200 Hz, 40 harmonics) low-passed at a known cut-off with white noise above, cut-offs 1.5, 3 and 5 kHz: the stored MVF is within one harmonic (or 300 Hz) of the cut-off in the steady frames; a fully harmonic tone gives the ceiling; white noise and silence give 0.
2. The vector has `n_frames` entries, is finite, clamped, and 0 exactly on the frames of class silence, plosive, sibilant, breath.
3. `validate` rejects a wrong length, NaN, a negative value and a value above Nyquist; accepts empty.
4. JSON round trip, and an older JSON without the key loads.
5. Fingerprint: two notes that differ only in `max_voiced_hz` have different fingerprints.
6. Engines (when their options are written): the harmonic-noise engine on a breathy synthetic frame leaves the level above the MVF within 1 dB of the original; the Rubber Band split with a zero shift reproduces the input to within -60 dB, and the content above the MVF is unchanged after a shift (the band above the cross-over, aligned).
7. Bench (not a unit test): A against D4C on `vocadito` and on synthetic signals; the table goes into the design note's review log and the owner decides whether A is good enough.

## Open questions
- MVF accuracy on real singing is unknown; A's thresholds are guesses (README of `harmonic_noise`). The bench answers it; if A is off by more than about 500 Hz on the committed tracks, option C or a re-tuned A is the next step, not B.
- Whether a per-band aperiodicity (a matrix) is a better thing to store than one number: it would allow a smooth cross-over instead of a cut-off, at the cost of a bigger track. Not needed by the two consumers above; revisit if listening finds the single cut-off audible.
- The cut-off is a step in the spectrum; real voices have a gradual transition. The cross-over width (1.5 x f0) is the engineering answer; a listening round decides if it must be wider.
- Interaction with the withdrawn core-window experiment (ADR-0032): none; the MVF is a frequency split, the core window was a time split.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §3 (`NoiseTrack`) is overridden by this note and ADR-0017 amendment 2 (one more field) once it is implemented; an ADR (next number) records the data-model change when the implementation starts.
