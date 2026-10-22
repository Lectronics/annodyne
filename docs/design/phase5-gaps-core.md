# Design note: The segment timeline and the gate (Phase 5, gap slices 1-7)

- **Phase:** 5 (Timing)
- **Status:** draft, revision 1 (answers structure review round 1 of `silence-and-gaps.md`, findings B1-B12)
- **Supersedes, for Phase 5:** `silence-and-gaps.md` (kept as the long-term plan; where the two differ this note and ADR-0024 win) and the gap parts of `breath-lane.md` (breath detection and everything about breaths are **not** in this note: see "Deferred").
- **Spec sections:** architecture spec §2, §3, §5.1-5.2; ADR-0005, ADR-0008, ADR-0017, ADR-0023, ADR-0024 (proposed).
- **Folders / CMake targets:** `src/core/types` (new headers), `src/core/analysis/gaps` (layer 1), `src/core/edits` (`effective_gap`), `src/core/synthesis/gap` (layer 2, slice 7), `src/core/render` (slice 7), `src/macros/strip_silence` and `src/tools/gap` (layer 5, slice 7). The shift-first layout (slice 8) has its **own** design note and ADR amending ADR-0023 §3; it is not designed here.

## Purpose

The space between notes becomes an addressable object (a gap with regions), so a gate can turn down the noise between phrases without touching notes, and so the layout (slice 8) knows where time can be cut or inserted. This note covers what exists before any layout change: types, the noise floor, the timeline, gap regions, the gap gain edit and the gate render.

## What exists already (do not redefine)

`SegmentKind`, `SegmentId` (`{kind, string value}`), `segment_id(NoteId)` in `core/types/ids.h`; `TimeMap`, `TimeSpan`, `GeneratedTime` in `time_map.h`; `TimePair` in `time_pair.h`; `ActivityTrack` (per-frame `level` in dBFS, global `noise_floor`, activity) in `core/analysis/activity`; `EditCategory::gap`, `layout` as enum values without parameters. Where `silence-and-gaps.md` §2 shows a different definition (uint64 `SegmentId`, an "empty" range in `TimeSpan`, `Anchor`, a TimeMap "moved" to core/types) it is superseded by the code. `Anchor` is dropped (`TimePair` is the pair).

## Public types (core/types, data only)

```cpp
// core/types/segments.h
enum class GapRegionKind { tail, silence, noise };   // breath joins in Phase 6 with BreathEvent

struct GapRegion {
    GapRegionKind kind = GapRegionKind::silence;
    SourceTime start;
    SourceTime end;   // end > start
};

struct Gap {
    SegmentId id;                     // kind gap; value "gap:<before>|<after>" (see below)
    SourceTime start;
    SourceTime end;                   // end > start
    std::vector<GapRegion> regions;   // contiguous, in order, covering [start, end] exactly
    Decibels floor_dbfs{-120.0};      // the noise floor measured inside this gap (dBFS)
    std::optional<NoteId> before;     // the active note ending at `start` (none: leading gap)
    std::optional<NoteId> after;      // the active note starting at `end` (none: trailing gap)
};

struct Segment {
    SegmentId id;
    SourceTime start;
    SourceTime end;
    std::size_t gap_index = k_no_gap; // index into SegmentTimeline::gaps() for a gap segment
};

/// Invariant (class): segments ordered, contiguous, non-overlapping, covering [0, source_length]
/// exactly; kinds alternate except that two notes may touch (legato: no gap between them); no
/// zero-length gap; every gap's regions tile it; ids unique; a gap's before/after match its
/// neighbouring note segments.
class SegmentTimeline {
public:
    static Result<SegmentTimeline> make(std::vector<Segment> segments, std::vector<Gap> gaps,
                                        SourceTime source_end);
    std::span<const Segment> segments() const noexcept;
    std::span<const Gap> gaps() const noexcept;
    /// The segment holding `t`; a boundary belongs to the later segment (right-continuous), the
    /// source end to the last. Pre: 0 <= t <= source_end (asserted in debug, clamped in release).
    const Segment& at(SourceTime t) const noexcept;
    const Gap* gap_before(const NoteId&) const noexcept;  // nullptr if none
    const Gap* gap_after(const NoteId&) const noexcept;
    SourceTime source_end() const noexcept;
};
```

`make` returns `invalid_argument` for a hole, an overlap, a zero-length segment or region, regions that do not tile their gap, duplicate ids, a gap whose `before`/`after` do not name its neighbours, a gap index out of range, or a gap id whose kind is not gap.

