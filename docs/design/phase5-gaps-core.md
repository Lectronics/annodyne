# Design note: The segment timeline and the gate (Phase 5, gap slices 1-7)

- **Phase:** 5 (Timing)
- **Status:** draft, revision 2 (round 1 findings B1-B12 and round 2 findings B-R1..B-F1, S-1..S-7 answered)
- **Supersedes, for Phase 5:** `silence-and-gaps.md` (kept as the long-term plan; where the two differ this note and ADR-0024 win) and the gap parts of `breath-lane.md` (breath detection and everything about breaths are **not** in this note: see "Deferred").
- **Spec sections:** architecture spec §2, §3, §5.1-5.2; ADR-0005, ADR-0008, ADR-0017, ADR-0023, ADR-0024 (proposed).
- **Folders / CMake targets:** `src/core/types` (new headers), `src/core/analysis/gaps` (layer 1), `src/core/edits` (`effective_gap`), `src/core/render` (`gate`, slice 7), `src/macros/strip_silence` and `src/tools/gap` (layer 5, slice 7). The shift-first layout (slice 8) has its **own** design note and ADR amending ADR-0023 §3; it is not designed here.

## Purpose

The space between notes becomes an addressable object (a gap with regions), so a gate can turn down the noise between phrases without touching notes, and so the layout (slice 8) knows where time can be cut or inserted. This note covers what exists before any layout change: types, the noise floor, the timeline, gap regions, the gap gain edit and the gate render.

## What exists already (do not redefine)

`SegmentKind`, `SegmentId` (`{kind, string value}`), `segment_id(NoteId)` in `core/types/ids.h`; `TimeMap`, `TimeSpan`, `GeneratedTime` in `time_map.h`; `TimePair` in `time_pair.h`; `ActivityTrack` (per-frame `level` in dBFS, global `noise_floor`, activity) in `core/analysis/activity`; `EditCategory::gap`, `layout` as enum values without parameters. Where `silence-and-gaps.md` §2 shows a different definition (uint64 `SegmentId`, an "empty" range in `TimeSpan`, `Anchor`, a TimeMap "moved" to core/types) it is superseded by the code. `Anchor` is dropped (`TimePair` is the pair).

## Public types (core/types, data only)

```cpp
// core/types/segments.h
enum class GapRegionKind { tail, protected_preroll, silence };   // `noise` and breath join with room tone / BreathEvent

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
    Decibels floor_dbfs{-120.0};      // median of the floor curve over the gap (RMS dBFS)
    std::optional<NoteId> before;     // the active note ending at `start` (none: leading gap)
    std::optional<NoteId> after;      // the active note starting at `end` (none: trailing gap)
};

/// A note segment holds the note's id; a gap segment the index of its Gap. Id, start and end of a
/// gap live only in the Gap (no second copy).
struct NoteSegment { NoteId note; SourceTime start; SourceTime end; };
using Segment = std::variant<NoteSegment, std::size_t /* index into gaps() */>;

/// Invariant (class): segments ordered, contiguous, non-overlapping, covering [0, source_length]
/// exactly; kinds alternate except that two notes may touch (legato: no gap between them); no
/// zero-length gap; every gap's regions tile it; ids unique; a gap's before/after match its
/// neighbouring note segments.
class SegmentTimeline {
public:
    static Result<SegmentTimeline> make(std::vector<Segment> segments, std::vector<Gap> gaps,
                                        SourceTime source_end);
    std::span<const Segment> segments() const noexcept;  // ordered; the variant says note or gap
    std::span<const Gap> gaps() const noexcept;
    /// The segment holding `t`; a boundary belongs to the later segment (right-continuous), the
    /// source end to the last. Pre: 0 <= t <= source_end (asserted in debug, clamped in release).
    const Segment& at(SourceTime t) const noexcept;  // the last segment holds source_end
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
    std::vector<Decibels> curve_dbfs;       // the floor curve, one per grid frame (RMS dBFS, below)
    std::vector<Hz> band_centre;            // band centres, ascending (log-spaced)
    std::vector<Decibels> band_dbfs;        // per-band mean power of the floor frames, RMS dBFS
                                            // (not summable to the curve); same size as band_centre
};

// core/types/effective_gap.h
struct EffectiveGap {
    SegmentId gap;
    Decibels gain{};      // gap_gain folded; <= k_gap_mute_db means exactly silent
    bool is_edited = false;
};
inline constexpr Decibels k_gap_mute_db{-120.0};
```

