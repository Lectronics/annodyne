# Design note: Phase 3 — note segmentation, pitch description, loudness

- **Phase:** 3
- **Status:** implemented (reviewed in two rounds; see Review log and Implementation notes)
- **Spec sections:** architecture §3 (`Note`), §4.2 (`segment_notes`, `perceived_pitch`, `decompose_pitch_curve`, `measure_loudness`); implementation guide B3, B4, B13; ROADMAP Phase 3
- **Folders / CMake targets:** `src/core/analysis/segmentation`, `perceived_pitch`, `loudness` (layer 1; link `core/types`, and `core/dsp` for loudness). `NoteSpan` and `note_grid` live in `core/types/note_span.h` (plain data, shared by all three), so `loudness` and `perceived_pitch` do not depend on `segmentation`. Internal headers are in each target's `detail/` folder. `is_voiced` / `k_voiced_probability` (0.5) are defined once next to `PitchTrack`.
- **Depends on:** `phase3-dsp.md`, `phase3-pitch-onsets.md`

## Purpose
Turn the pitch track and onsets into `Note`s (start, attack end, release start, end, boundary type, confidence), then describe each note: the single pitch a listener hears, the stable centre with slow drift and fast modulation (vibrato) separated, the amplitude curve and the K-weighted loudness. Everything is source-axis and read-only on the audio.

## Deviation from the spec signatures (ADR-0010)
The spec's `segment_notes` returns `Note`s "with pitch curve filled", but `Note` stores only centre + drift + mod, so the raw f0 has nowhere to live before decomposition, and a half-built `Note` (empty curves, loudness 0) would pass `validate`. Resolution (ADR-0010): `segment_notes` returns **`NoteSpan`s** (timing, rough pitch, boundary type, confidence), `decompose_pitch_curve`, `measure_loudness` and `perceived_pitch` take the pieces they need, and the **orchestrator alone builds a `Note`**. `Note` and `AnalysisResult` do not change; a contract test asserts every note in `analyse()` output has `grid.n_frames > 0` and full curves. `detect_onsets` gains an optional pitch input; `measure_loudness` takes the mono and K-weighted signals instead of the source.

