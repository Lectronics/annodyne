# Design note: level-aware voicing and non-vocal notes

> **Superseded 2026-10-20.** After this note was drafted the owner decided to switch the pitch tracker to
> the vendored c4dm/pyin (ADR-0014, `docs/design/c4dm-pyin-integration.md`). With it every probe row of
> `tests/unit/voicing_probes_test.cpp` passes, including the four this note planned to fix (tail below
> -52 dB, lone hum, lone -80 dBFS tone, voiced 'z'), through the plugin's low-amplitude rule and
> per-bin unvoiced states. The drop gate, level weight and hum classifier below were therefore **not
> built** (the work-in-progress code is parked in a local git stash). They remain the design to pick up
> if a take-relative level reference or hum louder than the plugin's rule is needed. The structure
> review of this note (CHANGES REQUIRED: ADR missing, ghost-note consumers, probe test contradiction,
> Hz/cents reference, high-pass bias at 50 Hz, false positives with an untrusted floor) is kept in the
> Phase 3 review addendum for that case.

- **Phase:** 3 (closing slice)
- **Status:** superseded (see below)
- **Spec sections:** implementation guide (pitch, onsets and segmentation), core architecture §3 (`Note`, `is_active`)
- **Folder / CMake target:** `src/core/analysis/activity` (extended), `src/core/analysis/non_vocal` (new, layer 1), `core/types/pitch_target.h` (one flag), `core/analysis/orchestrator` (wiring)
- **Evidence:** `docs/design/phase3-voicing-review.md` (outside review), probes in `docs/design/probes/`, the C++ ports in `tests/unit/voicing_probes_test.cpp`

## Purpose
The outside review says the voicing decision ignores level, so quiet periodic sounds become notes and breathy singing is lost. Porting its probes to C++ tests (2026-10-20) showed that half of that does not hold on the current code and half does:

| Review claim | Measured on the current code |
|---|---|
| breathy singing lost at HNR <= 15 dB (beta mean 0.10) | **not reproduced**: `beta_mean` is 0.3 (tuned on Vocadito, ADR-0012); voiced 100 % from 30 dB down to 3 dB HNR, 79 % at 0 dB |
| hum between notes becomes notes | **not reproduced**: the activity stage (ADR-0011) takes the hum as the noise floor and gates it; p2 passes, as do inhale, sibilant, bleed and the scoop |
| a lone 50 Hz hum becomes a note | **reproduced**: -55, -65, -75 dBFS give a 100 % voiced note; a file with no silence has no floor to gate against |
| a lone -80 dBFS tone is a confident note | **reproduced**: confidence 0.995 |
| a reverb tail below -52 dB stays voiced | **reproduced**: 52 % voiced and a note |

This note covers only what is reproduced. P2 (`yin_trust`, per-bin unvoiced states), P4 (soft voicing posterior), P5 (note HMM changes) and the tail-merging part of P6 are **not** adopted: the probes do not fail on them, and the rule is that constants and structure are judged on the golden vocals, not on synthetic probes alone. They stay in the review as candidates for a later phase (ROADMAP carried items).

## Changes

### 1. Drop gate in the activity stage (tails)
`ActivityParams::max_drop` (Decibels, default 40; review P1.2: "full weight within 40 dB of the reference"). After the Schmitt trigger, the take's loud reference is the `range_percentile_prob` (0.95) quantile of the levels of the *active* frames. Active frames more than `max_drop` below it become silent, then the existing short-run rules run. Only applied when the file has silence (`range_top - floor >= min_range`); a file without silence has no meaningful reference.

### 2. Level weight for note confidence (P1.4)
`ActivityTrack::floor_trusted` (bool): true when the file has silence, i.e. the floor is a floor and not just the quietest part of a steady signal. New free function

```cpp
/// 0..1 weight of a note's level. Absolute part: 0 at <= absolute_zero, 1 at >= absolute_full
/// (linear in dB). Relative part, only when the floor is trusted: 0 at <= floor + margin_zero,
/// 1 at >= floor + margin_full. The product.
[[nodiscard]] double level_weight(Decibels level, const ActivityTrack& activity, const LevelWeightParams& p);
/// Median level (dBFS) of the frames of `activity` inside [start, end); -120 if none.
[[nodiscard]] Decibels median_level(const ActivityTrack& activity, SourceTime start, SourceTime end);
```

