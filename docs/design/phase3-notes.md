# Design note: Phase 3 — note segmentation, pitch description, loudness

- **Phase:** 3
- **Status:** draft
- **Spec sections:** architecture §3 (`Note`), §4.2 (`segment_notes`, `perceived_pitch`, `decompose_pitch_curve`, `measure_loudness`); implementation guide B3, B4, B13; ROADMAP Phase 3
- **Folders / CMake targets:** `src/core/analysis/segmentation`, `perceived_pitch`, `loudness` (layer 1; link `core/types`, `core/dsp`)
- **Depends on:** `phase3-dsp.md`, `phase3-pitch-onsets.md`

## Purpose
Turn the pitch track and onsets into `Note`s (start, attack end, release start, end, boundary type, confidence), then describe each note: the single pitch a listener hears, the stable centre with slow drift and fast modulation (vibrato) separated, the amplitude curve and the K-weighted loudness. Everything is source-axis and read-only on the audio.

## Deviation from the spec signatures (recorded here, no ADR: the spec is prose)
The spec's `perceived_pitch(note, …)` and `decompose_pitch_curve(note, …)` take a note that already carries its curves, while `segment_notes` returns notes "with pitch curve filled". `Note` stores only centre + drift + mod, so the raw f0 has nowhere to live before decomposition. Resolution: `segment_notes` returns **valid** `Note`s with timing and a rough pitch only (`grid.n_frames == 0`, empty curves: valid per `validate(Note)`); `decompose_pitch_curve` takes the `PitchTrack` and returns the curves; the orchestrator assembles the finished `Note`. `Note` itself does not change.

## 1. Segmentation
```cpp
struct SegmentParams {
    TrackId track{"t1"};
    Seconds min_note{0.060};
    Seconds vibrato_smooth{0.150};        // moving-mean of f0 in cents before the HMM (about one vibrato cycle)
    double note_bin_cents = 100.0 / 3.0;  // 3 pitch states per semitone
    double pitch_sigma_cents = 45.0;      // observation spread around a bin
    double stay_stable = 0.995;           // transition probabilities (see method)
    double stable_to_silence = 0.005;
    double onset_boost = 25.0;            // multiplier on attack transitions near an onset (±30 ms)
    Seconds preroll_max{0.120};           // how far before the voiced region a consonant onset may pull the start
    Cents stable_band{40.0};              // attack/release: within this of the note pitch counts as stable
    Seconds stable_min{0.030};
};

/// Notes sorted by source_start, non-overlapping (monophonic), is_active = true, voice 0.
/// `perceived_pitch` and `pitch_center` hold the HMM's note pitch (refined later); the id is
/// "n" + start in milliseconds (6 digits; a collision gets "_1", "_2"). separation_in is soft when the
/// previous note ends within 20 ms of this note's start, else hard. confidence_prob = mean voiced
/// posterior over the note times the Viterbi margin (0..1).
/// Errors: invalid_argument (bad params, pitch/onsets invalid, grids mismatch), out_of_range from src.
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<Note>> segment_notes(const PitchTrack& pitch, const OnsetList& onsets,
                                                      const AudioSource& src, const SegmentParams& params);
```
Method (Mauch & Dixon 2014 note HMM, with onset fusion): the f0 curve is smoothed in cents over `vibrato_smooth` so vibrato does not flip the pitch bin. States: for each pitch bin an **attack** and a **stable** state, plus one **silence** state. Observation: Gaussian of the smoothed f0 around the bin centre (`pitch_sigma_cents`) times the voicing probability (silence emits `1 - voicing`). Transitions: attack→stable, stable→stable (`stay_stable`), stable→attack of a neighbouring or equal bin (a new note, including the same pitch repeated), stable→silence, silence→attack. Attack transitions are multiplied by `onset_boost` within ±30 ms of an onset from `OnsetList`, so a repeated same-pitch note needs an onset to split (the most common failure, a named test) and a vibrato cannot split a note without one. Viterbi → runs of attack+stable states of one bin = notes; notes shorter than `min_note` are dropped or merged into the neighbour with the nearest pitch.
Times: `source_start` = the earlier of the HMM start and a consonant/attack onset within `preroll_max` before it (the consonant belongs to the note); `source_end` = last voiced frame + one hop (or the next note's start when abutting). `attack_end` = first time the f0 stays within `stable_band` of the note pitch for `stable_min` (never before the voiced start), `release_start` = the symmetric last stable time; the four times satisfy start ≤ attack_end ≤ release_start ≤ end (clamped).

## 2. Pitch curve description
```cpp
struct PitchDecompositionParams {
    Hz drift_cutoff{2.5};       // below: drift, above: modulation (vibrato)
    Seconds trim_start{0.040};  // attack excluded when estimating the centre
    Seconds trim_end{0.060};    // release excluded
};

struct PitchDecomposition {
    Cents center;               // absolute, constant over the note
    FrameGrid grid;             // note grid: start = note.source_start, hop = pitch hop
    std::vector<Cents> drift;   // slow component relative to center, grid.n_frames long
    std::vector<Cents> mod;     // fast component relative to center
    double voiced_fraction = 0; // share of frames that were voiced before filling
};

/// f0 over the note's frames (unvoiced gaps filled by linear interpolation in cents, hold at the
/// ends), low-passed at drift_cutoff (zero-phase, two-pass one-pole cascade) into slow = center +
/// drift; mod = f0 − slow. center = stability-weighted mean of `slow` over the trimmed region
/// (weights as in perceived_pitch). drift = slow − center. Exact reconstruction:
/// center + drift[i] + mod[i] == filled f0[i] to float precision. Errors: invalid_argument (note
/// has no frames in the track, or bad params). Pure; thread-safe.
[[nodiscard]] Result<PitchDecomposition> decompose_pitch_curve(const Note& note, const PitchTrack& pitch, const PitchDecompositionParams& params);

struct PerceivedPitchParams {
    double gamma = 1.0;          // weight = exp(−(|df/dt| / scale)^2)^gamma
    double rate_scale_cents_per_s = 600.0;
    Seconds trim_start{0.040};
    Seconds trim_end{0.060};
    bool whole_vibrato_cycles = true;
};

/// The pitch a listener hears: stability-weighted mean (log-frequency = cents) of center + drift +
/// mod over the trimmed note, weights falling with |d f0/dt| (slow-changing frames count more:
/// d'Alessandro & Castellengo 1994; Mesz & Eguia; pyAMPACT) and with low amp. For a vibrato note
/// (modulation periodicity found by an FFT peak of `mod` in 3-9 Hz) the average runs over whole
/// cycles only, to avoid a partial-cycle bias. A scoop in the first `trim_start` is excluded by the
/// trim and by the weights. Error: invalid_argument if the note has < 3 usable frames.
/// Pure; thread-safe.
[[nodiscard]] Result<Cents> perceived_pitch(const Note& note, const PerceivedPitchParams& params);
```
`perceived_pitch` reads `center`, `drift`, `mod` and `amp` from the note, so the orchestrator calls loudness first.

## 3. Loudness
```cpp
struct LoudnessParams {
    Seconds amp_window{0.020};        // short-term RMS window for the amp curve (on the note grid)
    Decibels absolute_gate{-70.0};    // BS.1770 absolute gate for the single loudness value
    Seconds exclude_attack{0.0};      // part of the attack to exclude (default: from attack_end)
};

struct NoteLoudness {
    LoudnessUnits loudness;           // K-weighted LUFS over [attack_end, source_end]
    std::vector<Decibels> amp;        // dBFS RMS of the UNWEIGHTED signal per grid frame (what the Amplitude tool changes)
};

/// `mono` is the unweighted mix, `k_weighted` its `k_weight()` output (computed once per source),
/// both at `rate`. loudness = −0.691 + 10 log10(mean square of k_weighted over [attack_end,
/// source_end] with the absolute gate); notes shorter than 400 ms use their own span (no 400 ms
/// blocks). Quiet frames below −120 dBFS are floored at −120. Errors: invalid_argument (spans
/// differ in length, note times outside the signal). Pure; thread-safe.
[[nodiscard]] Result<NoteLoudness> measure_loudness(std::span<const float> mono, std::span<const float> k_weighted, Hz rate, const Note& note, const FrameGrid& grid, const LoudnessParams& params);
```
Per implementation guide B13, sibilant/breath exclusion needs the noise track (Phase 4); until then `[attack_end, source_end]` is used.

## Effect on audio
None.

## Split / join decisions
- `decompose_pitch_curve` and `perceived_pitch` are separate: the former builds curves from the track, the latter reads the note's curves (the spec's signature) — split function rule 4; both are tested on synthetic curves alone.
- Segmentation, pitch description and loudness are separate targets (different reasons to change: HMM model vs perceptual weighting vs standards) but share no state, so no classes.
- One `segment_notes` (HMM, boundaries, times) rather than three: the boundary refinement uses the same smoothed curve and posterior as the HMM — keep-together rule 1.