**dBFS convention (one for the floor).** Every level in this note is the RMS dBFS of the activity stage: `10*log10(mean square)`, so a full-scale sine reads -3.01 dB. That holds for `NoiseFloor::curve_dbfs`, `band_dbfs` and `Gap::floor_dbfs`. Names: `ActivityTrack::noise_floor` is the global scalar, `NoiseFloor::curve_dbfs` the time-varying curve, `Gap::floor_dbfs` the median of the curve over the gap.

**Gap ids.** `SegmentId{gap, "gap:" + <len>:<before> + <len>:<after>}`: each side length-prefixed (`5:n0001`, empty side `0:`), so no note id text can make two different pairs collide (`gap:0:5:n0001` leading, `gap:5:n00090:` trailing, `gap:0:0:` for a recording with no note). **Remap does not parse the text:** `EditList::retargeted` gets an overload taking the old and the new `SegmentTimeline` besides the `NoteIdMap`: a gap edit follows the new gap whose neighbours are the images of the old gap's neighbours (a split note: the last child is the gap's `before`, the first child its `after`); a merged-away or vanished gap orphans its edits (`orphaned_segment_ids`). Re-analysis changes note ids often (they are time-derived), so this is the main path, not an edge case (slice 3 tests: split, merge, a pair that would collide without the prefix, a note-only remap that leaves gap ids alone).

**`AnalysisResult`** gains `std::optional<SegmentTimeline> timeline` and `std::optional<NoiseFloor> noise_floor` (empty by default: every existing construction stays valid). `analyse()` leaves both empty (no error) for non-melodic algorithms and when active notes overlap; a present timeline that disagrees with `notes` fails `validate`. `validate(AnalysisResult)` additionally requires, when present, that every active note is exactly one note segment with the note's range, that the floor grid is valid and its curves match the grid, and the floor's bands are ascending and equal in size. Only `is_active` notes partition the timeline (ghost candidates do not). **No `version` bump** (fields are added, no existing output changes); the fingerprint and JSON outputs gain the new fields and are re-baselined deliberately in slice 5.

## Public functions

### `estimate_noise_floor` (`core/analysis/gaps`, slice 4)

