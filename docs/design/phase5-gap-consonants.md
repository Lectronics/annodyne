# Design note: Consonant and breath evidence in gaps (Phase 5, slice 6b)

- **Phase:** 5 (Timing), before the gate (owner, 2026-10-22: sibilants, fricatives and other unvoiced consonants are fundamental to the sound and must be detected before anything can mute a gap)
- **Status:** draft, revision 2 (answers structure review round 1)
- **Spec sections:** implementation guide B6 (sibilant/breath classification, level-independent features), ADR-0017 (frame classes, classifier), ADR-0018 (research adoption), ADR-0024 (segment timeline), `phase5-gaps-core.md` (regions, slice 6)
- **Folder / CMake target:** `src/core/analysis/gaps` (layer 1; links `core/types`, `core/dsp`, `analysis/activity`, and now `analysis/harmonic_noise` for the feature extractor: same layer, as the orchestrator does)

## Purpose

Slice 6 protects any stretch of a gap that is more than `tail_margin` (6 dB) above the floor. That misses quiet unvoiced consonants (a soft "f" or "th", a light breath) and tells the gate nothing about *what* a protected stretch is. This slice adds evidence from the ADR-0017 features, which are level-independent by design (centroid, zero-crossing rate, high-band share, spectral flux, burst detection), so a gap's regions carry the kind of sound they hold: `sibilant`, `breath`, `plosive`. The gate leaves every one of them alone; the later tools (Sibilant tool, breath macros, stretching rules of ADR-0018) read the kind.

## Public types

```cpp
// core/types/segments.h: GapRegionKind gains (appended; nothing serialises the integer)
enum class GapRegionKind { tail, protected_preroll, silence, audible, sibilant, breath, plosive };
//   audible = above the floor by level only (kind unknown); sibilant/breath/plosive = classified by features.
//   The gate mutes `silence` regions ONLY (ADR-0024 amendment 1), so a new kind can never be missed.
```

```cpp
// core/analysis/gaps/classify_gap_frames.h
struct GapEvidenceParams {
    HarmonicNoiseParams features;   // ADR-0017 thresholds (provisional). `silence_level` is IGNORED:
                                    // it is overridden to k_level_floor_db (documented, tested), so
                                    // the features and the floor, not an absolute level, decide.
};

/// Per grid frame of the activity track (source axis). Entries of note frames are unspecified.
struct GapEvidence {
    FrameGrid grid;
    std::vector<FrameClass> frame_class;  // only silence, plosive, sibilant, breath can occur (voicing 0)
    std::vector<Decibels> excess_db;      // activity.level - floor.curve, per frame (the threshold-free quantity)
};
[[nodiscard]] GapEvidence neutral_gap_evidence(const FrameGrid& grid);  // all silence, excess -inf-ish
```

`GapParams` gains `Decibels event_margin{3.0}` (a frame is evidence only if `excess_db > event_margin`, strict, as the level rule uses `> tail_margin`) and `Seconds evidence_guard{0.015}`. `min_audible` also applies to classified runs (no separate `min_event`).

## Public functions

```cpp
/// Runs measure_frames and classify_frames of analysis/harmonic_noise over the whole recording with
/// voicing 0 on every frame, zero pitch_drift / pitch_mod / pitch_center (measure_frames reads them on every
/// frame), an empty-column HarmonicTrack and no alternates, so every frame is judged by its noise
/// features. Keeps only the class and `activity.level - floor.curve` per frame.
/// Errors: invalid_argument (empty signal; sample rate below 8 kHz, which measure_frames rejects: the
/// caller passes neutral evidence instead; the activity grid and the floor on different grids; arrays
/// not matching the grid; bad params). Pure; thread-safe; not real-time safe. Cost: one STFT pass (a
/// 4096 and a 1024 FFT per 5 ms frame, about 25 MB transient for 10 minutes), roughly doubling the
/// per-note harmonic/noise cost; accepted, with a timing test.
[[nodiscard]] Result<GapEvidence> classify_gap_frames(const MonoAudio& mono, const ActivityTrack& activity, const NoiseFloor& floor, const GapEvidenceParams& params);
```

`build_segment_timeline(notes, source_end, activity, floor, evidence, params)`: the evidence is a required `const GapEvidence&`; `neutral_gap_evidence` reproduces slice 6 exactly (tested against every slice 6 expectation). Region rules, per gap, after the tail and before the preroll:

1. **Classified runs, per class.** A run is a maximal stretch of frames with the same class in {sibilant, breath, plosive} and `excess_db > event_margin`, taken from the frames of the gap proper: the first `evidence_guard` after the gap start (the classifier's median smoothing and a note's release bleed across the boundary) and the preroll are excluded. `sibilant` and `breath` runs need at least `min_audible`; a `plosive` run counts at any length (bursts are about 15 ms). Adjacent classified runs stay separate regions (an affricate "ts" is a plosive then a sibilant). The run becomes a region of its class, padded by `audible_pad` each side, clipped to its neighbours (earlier region first) and to [tail end, preroll start).
2. **Level runs (slice 6)** that overlap no classified region stay `audible`. A level run partly covered is split: the covered part takes the class; an `audible` remainder shorter than `min_audible` is absorbed into the adjacent classified region.
3. Regions still tile the gap in order; `silence` is what is left.