## 1. Segmentation
```cpp
/// One segmented note before it is described: no curves, no loudness. NOT a Note.
struct NoteSpan {
    NoteId id;
    SourceTime source_start, attack_end, release_start, source_end;   // start <= attack_end <= release_start <= end
    Cents rough_pitch;               // median smoothed f0 over the note's stable part; refined by perceived_pitch
    SeparationIn separation_in = SeparationIn::hard;
    double confidence_prob = 1.0;    // 0..1
};

/// The frame grid of a note: the pitch-track frames in [source_start, source_end): start = frame_time of
/// the first pitch frame >= source_start, hop = pitch.grid.hop, n_frames = number of such frames (>= 1).
/// Curves (drift, mod, amp) are values AT those frames; nothing is interpolated off-grid.
[[nodiscard]] FrameGrid note_grid(const NoteSpan& span, const FrameGrid& pitch_grid);

struct SegmentParams {
    Seconds min_note{0.060};
    Seconds vibrato_smooth{0.150};        // moving-mean of f0 in cents before the HMM (about one vibrato cycle)
    double note_bin_cents = 100.0 / 3.0;  // 3 pitch states per semitone
    double pitch_sigma_cents = 45.0;      // observation spread around a bin
    double stay_stable = 0.995;           // transition probabilities (see method)
    double stable_to_silence = 0.005;
    double onset_new_note_prob = 0.98;    // P(new note) at the frames of an onset (±10 ms); new_note_prob is the base
    Seconds preroll_max{0.120};           // how far before the voiced region a consonant onset may pull the start
    Cents stable_band{40.0};              // attack/release: within this of the note pitch counts as stable
    Seconds stable_min{0.030};
};

/// Spans sorted by source_start, non-overlapping (monophonic). The id is "n" + start in milliseconds
/// (6 digits; a collision gets "_1", "_2"). separation_in is soft when the previous note ends within
/// 20 ms of this note's start, else hard. confidence_prob = mean voicing probability over the note
/// times the share of its voiced frames within 60 cents of its pitch (0..1). A run shorter than
/// min_note (or than 10 pitch frames) is absorbed by an adjacent longer run (a glide becomes the
/// attack of the note it reaches; a scoop belongs to its note) or dropped if it has none. Time clamps: every time lies in [0, last pitch frame time] (grid.start + (n_frames - 1) * hop;
/// the < 1 hop after the last frame is not analysed); a preroll never reaches into the previous note (start >= previous end);
/// an end never passes the next start; the four times are made ordered. The only inputs are the pitch
/// track (f0, voicing, confidence) and the onsets (the energy cue is carried by the onset strengths;
/// the audio itself is not read, so no source is needed). An empty or all-unvoiced track gives no
/// notes (not an error).
/// Errors: invalid_argument (bad params, invalid pitch track or onset list).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<NoteSpan>> segment_notes(const PitchTrack& pitch, const OnsetList& onsets, const SegmentParams& params);
```
Method (Mauch & Dixon 2014 note HMM, with onset fusion): the f0 curve is smoothed in cents over `vibrato_smooth` (a moving mean over the voiced frames of the window that lie within 120 cents of the frame's own value, so a vibrato is averaged but a note change is not blurred) so vibrato does not flip the pitch bin. States: for each pitch bin an **attack** and a **stable** state, plus one **silence** state. Observation: Gaussian of the smoothed f0 around the bin centre (`pitch_sigma_cents`) times the voicing probability (silence emits `1 - voicing`). Transitions: attack→stable, stable→stable (`stay_stable`), stable→attack of a neighbouring or equal bin (a new note, including the same pitch repeated), stable→silence, silence→attack. At the frames within ±10 ms of an onset from `OnsetList` the new-note probability is `onset_new_note_prob` (0.98) and staying drops to its floor, so a repeated same-pitch note needs an onset to split (the most common failure, a named test) and a vibrato cannot split a note without one. Viterbi → runs of attack+stable states of one bin = notes; runs shorter than `min_note` are absorbed into the following adjacent run or dropped (see Implementation notes); after time snapping, notes with fewer than 10 grid frames or fewer than 3 voiced frames are dropped, so every returned note can be described.
Times: `source_start` = the earlier of the HMM start and a consonant/attack onset within `preroll_max` before it (the consonant belongs to the note); `source_end` = last voiced frame + one hop (or the next note's start when abutting). `attack_end` = first time the f0 stays within `stable_band` of the note pitch for `stable_min` (never before the voiced start), `release_start` = the symmetric last stable time; the four times satisfy start ≤ attack_end ≤ release_start ≤ end (clamped).

## 2. Pitch curve description
Both functions use one weight definition, in the `perceived_pitch` target (no duplicated constants):
```cpp
struct StabilityWeightParams {
    double rate_scale_cents_per_s = 600.0;  // |d f0/dt| at which the stability weight is e^-1
    double gamma = 1.0;                      // exponent on the stability weight
    Decibels amp_range{30.0};                // frames more than this below the note's loudest get weight 0
};

/// w_i = exp(-(|d f0_i/dt| / rate_scale)^2)^gamma * a_i, with the derivative a central difference of the
/// (gap-filled) f0 in cents per second and a_i = clamp(1 - (amp_max - amp_i) / amp_range, 0, 1) from the
/// note's amp curve (a_i = 1 when `amp` is empty). Pure.
[[nodiscard]] std::vector<double> stability_weights(std::span<const Cents> f0, Seconds hop, std::span<const Decibels> amp, const StabilityWeightParams& params);

enum class CyclePolicy { whole_cycles, all_frames };

/// Attack/release trims shared by decompose_pitch_curve and perceived_pitch: per side
/// min(fixed trim, max_fraction x note length); if the trimmed region has < 3 frames the whole note is used.
struct TrimParams {
    Seconds start{0.040};
    Seconds end{0.060};
    double max_fraction = 0.25;
};

struct PitchDecompositionParams {
    Hz drift_cutoff{2.5};          // below: drift, above: modulation (vibrato)
    TrimParams trim;
    StabilityWeightParams weights;
};

struct PitchDecomposition {
    Cents center;               // absolute, constant over the note
    std::vector<Cents> drift;   // slow component relative to center, grid.n_frames long (the note_grid)
    std::vector<Cents> mod;     // fast component relative to center
    double voiced_fraction = 0; // share of frames that were voiced before filling
};

/// f0 at the frames of `note_grid(span, pitch.grid)` (unvoiced gaps filled by linear interpolation in
/// cents, hold at the ends; this fill is internal, `interp_curve` is not given NaN), low-passed at
/// drift_cutoff (zero-phase, two-pass one-pole cascade) into slow = center + drift; mod = f0 − slow.
/// center = stability-weighted mean of `slow` over the trimmed region, with the weights of
/// `stability_weights` using `amp` (the note's amp curve on the same grid; empty = amp term 1).
/// drift = slow − center. Exact reconstruction: center + drift[i] + mod[i] == filled f0[i] to float
/// precision. Trims are min(fixed trim, trim fraction × note length) per side; if the trimmed region
/// has < 3 frames the whole note is used. Errors: invalid_argument (bad params, amp size != frame
/// count) or fewer than 3 voiced frames in the note (degenerate; `segment_notes` guarantees at least
/// 10 pitch frames (the longer of min_note and 10 hops) and voiced notes, so the orchestrator never hits this: a contract test checks it).
/// Pure; thread-safe.
[[nodiscard]] Result<PitchDecomposition> decompose_pitch_curve(const NoteSpan& span, const PitchTrack& pitch, std::span<const Decibels> amp, const PitchDecompositionParams& params);

struct PerceivedPitchParams {
    StabilityWeightParams weights;       // the same definition as decompose_pitch_curve
    TrimParams trim;                     // the same trims as decompose_pitch_curve
    CyclePolicy cycles = CyclePolicy::whole_cycles;
};

/// The pitch a listener hears: stability-weighted mean (log-frequency = cents) of center + drift +
/// mod over the trimmed note, weights from `stability_weights` (slow-changing frames count more:
/// d'Alessandro & Castellengo 1994; Mesz & Eguia; pyAMPACT). For a vibrato note (FFT peak of `mod` in
/// 3-9 Hz) and `CyclePolicy::whole_cycles` the average runs over whole cycles only, to avoid a
/// partial-cycle bias. A scoop in the first `trim_start` is excluded by the trim and the weights.
/// Takes the finished curves from a Note (grid, pitch_drift, pitch_mod, amp, pitch_center), so the
/// orchestrator builds those first. Error: invalid_argument if the note has < 3 frames or curves of
/// inconsistent length. Pure; thread-safe.
[[nodiscard]] Result<Cents> perceived_pitch(const Note& note, const PerceivedPitchParams& params);
```
Orchestrator order per note: `note_grid` → `measure_loudness` (amp on the grid) → `decompose_pitch_curve` (uses amp) → `perceived_pitch`.

## 3. Loudness
```cpp
struct LoudnessParams {
    Seconds amp_window{0.020};        // short-term RMS window for the amp curve, centred on each grid frame
    Decibels absolute_gate{-70.0};    // BS.1770 absolute gate (LUFS)
    Decibels amp_floor{-120.0};       // amp curve floor (dBFS)
};

struct NoteLoudness {
    LoudnessUnits loudness;           // K-weighted LUFS over [attack_end, source_end]
    std::vector<Decibels> amp;        // dBFS RMS of the UNWEIGHTED signal per grid frame (what the Amplitude tool changes)
};

/// `plain` is the unweighted mix (`read_mono`), `k_weighted` its `k_weight()` output (computed once per
/// source), same rate and length. loudness: **integrated over the note's own span [attack_end,
/// source_end]** = −0.691 + 10 log10(mean square of k_weighted) with the BS.1770 absolute gate applied
/// to 400 ms-or-shorter blocks of the span (one block if the span is shorter): blocks below the gate are
/// ignored; if every block is gated out the value is the gate itself (−70 LUFS: "silent"). There is no
/// relative gate and no step at 400 ms: the note is the programme. Per implementation guide B13,
/// sibilant/breath exclusion needs the noise track (Phase 4); until then the span is used whole.
/// amp[i] = 10 log10(mean square of `plain` over amp_window centred on frame i) in dBFS, floored at
/// amp_floor, on `grid` (the note_grid). Errors: invalid_argument (signals differ in rate/length, grid
/// or note times outside the signal). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<NoteLoudness> measure_loudness(const MonoAudio& plain, const MonoAudio& k_weighted, const NoteSpan& span, const FrameGrid& grid, const LoudnessParams& params);
```

## Effect on audio
None.

## Split / join decisions
- `decompose_pitch_curve` and `perceived_pitch` are separate: the former builds curves from the track, the latter reads the note's curves (the spec's signature) — split function rule 4; both are tested on synthetic curves alone.
- Segmentation, pitch description and loudness are separate targets (different reasons to change: HMM model vs perceptual weighting vs standards) but share no state, so no classes.
- One `segment_notes` (HMM, boundaries, times) rather than three: the boundary refinement uses the same smoothed curve and posterior as the HMM — keep-together rule 1.

## Dependencies
`core/types`; `core/dsp` for loudness (`MonoAudio`). No external libraries. (`decompose_pitch_curve` has its own gap fill; `interp_curve` has no production caller yet and is kept for Phase 4.)

## Tests
- Segmentation (synthetic melodies from `tests/support/synth_signals.h`): onset ±20 ms, offset ±30 ms, pitch ±10 cents; **repeated same-pitch notes** with a short dip → N notes; one long vibrato note (±60 cents) → one note; legato A4→B4 → two notes with `soft` separation; glide handled as one transition (policy recorded); silence gaps; notes shorter than `min_note` dropped; consonant preroll pulled into the start and clamped to the previous note's end; ends clamped to the file; empty and all-unvoiced tracks give no notes; output sorted, non-overlapping, times ordered; a 60-100 ms note decomposes and gets a perceived pitch (trim fraction); `note_grid` frame count equals the frames in [start, end).
- `decompose_pitch_curve`: reconstruction exact (1e-4 cents); a 5.5 Hz ±50 cent vibrato lands in `mod` (amplitude ±5 cents) and a slow 0.5 Hz drift in `drift`; centre equals the generating centre ±1 cent.
- `perceived_pitch`: vibrato tone = geometric mean (cents mean) ±2 cents; a scoop up into the pitch does not move it by more than 5 cents; stable note equals its centre ±0.5 cents; < 3 frames is an error.
- Loudness: a −23 dBFS 1 kHz sine measures −23.0 ± 0.1 LUFS; amp curve of a tone equals its RMS ± 0.1 dB; a note shorter than 400 ms works; gate drops a silent note to the floor.

## Open questions
1. 3 pitch states per semitone and σ = 45 cents are initial values to tune on the synthetic set and then the golden vocals (recorded in the Phase 3 review).
2. Glides (portamento): treated as one transition inside the next note, not a separate note; confirmed or changed when you review the plots.

## Review log
Round 1 (design): CHANGES REQUIRED (7 blocking: ADR for the signature changes, half-built `Note`, short-note trims, note grid, shared weights, gate coverage, `segment_notes` inputs); round 2: APPROVED WITH FIXES (shared `TrimParams`, loudness wording, clamps, orchestrator order). All fixed before coding; ADR-0010 added. Implementation review: APPROVED WITH FIXES, fixed: short-note guarantee enforced (`drop_unusable`) and property-tested on 150 random tracks, `NoteSpan` moved to `core/types`, `is_voiced` defined once, internal headers moved to `detail/`, tolerances moved to `tests/tolerances.h`, `k_weight`/`analyse` rate limit (4 kHz) documented and tested, long functions split, `-ffp-contract=off` on `core/types`.

## Implementation notes
- `SegmentParams::onset_boost` was replaced by `onset_new_note_prob` (0.98): an onset is modelled as strong evidence, so at the frames within +-10 ms of an onset the new-note probability is 0.98 and the stay probability drops to its floor. A multiplier on a 1e-3 prior could not split a repeated same-pitch note (the HMM has no emission difference between "same note" and "new note of the same pitch"); the likelihood-ratio view needs ~1e4.
- A new note from a stable state goes 80 % to pitches within +-1 semitone and 20 % to any other (`note_hmm.cpp`).
- Short runs are absorbed **right to left** into the following adjacent run (a glide chain collects onto the note it reaches), else into a preceding long run, else dropped; the minimum is the longer of `min_note` and 10 pitch frames.
- `confidence_prob` = mean voicing probability over the note x the share of its voiced frames within 60 cents of its pitch (the "Viterbi margin" of the draft was not implemented).
- `note_grid` uses an epsilon of 1e-9 frames when rounding, so a time that is an exact multiple of the hop is not pushed to the next frame.
- `decompose_pitch_curve` and `perceived_pitch` share `TrimParams` and `StabilityWeightParams` (one definition). Loudness: per-block absolute gate over [attack_end, source_end] with no relative gate (ADR-0010 item 3).
- Test fixtures: `tests/support/synth_melody.h` builds exact f0 tracks (no estimator in the loop) for the segmentation tests; `synth_signals.h` has the audio generators.