```cpp
struct NoiseFloorParams {
    Seconds window{3.0};          // sliding window of the floor curve; clamped to the recording
    double percentile_prob = 0.1; // of the frame levels inside the window that are not in a note
    int bands = 24;
    Hz band_low{80.0};
    Hz band_high{12000.0};        // clamped to 0.45 x the sample rate; bands above are dropped
};
/// The time-varying noise floor and the average floor spectrum of `source`.
/// The caller passes the same `ActivityTrack` that was computed from this source (the mono mix,
/// 60 Hz high-pass front end, `activity.level` = RMS dBFS per grid frame); only the grid is
/// checked. Curve: at frame i the `percentile_prob` quantile of `activity.level` over the frames
/// within window/2 of i that lie outside every active note; a window with no such frame takes the
/// nearest estimate on either side, else the global `activity.noise_floor`. Frames at digital
/// silence are clamped at the activity stage's absolute floor. Spectrum: over the frames that
/// supplied the curve's estimates, the mean power per log-spaced band (its own 25 ms frames, same
/// hop), in RMS dBFS; it is a mean, not a percentile, and not summable to the curve (band-limited,
/// different statistic).
/// Errors: invalid_argument (bad params, a grid that does not match the activity track),
/// out_of_range (empty recording). A recording shorter than the window is fine.
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
    Seconds preroll_guard{0.015};
    Seconds min_gap{0.005};      // shorter spaces between notes are legato (no gap)
};
/// Partitions [0, source_end] into note and gap segments and classifies each gap's regions:
/// `tail` from the previous note's end while the activity level stays above floor + tail_margin
/// (at most max_tail); `protected_preroll` for the `preroll_guard` before a note's start (untouched by the gate, like
/// a tail: a breath-in or a soft consonant lead-in is not silence; it is a region of the gap, **not**
/// an extension of the note, whose range stays `Note::source_start/end` everywhere); `silence` for
/// the rest. A gap shorter than min_gap is not made; the two
/// notes touch. Slice 5 emits one `silence` region per gap; slice 6 adds tail and preroll. (`noise` regions and `noise_*` parameters wait for room tone, slice 9: no consumer before.)
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
A gain within `k_neutral_snap_offset` of 0 dB folds to `is_edited == false`. **Target-kind rule (ADR-0024), exactly:** the category -> allowed `SegmentKind` table is `gap` -> gap; `layout` -> none yet (any target refused except as below); every other category -> note. The rule applies to `segment_ids` **and** `orphaned_segment_ids`, to every op **including reset** (a note-category reset on a gap id is refused, symmetric), and a mixed-kind target is refused. The only exception is a parameterless reset of `gap`/`layout`, which keeps passing with any target as today (`edit_types_test`: "the_new_categories_accept_only_a_parameterless_reset" is rewritten to use gap ids for `gap`); `edit_list_test` "gap_targets_pass_through_retargeting_unchanged" is rewritten (it builds a pitch edit with a mixed note+gap target: `retargeted` does not validate, so it builds the `Edit` directly). `tests/support/random_scenarios.h::random_spec` skips `gap_gain` for note targets. `EditParam::gap_gain` is appended last (`k_edit_param_count` 19 -> 20; the table is positional), one name row, one `offset_row(gap_gain, gap, decibels, k_gap_mute_db, +24)`. The fold: `effective_gap` shares `edits/detail/fold` with `effective_state` (set/add/reset, snap, undo cursor; the second consumer, not a copy), and clamps `gap_gain` to [`k_gap_mute_db`, +24] after folding (an `add` after a mute stays at the mute). The neutral snap is the double `k_neutral_snap_offset` (1e-9 dB).

### The gate: a gain curve on the finished base (`core/render`, slice 7)

The gate is **not** a replacement signal. It is a per-sample gain applied **after** `render_unedited` and `blend`, so it works for any slope (a gap inside a stretched span is gated like any other) and needs no zone, no new blend signal and no `not_implemented`.

```cpp
struct GateParams { Seconds gate_fade{0.005}; };
/// The gain, one per output sample of `range`, of the gap edits that apply there: 1 outside every
/// gated region; inside a `silence` region of a gap with `EffectiveGap::is_edited` the linear gain
/// of `gain` (exactly 0 at k_gap_mute_db), ramping from 1 to that gain over `gate_fade` at a region
/// edge that borders an ungated neighbour (a note, a tail or a protected_preroll region), never into
/// the neighbour; no ramp between two gated regions or at the recording's start or end. A region
/// shorter than 2 x gate_fade ramps over half its length each side. Region boundaries are the gap's
/// SourceTime ranges through the map: `to_output` / `to_output_end`.
/// Errors: invalid_argument (bad params; a gated region whose start or end maps into a cut: the
/// gap is skipped? no: error, as for a note); out_of_range (range outside the output).
/// Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<float>> gate_curve(const Gap&, const EffectiveGap&, const TimeMap&,
                                                    Hz rate, OutputRange range, const GateParams&);