```cpp
// core/types/noise_floor.h
struct NoiseFloor {
    FrameGrid grid;                         // source axis
    std::vector<Decibels> level_dbfs;       // the floor curve, one per grid frame
    std::vector<Hz> band_centre;            // band centres, ascending (log-spaced)
    std::vector<Decibels> band_dbfs;        // average floor spectrum, same size as band_centre
};

// core/types/effective_gap.h
struct EffectiveGap {
    SegmentId gap;
    Decibels gain{};      // gap_gain folded; <= k_gap_mute_db means exactly silent
    bool is_edited = false;
};
inline constexpr Decibels k_gap_mute_db{-120.0};
```

**Gap ids.** `"gap:" + before + "|" + after` with the note ids, empty for the missing side (leading `gap:|n1`, trailing `n9|`, a recording with no note `gap:|`). Notes are the only things that name a gap, so an id survives re-analysis as long as its two notes do; remapping gap ids through `NoteIdMap` is slice 3's `remap` (a gap id is rewritten when both neighbours are remapped, orphaned otherwise).

**`AnalysisResult`** gains `std::optional<SegmentTimeline> timeline` and `std::optional<NoiseFloor> noise_floor` (empty by default: every existing construction stays valid). `validate(AnalysisResult)` additionally requires, when present, that every active note is exactly one note segment with the note's range, that the floor grid is valid and its curves match the grid, and the floor's bands are ascending and equal in size. Only `is_active` notes partition the timeline (ghost candidates do not). **No `version` bump** (fields are added, no existing output changes); the fingerprint and JSON outputs gain the new fields and are re-baselined deliberately in slice 5.

## Public functions

### `estimate_noise_floor` (`core/analysis/gaps`, slice 4)

```cpp
struct NoiseFloorParams {
    Seconds window{3.0};          // sliding window of the floor curve; clamped to the recording
    double percentile_prob = 0.1; // of the frame levels inside the window that are not in a note
    int bands = 24;
    Hz band_low{80.0};
    Hz band_high{12000.0};
};
/// The time-varying noise floor and the average floor spectrum of `source`.
/// Built on the ActivityTrack (`activity.level` are the frame levels, `activity.noise_floor` the
/// global floor): the curve at frame i is the `percentile_prob` quantile of the levels of the frames
/// within window/2 of i that lie outside every active note (frames inside a note are skipped); a
/// window with no such frame takes the nearest estimate on either side, and if there is none the
/// global `activity.noise_floor`. Digital silence is clamped at the activity stage's absolute floor.
/// The spectrum is the mean power in log-spaced bands (80 Hz .. 12 kHz) over the frames the curve
/// treats as floor, in dBFS (full-scale sine = 0 dBFS per band), of the mono mix.
/// Errors: invalid_argument (bad params, a grid that does not match the activity track), out_of_range
/// (the recording is empty). A recording shorter than the window is fine (clamped).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<NoiseFloor> estimate_noise_floor(const AudioSource&, const ActivityTrack&,
                                                      std::span<const Note> notes,
                                                      const NoiseFloorParams&);
```

### `build_segment_timeline` (`core/analysis/gaps`, slices 5 and 6)

```cpp
struct GapParams {
    Decibels tail_margin{6.0};   // a tail lasts while the floor curve + margin is exceeded
    Seconds max_tail{1.5};
    Decibels noise_margin{10.0}; // a short event this far above the floor inside a gap is noise
    Seconds noise_max_length{0.080};
    Seconds preroll_guard{0.015};
    Seconds min_gap{0.005};      // shorter spaces between notes are legato (no gap)
};
/// Partitions [0, source_end] into note and gap segments and classifies each gap's regions:
/// `tail` from the previous note's end while the activity level stays above floor + tail_margin
/// (at most max_tail); `noise` for runs of frames above floor + noise_margin shorter than
/// noise_max_length; `silence` for the rest. The `preroll_guard` before a note's start is a
/// protected sub-region of the gap (a region boundary), **not** an extension of the note: the note
/// range stays `Note::source_start/end` everywhere. A gap shorter than min_gap is not made; the two
/// notes touch. Slice 5 emits one `silence` region per gap; slice 6 adds tail and noise.
/// Errors: invalid_argument (bad params; active notes overlapping or unsorted; a note outside
/// [0, source_end]). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<SegmentTimeline> build_segment_timeline(std::span<const Note> notes,
                                                             SourceTime source_end,
                                                             const ActivityTrack&,
                                                             const NoiseFloor&,
                                                             const GapParams&);
```

Inputs are spans of notes, not `AnalysisResult` (the outputs are stored in it), so tests use synthetic notes. **Order in `analyse()`:** activity -> pitch -> notes -> noise floor -> timeline. Breath detection (Phase 6) will run between the floor and the final timeline and add `breath` regions; nothing here prevents that.

### `effective_gap` (`core/edits`, slice 3)