`LevelWeightParams` (in `ActivityParams`, so one tuning struct): `absolute_zero` -80 dBFS, `absolute_full` -65 dBFS (a take whose quietest notes are quieter than -65 dBFS RMS is at the noise level of 12-bit audio; the review's lone -80 dBFS tone must not be confident), `margin_zero` 6 dB, `margin_full` 20 dB (review P1.1). The orchestrator multiplies `note.confidence_prob` by `level_weight(median_level(...))`. Unchanged: `PitchTarget::confidence`, which is about the pitch not the level.

### 3. Non-vocal notes: mains hum (P6, first rule)
New target `core/analysis/non_vocal` (layer 1; links `core_types`, `core_analysis_activity`).

```cpp
enum class NonVocalReason { none, mains_hum };
struct NonVocalParams {
    std::array<Hz, 2> mains{Hz{50.0}, Hz{60.0}};  // mains fundamentals
    int harmonics = 3;                              // fundamental and the next two
    Cents tolerance{30.0};                          // median pitch this close to a mains harmonic
    Seconds min_duration{0.5};                      // a hum is long ...
    Cents max_deviation{5.0};                       // ... and unmoving (std of the voiced f0)
    Decibels max_above_floor{25.0};                 // ... and not loud
};
[[nodiscard]] Result<void> validate(const NonVocalParams&);
/// One reason per note, in order. A note is `mains_hum` if its median voiced f0 is within
/// `tolerance` of a mains harmonic, it lasts at least `min_duration`, the std of its voiced f0 is
/// below `max_deviation` and its median level is below `floor + max_above_floor`.
[[nodiscard]] Result<std::vector<NonVocalReason>> classify_non_vocal(std::span<const Note> notes, const PitchTrack& pitch, const ActivityTrack& activity, const NonVocalParams& params);
```

The orchestrator marks such a note `is_active = false` (a ghost the user can switch on, as the architecture defines) and sets the new `PitchFlag::mains_hum` (value 128) on its target, so the reason travels with the note. The pitch track is **not** altered (review P6). Not rejected: hum louder than floor + 25 dB (a hum that loud is a recording problem the user must see), sung notes near 50-180 Hz at normal level (the level and std tests exclude them; negative test).

## Units, time axis, ownership
All times are `SourceTime`/`Seconds`, levels `Decibels` (dBFS), pitch `Cents` (A4 = 6900) or `Hz` for the mains table. Value types; no ownership beyond the caller's spans. Pure, thread-safe, not real-time safe (allocate). Compiled with `-ffp-contract=off`.

## Errors
`classify_non_vocal`: invalid_argument for invalid params (non-finite or non-positive values, harmonics < 1) or a pitch track / activity track whose grids differ. `detect_activity`: the new params are validated (`max_drop` > 0 and finite, the level weights finite with zero < full). `level_weight`: total; non-finite input gives 0.

## Effect on audio
None directly: analysis only. Downstream, a hum note or a tail that was a note is gone or inactive, and confidences change. No audio is rendered, so no audio-verifier run; the 5-plot human check covers the golden tracks.

## Split / join decisions
Hum classification is its own target, not part of segmentation or the pitch target: it needs level and the activity track (coding-standards §5, one reason to change; keeps `pitch_target` free of the activity dependency). The level weight lives with the activity stage because it needs the floor and its trust flag.

## Dependencies
`non_vocal`: `core_types`, `core_analysis_activity`. `activity`: unchanged. No new external library. `AnalysisResult` is unchanged; `PitchFlag` would gain one value (a new ADR; not built, see the banner).

## Tests
- Probes (`voicing_probes_test.cpp`): tail below -52 dB unvoiced; hum alone gives no active note (it is a flagged ghost) and an inactive note carries `mains_hum`; a lone -80 dBFS tone is not confident; audible lone tones stay confident; every row that passes today still passes.
- Unit: drop gate on a synthetic decay; floor_trusted both ways; level_weight ramps and boundaries; hum classification: positive for 50 and 60 Hz and harmonics, negative for a sung 98 Hz note with vibrato, a loud 100 Hz tone, a short hum, a drifting hum; parameter validation.
- Golden (Vocadito, 35 tuning / 5 held-out): voicing recall, false alarm and pitch accuracy unchanged within 0.3 points; no hum rejection on any track.

## Open questions
- A voiced fricative ('z', 150 ms, probe p3): with the owner's decision that consonants are grouped with the pitch, a standalone one is ambiguous. The row asserts nothing until the consonant phase decides.
- Hum louder than floor + 25 dB, reverb-tail merging (review P6) and singer-range re-weighting are not done here.
- GCC 13 `-Werror=free-nonheap-object` in `core/edits/remap.cpp`: not reproducible here (no GCC 13, CI builds Linux with clang); a version-gated suppression is added unverified.

## Review log
(pending structure review)