## Why these choices

- **The same feature extractor, not a second one.** `measure_frames` and `classify_frames` exist, are tested and carry the research thresholds. With `voicing_prob` all 0 and no harmonics, the classifier can only return silence, plosive, sibilant or breath (verified in the code by the structure review).
- **`silence_level` overridden** to the level floor: a quiet fricative can sit below the absolute -70 dBFS test.
- **The level source is `activity.level - floor.curve`** (the same quantity the slice 6 rule uses, strict `>`): `FrameMetrics::level_db` is a different window and filter and is not used for the threshold. Storing the excess keeps the expensive pass threshold-free; `event_margin` is retuned without rerunning it.
- **`event_margin` (3 dB) below `tail_margin` (6 dB):** the level rule is the conservative safety net; the features can justify protecting something nearer the floor. A frame at the floor is never evidence (a flat floor has a high centroid and would be called sibilant everywhere).
- **Known limit, stated:** a fricative less than `event_margin` above the floor is not detected. Comparing each frame's band spectrum with `NoiseFloor::band_dbfs` (shape change at any level) is the v2 candidate; it needs calibration data we do not have.
- **Voiced note onsets look like plosive bursts** at voicing 0 (flux peak, closure dip, high-frequency rise): the guard and the preroll keep them out; a test pins it.
- **Burst statistics** (median + 3 MAD of flux) are now taken over the whole recording, not per note as in the research; provisional, recorded.
- **Sample rates 4-8 kHz:** `analyse()` accepts 4 kHz and up, `measure_frames` 8 kHz and up; below 8 kHz `analyse` passes neutral evidence (so the level rule alone protects), with a test.

## Effect on audio

None by itself (analysis only). It decides which stretches of a gap the gate (slice 7) and the later tools will not touch.

## Tests
- Unit (`classify_gap_frames_test`): synthetic scene, quiet floor with (a) a high-passed noise burst of 200 ms at +12 dB (sibilant), (b) a low-passed noise stretch at +8 dB (breath), (c) a broadband click of 10 ms after a closure dip (plosive), (d) a flat stretch at the floor, (e) a coloured (pink/low-passed) floor with slow drift: classes and `excess_db` as expected, (d) and (e) never evidence; level independence (the same sibilant at +5 dB and +20 dB gets the same class); `silence_level` is ignored; zero-length and 1-2 frame grids; the 8 kHz boundary; determinism; one test per error; a 10-minute noisy recording timing test.
- Unit (`build_segment_timeline_test`): neutral evidence reproduces every slice 6 expectation; regions get the class kinds; strict boundary (+2.9, exactly +3.0, +3.1 dB); a plosive followed immediately by a sibilant gives two regions; a classified run overlapped by a level run splits it and leaves no audible sliver shorter than `min_audible`; a sibilant shorter than `min_audible` is ignored, a plosive of one frame is kept; a loud note next to a quiet gap gives no classified region at the gap edge; a note onset does not create a plosive in or just before the preroll; regions always tile.
- Integration (`analyse_test`): a melody with a noise burst ("s") in a pause gets a `sibilant` region there and none elsewhere; a 6 kHz source analyses without error (neutral evidence).
- CLI (`cli_analyse_test`): the new kinds appear in the JSON.
- Audio: none (no audio is produced); the gate's audition (slice 7e) includes a fricative in a gap, gated, to check by ear that it survives.

## Open questions
1. Is a 3 dB `event_margin` the right start? It trades missed quiet fricatives against noise-floor wobble labelled as breath.
2. Voiced fricatives ("z", "v") between notes: they have harmonics, so with voicing 0 they are measured as noise with a lower centroid and tend to come out as `breath` or `audible`. They are protected either way; the label may be wrong (ADR-0017 leaves voiced consonants with the pitch, per the owner).

## Review log
- **Round 1 (structure-reviewer, 2026-10-22): CHANGES REQUIRED, B1-B4.** Answered in revision 2: B1 (ADR-0024 amendment 1: final `GapRegionKind`, the gate mutes `silence` only), B2 (neutral evidence below 8 kHz, test), B3 (`excess_db` = `activity.level - floor.curve`, strict, stored not thresholded), B4 (runs per class, no vote, plosive exempt from the minimum, clip and sliver rules). Also taken: S1 (required evidence argument, `neutral_gap_evidence`, `event_margin` in `GapParams`, `silence_level` documented as ignored, dead mapping deleted), S2 (to_text, README, test whitelist), S3 (evidence guard, onset/plosive test), the cost note, the added tests.