```cpp
/// Folds the `gap` edits that target `gap` in seq order up to the undo cursor.
[[nodiscard]] EffectiveGap effective_gap(const SegmentId& gap, const EditList&);
```
A gain within `k_neutral_snap_offset` of 0 dB folds to `is_edited == false`. A gap edit on a note id, or a note edit on a gap id, is refused by `validate(EditSpec)` (target-kind rule, ADR-0024).

### `render_gap` and the gate (`core/synthesis/gap` + `core/render`, slice 7)

Gate = `gap_gain` on a gap: the `silence` and `noise` regions change level by `gain` (exactly zero at `k_gap_mute_db`); `tail` regions and notes are untouched; region edges get an equal-power-free linear fade of `gate_fade` (5 ms) so there is no step. `render_gap(src, gap, effective_gap, range) -> Result<AudioBuffer>` is a free function (like `stretch_span`, ADR-0023: it renders part of the unedited base of a region, it is not chosen per note). `render_region` treats a gap with an edit as an edited span: its samples replace the base over the gap's output range, with the gate fade at region edges and no blend ramps at the gap's note-facing edges (notes are untouched and the regions start where the note ends). Dirty region of a gap edit: the gap's source range mapped through the map (gain does not move time). The cache key hashes the `EffectiveGap` and the region boundaries of every gap edit that meets the region, and the gap's floor.

## Deferred (not in this note)

Layout and ripple (slice 8 note; anchors, `layout` edits, ARA/host region questions are out of Phase 5); room tone and `shaped_noise` (slice 9); tighten and remove, `gap_target_duration`, the planner minimum, `GapTool` drag and `StripSilence` modes other than gate (slice 10); breath detection, `BreathEvent`, `BreathId`, the breath chain, the dynamics classes, breath edits and `breath_assignment` (breath-lane.md: Phase 6, 10; the review's finding B11 stands for when they are designed). `tools/gap` is only the gain/mute gesture.

## Split / join decisions

- **Types in `core/types`** because `AnalysisResult` (layer 0) holds them; `analysis/gaps` links `core/types` and `analysis/activity` only.
- **`SegmentTimeline` is a class** (partition invariant); `Gap`, `Segment`, `GapRegion`, `NoiseFloor`, `EffectiveGap` are plain structs.
- **`estimate_noise_floor` and `build_segment_timeline` separate:** the floor is reused by room tone (slice 9) and the breath detector; the timeline needs the floor but not its spectrum (split rule 4).
- **Mute is a gain at `k_gap_mute_db`**, not a flag: no new `ValueKind`, no bool in `EditValue`, one parameter (join rule: a flag and a gain that must be kept consistent are one value).
- **`render_gap` free function** (as `stretch_span`): it does not go behind `SynthesisEngine` because the renderer calls it for gaps, which are not notes.

## Tests (per slice, written first)

- **Types (2):** `SegmentTimeline::make` accepts a tiling and rejects each listed error; `at()` boundary and clamping; `gap_before/after`; `validate(AnalysisResult)` with and without a timeline; existing analysis tests unchanged.
- **Edits (3):** `gap_gain` set/add/reset fold; target-kind rule both ways; neutral snap; `effective_gap`; `remap` of gap ids (both neighbours remapped, one orphaned).
- **Floor (4):** tone bursts over -60 dBFS pink noise: curve within 2 dB, spectrum slope within 3 dB; short recording; all digital silence (floor at the clamp, no NaN); continuous singing with no gap frame (nearest/global fallback); stereo mixed down.
- **Timeline (5, 6):** tiling property on random note sets; legato notes have no gap; leading and trailing gaps; ghost notes ignored; ids as specified; `analyse()` fills a valid timeline; tail detected on a decaying tone; `preroll_guard` is a region boundary and the note range is unchanged; noise events classified.
- **Gate (7):** no gap edit: render bit-identical and `passthrough` as today; gate -24 dB: output length unchanged, note samples bit-identical, silence-region RMS -24 +/- 0.5 dB, tail untouched, no step at region edges (click detector); mute gives exact zeros; cache key follows the edit; a gap edit combined with a stretched map renders (gaps inside a slope-1 span only; inside a stretched span `not_implemented`, named).
- **Tolerances** (floor +/- 2 dB, gate +/- 0.5 dB) are provisional values in `tests/tolerances.h` with a comment; that file is the owner's: they are proposed, not loosened.

## Review log

*(round 2 findings go here; round 1 findings B1-B12 and S1-S13 of `silence-and-gaps.md` are answered above: B1 existing types, B2 representation, B3 types in core/types, B4 narrow inputs and order, B5 no `allocate_stretch`, `build_time_map` kept (slice 8), B6 slice 8 note, B7 `StripSilence` clamps in the planner (slice 10), B8 ADR-0024, B9 `EffectiveGap` in types, B10 floor builds on `ActivityTrack`, B11 breaths deferred, B12 render integration for the gate, S3 preroll as a region.)*