## Dependencies
`core/types`, `core/dsp` (`interp_curve`, `k_weight`, `RealFft`). No external libraries.

## Tests
- Segmentation (synthetic melodies built from tones with known note lists): onset ±20 ms, offset ±30 ms, pitch ±10 cents; **repeated same-pitch notes** with a short dip → N notes; one long vibrato note (±60 cents) → one note; legato A4→B4 → two notes with `soft` separation; glide handled as one transition (policy recorded); silence gaps; notes shorter than `min_note` dropped; consonant preroll pulled into the start; output sorted, non-overlapping, `validate(Note)` passes.
- `decompose_pitch_curve`: reconstruction exact (1e-4 cents); a 5.5 Hz ±50 cent vibrato lands in `mod` (amplitude ±5 cents) and a slow 0.5 Hz drift in `drift`; centre equals the generating centre ±1 cent.
- `perceived_pitch`: vibrato tone = geometric mean (cents mean) ±2 cents; a scoop up into the pitch does not move it by more than 5 cents; stable note equals its centre ±0.5 cents; < 3 frames is an error.
- Loudness: a −23 dBFS 1 kHz sine measures −23.0 ± 0.1 LUFS; amp curve of a tone equals its RMS ± 0.1 dB; a note shorter than 400 ms works; gate drops a silent note to the floor.

## Open questions
1. 3 pitch states per semitone and σ = 45 cents are initial values to tune on the synthetic set and then the golden vocals (recorded in the Phase 3 review).
2. Glides (portamento): treated as one transition inside the next note, not a separate note; confirmed or changed when you review the plots.

## Review log
*(structure-reviewer findings go here)*