```

- **Order and overlap.** The gate multiplies the already-blended base inside a gated region's output range. A note's blend margin (up to 20 ms) that falls inside a gap is gated like the rest of the gap; the gain never reaches into a note. A gate on a silence region adjacent to an edited neighbour is therefore tested (below).
- **`render_region` changes** (ADR-0024 lists them): `RenderParams` gains `GateParams gate`; `RenderKeyInputs` gains `gates` (for every gap edit meeting the region: the region boundaries as output samples, the `EffectiveGap`, `gate_fade`) and `k_key_format_version` becomes 4; `RenderResult` gains `gaps_rendered` (ids) and `passthrough` is `notes_rendered.empty() && gaps_rendered.empty()`; the `Plan` gains the gate list. **A gap edit whose id is not in `analysis->timeline`, or any gap edit when the timeline is absent, is an `invalid_argument` naming the id** (an orphaned gap edit, `orphaned_segment_ids`, is ignored as for notes).
- **`dirty_regions`** gains a parameter, the `SegmentTimeline`: a gap-gain change dirties the gap's source range (gain does not move time); a timing change keeps its origin-to-end rule. A public-API change, listed in ADR-0024.
- **No `synthesis/gap` target** (the kernel has one consumer and is not an engine): `gate_curve` lives in `core/render/gate.{h,cpp}`. `macros/strip_silence` (gate mode) and `tools/gap` (gain, mute gesture) write `gap_gain` edits only.

## Slices (one concern per commit group)

1. This note and ADR-0024 reviewed (round 3 if findings remain). 2. Types and validation (`segments.h`, `noise_floor.h`, `effective_gap.h`, `AnalysisResult` fields, `validate`). 3. Edit parameter `gap_gain`, target-kind rule, shared fold, `effective_gap`, `remap` with timelines. 4. `estimate_noise_floor`. 5. `build_segment_timeline` (silence regions), wired into `analyse()`, fixtures re-baselined. 6. Tail and `protected_preroll` regions. 7. The gate: `gate_curve`, `render_region`/key/result/`dirty_regions` changes, `macros/strip_silence` (gate) and `tools/gap` (gain, mute), scenario JSON gap target, audition scenario. Slice 8 (shift-first layout) follows with its own note.

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
- **More (review S-5):** gate leaves everything outside the gap's output range bit-identical (rule 5) and a region cut in the middle of a gap equals the same slice of a full render; stereo; undo cursor on a gap edit; a missing/orphaned/absent-timeline gap edit; a gap edit next to an edited note whose blend zone overlaps the gap; the leading and trailing gap (no ramp at the recording's ends); a region shorter than 2 x `gate_fade`; a gap edit under a time-shifted and a stretched map; the 15 ms `protected_preroll` before a note unchanged by the gate; `dirty_regions` and `remap` for gaps; the floor of white noise at X dBFS RMS reads X and `band_dbfs` of a sine reads -3.01 in its band; bands above 0.45 x the rate dropped at 22.05 kHz; `SegmentTimeline::at` on the last segment; `analyse()` leaves the timeline empty for overlapping notes; `validate(EditSpec)` target rules; the fingerprint/JSON/plot outputs re-baselined in slice 5 (files named in that commit); a scenario gate test and an audition scenario (a `"gap": {"before": id, "after": id}` target in the scenario JSON, the timeline built from the scenario notes with `build_segment_timeline`).
- **Tolerances** (floor +/- 2 dB, gate +/- 0.5 dB) are provisional values in `tests/tolerances.h` with a comment; that file is the owner's: they are proposed, not loosened.

## Review log

*(round 2 findings go here; round 1 findings B1-B12 and S1-S13 of `silence-and-gaps.md` are answered above: B1 existing types, B2 representation, B3 types in core/types, B4 narrow inputs and order, B5 no `allocate_stretch`, `build_time_map` kept (slice 8), B6 slice 8 note, B7 `StripSilence` clamps in the planner (slice 10), B8 ADR-0024, B9 `EffectiveGap` in types, B10 floor builds on `ActivityTrack`, B11 breaths deferred, B12 render integration for the gate, S3 preroll as a region.)*
