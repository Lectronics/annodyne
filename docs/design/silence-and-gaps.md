# Design note: Gaps, silence removal and the segment timeline

- **Phase:** 5 (Timing) — must be reviewed before any Phase 5 code, because it changes the time model
- **Status:** draft
- **Requires:** ADR-0008 (docs/decisions/0008-segment-timeline-and-timemap-spans.md) (segment timeline and TimeMap spans)
- **Related:** `note-stretching.md`, `breath-lane.md`
- **Spec sections changed:** `annodyne-core-architecture.md` §2 (time axes), §3 (types), §5.4 (TimeMap). This note supersedes §5.4 step 5.
- **Folders / CMake targets:**
  - `src/core/types/` — `Segment`, `Gap`, `SegmentTimeline`, `TimeMap` (data only, layer 0)
  - `src/core/analysis/gaps/` — noise floor, gap detection and classification (layer 1)
  - `src/core/timemap/` — layout solver, `build_time_map` (layer 3)
  - `src/core/synthesis/gap/` — gap rendering: gate, cut, room-tone insert (layer 2)
  - `src/tools/gap/`, `src/macros/strip_silence/` — user edits (layer 5)

## Purpose

Make the space **between** notes a first-class, editable object, so users can:

- **Gate** silences: turn down or mute the noise floor between phrases without touching notes.
- **Tighten** a performance: shorten long gaps, with later material moving earlier inside a chosen range.
- **Remove** silences entirely (spoken word, a cappella, comping), down to a minimum gap.

Doing this properly changes how time works. Until now the `TimeMap` was a continuous stretch built from note anchors, and gaps were whatever was left over. From here on, a track's timeline is a **sequence of segments, notes and gaps, that together cover every instant of the source**. Each segment is laid out individually, and output time is assembled from them. Gaps can be **cut** (source time removed) and **extended** (new room tone generated), which a stretch-only `TimeMap` can't express.

---

## 1. What changes in the time model

### 1.1 The segment timeline

Every track's source time is partitioned into contiguous segments with no holes and no overlaps:

```
source time ──────────────────────────────────────────────────────────►
| gap | note | note | gap        | note | gap  | note | note | gap |
        └ legato: no gap between notes when they touch
```

- **Note segment**: one analysed note, from its true start (including its consonant) to its end.
- **Gap segment**: everything between two notes, or before the first note / after the last. A gap is divided into **sub-regions**: `tail` (decay or reverb of the previous note), `silence` (at the noise floor), `breath` (see `breath-lane.md`), and `noise` (clicks, lip smacks, rustles).
- Touching notes (legato) have no gap between them. A zero-length gap is never created.

### 1.2 Layout: per-segment output positions

Each segment gets an **output start** and an **output duration**. A note's duration comes from its stretch edits (`note-stretching.md`). A gap's duration comes from gap edits and the layout rules below. Output time is then built segment by segment.

Gaps act as **springs** between notes:

1. Notes are placed where their `time` edits put them.
2. A note moved earlier **shortens the gap before it** (the gap is cut). A note moved later **lengthens that gap** (room tone is inserted).
3. Only if a gap is used up (would go below `min_gap`) is stretching used: a second handle is added automatically at the next transient, and the audio between the two handles is stretched using the note-stretching rules (steady region first, never the attack). Stretching happens only when needed; shifting adds no stretch artefacts (owner decision, ADR-0008).

This replaces architecture spec §5.4 step 5 ("stretch the neighbour's boundary region"). Moving a note no longer alters its neighbour's sound unless there is no gap left to absorb the move.

### 1.3 Three kinds of time span

The `TimeMap` becomes an ordered list of **spans**. Each span is one of:

| Span | Source range | Output range | Meaning |
|---|---|---|---|
| `warp` | `[s0, s1)` | `[τ0, τ1)` | Source audio played back, linearly time-scaled (slope = (τ1−τ0)/(s1−s0); 1 = untouched) |
| `cut` | `[s0, s1)` | empty | Source audio removed (shortened gap) |
| `insert` | empty | `[τ0, τ1)` | Output with no source: generated room tone or digital silence (lengthened gap) |

Invariants (checked in debug, tested as contracts):
- Spans are ordered; source ranges of `warp` + `cut` spans tile the source exactly; output ranges of `warp` + `insert` spans tile the output exactly.
- Both axes are monotonic.
- `cut` and `insert` spans only occur **inside gap segments**. Notes are only ever warped.

Consequences for every caller:
- `to_output(SourceTime)` can fail: a source time in a `cut` span has no output position. It returns `std::nullopt`.
- `to_source(OutputTime)` can return "generated": a time in an `insert` span has no source. It returns a variant.
- Code that assumed `M⁻¹` always exists (rendering, the UI playhead, MIDI export) must handle both cases. The compiler enforces this through the return types.

### 1.4 Ripple scopes and anchors (keeping sync with the rest of the song)

Removing time from a vocal that plays against a backing track would push everything after it out of sync. So time removal is always bounded:

- An **anchor** is an output time that must not move (default: the source time it was placed at). The track start and end are always anchors.
- A **ripple scope** is the range between two anchors. When gaps inside a scope are cut, later material in that scope moves earlier, and the removed time is **collected as silence just before the next anchor**. Material after the anchor stays exactly where it was.
- With no user anchors, the scope is the whole track, and the removed time ends up at the track end.

| Mode | Time changes? | Typical use |
|---|---|---|
| **Gate** (default) | No | Mixing a vocal against a band: quieter gaps, perfect sync |
| **Tighten** | Yes, within scopes | Phrase edits, closing late entries inside a section |
| **Remove** | Yes, within scopes | Spoken word, a cappella, comping takes |

In a DAW over ARA, the host region's length is fixed. Material pushed past the region end is not played, and removed time shows up as silence at the region end. The UI must show this (Open question 2).

---

## 2. Public types

```cpp
namespace anno {

// ---------- core/types/segments.h ----------
enum class SegmentKind { note, gap };

struct SegmentId { std::uint64_t value; };   // stable across re-analysis where possible

enum class GapRegionKind { tail, silence, breath, noise };

struct GapRegion {
    GapRegionKind kind;
    SourceTime start;
    SourceTime end;
    std::optional<BreathId> breath;          // set when kind == breath
};

struct Gap {
    SegmentId id;
    SourceTime start;
    SourceTime end;
    std::vector<GapRegion> regions;          // contiguous, cover [start, end)
    Decibels floor_level;                    // noise floor measured inside this gap
    std::optional<NoteId> before;            // note ending at start (none for leading gap)
    std::optional<NoteId> after;             // note starting at end (none for trailing gap)
};

struct Segment {
    SegmentId id;
    SegmentKind kind;
    SourceTime start;
    SourceTime end;
    std::variant<NoteId, std::size_t> ref;   // NoteId, or index into gaps
};

/// Partition of a track's source time into notes and gaps.
/// Invariant: segments are contiguous, ordered, non-overlapping and cover
/// [0, source length) exactly; no zero-length gaps.
class SegmentTimeline {
public:
    [[nodiscard]] static Result<SegmentTimeline> make(std::vector<Segment> segments,
                                                      std::vector<Gap> gaps,
                                                      Seconds source_length);
    [[nodiscard]] std::span<const Segment> segments() const noexcept;
    [[nodiscard]] std::span<const Gap> gaps() const noexcept;
    [[nodiscard]] const Segment& at(SourceTime t) const;          // segment containing t
    [[nodiscard]] std::optional<const Gap*> gap_before(NoteId) const;
    [[nodiscard]] std::optional<const Gap*> gap_after(NoteId) const;
private:
    std::vector<Segment> segments_;
    std::vector<Gap> gaps_;
};

// ---------- core/types/time_map.h (moved here from core/timemap, ADR-0008) ----------
enum class SpanKind { warp, cut, insert };

struct TimeSpan {
    SpanKind kind;
    SourceTime source_start, source_end;     // empty for insert
    OutputTime output_start, output_end;     // empty for cut
    SegmentId segment;                       // which segment produced it
};

struct GeneratedTime { SegmentId segment; Seconds offset; };   // position inside an insert span

using SourceLookup = std::variant<SourceTime, GeneratedTime>;

/// Ordered spans mapping source time to output time.
/// Invariants: see design note §1.3, checked by make(). In normal use only
/// timemap::build_time_map calls make(); tests may call it directly.
class TimeMap {
public:
    [[nodiscard]] static Result<TimeMap> make(std::vector<TimeSpan> spans);
    [[nodiscard]] static TimeMap identity(Seconds source_length);
    [[nodiscard]] std::optional<OutputTime> to_output(SourceTime s) const;   // nullopt in a cut
    [[nodiscard]] SourceLookup to_source(OutputTime t) const;
    [[nodiscard]] double slope_at(OutputTime t) const;                       // 0 inside insert spans
    [[nodiscard]] std::span<const TimeSpan> spans() const noexcept;
    [[nodiscard]] Seconds output_length() const noexcept;
    [[nodiscard]] bool is_identity() const noexcept;
private:
    std::vector<TimeSpan> spans_;            // invariant: §1.3
};

struct Anchor { OutputTime at; SourceTime origin; bool user_placed; };

} // namespace anno
```

---

## 3. Public functions

### 3.1 `estimate_noise_floor` (`core/analysis/gaps/`)

```cpp
/// Estimate the recording's noise floor: a slowly varying level curve plus an
/// average noise spectrum (used to generate matching room tone).
/// Method: 20 ms RMS frames; floor(t) = params.percentile of frame levels in a
/// sliding window of params.window, excluding frames inside notes' steady regions.
/// Returns the level curve on a FrameGrid (source time) and a band spectrum.
/// Error if the source is shorter than one window. Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<NoiseFloor> estimate_noise_floor(const AudioSource& src,
                                                      const AnalysisResult& analysis,
                                                      const NoiseFloorParams& params);

struct NoiseFloorParams {
    Seconds frame{0.020};
    Seconds window{3.0};
    double percentile = 10.0;
    int spectrum_bands = 32;
};

struct NoiseFloor {
    FrameGrid grid;
    std::vector<Decibels> level;             // per frame
    std::vector<Decibels> band_spectrum;     // average floor spectrum (room tone shape)
};
```

### 3.2 `build_segment_timeline` (`core/analysis/gaps/`)

```cpp
/// Partition the track into notes and gaps, then classify each gap's sub-regions.
///  - tail:    from the previous note's end while the level stays above
///             floor + params.tail_margin, up to params.max_tail
///  - breath:  BreathEvents from detect_breaths() that fall inside the gap
///  - noise:   short events above floor + params.noise_margin that are not breaths
///  - silence: everything else
/// Also extends each note's start backwards by params.preroll_guard if energy above
/// the floor precedes the detected onset (protects soft consonants).
/// Returns a valid SegmentTimeline. Error if note spans overlap in a way that
/// cannot be partitioned (analysis bug). Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<SegmentTimeline> build_segment_timeline(const AnalysisResult& analysis,
                                                             const NoiseFloor& floor,
                                                             std::span<const BreathEvent> breaths,
                                                             const GapParams& params);

struct GapParams {
    Decibels tail_margin{6.0};
    Seconds max_tail{1.5};
    Decibels noise_margin{10.0};
    Seconds noise_max_length{0.080};         // longer non-breath events are noise too, but flagged
    Seconds preroll_guard{0.015};
    Seconds min_gap{0.005};                  // shorter spaces between notes count as legato
};
```

`build_segment_timeline` runs as part of `analyse()` and is stored in `AnalysisResult` (new field `timeline`).

### 3.3 `solve_layout` (`core/timemap/`)

```cpp
/// Compute each segment's output start and duration from the edits.
/// Order: (1) place notes from time edits and note-stretch allocations;
/// (2) gaps absorb the difference, shrinking to their minimum (§3.5) or growing;
/// (3) where a gap cannot absorb, compress/stretch the neighbouring notes via
///     note-stretching rules; (4) apply gap length edits (tighten/remove) within
///     ripple scopes and collect removed time before the next anchor.
/// Returns the layout and a list of warnings (e.g. material pushed past the end).
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<Layout> solve_layout(const SegmentTimeline& timeline,
                                          const EditList& edits,
                                          std::span<const StretchAllocation> note_stretches,
                                          const LayoutParams& params);

struct SegmentPlacement {
    SegmentId segment;
    OutputTime start;
    Seconds duration;
};

struct Layout {
    std::vector<SegmentPlacement> placements;   // same order as timeline segments
    std::vector<Anchor> anchors;
    std::vector<LayoutWarning> warnings;
};
```

### 3.4 `build_time_map` (`core/timemap/`) — replaces the previous signature

```cpp
/// Turn a layout into spans. Note segments → warp spans from their stretch anchors.
/// Gap segments → from plan_gap_layout(): warp (kept head/tail), cut (removed
/// middle) and insert (added room tone). Error if any invariant in §1.3 fails.
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<TimeMap> build_time_map(const SegmentTimeline& timeline,
                                             const Layout& layout,
                                             std::span<const StretchAllocation> note_stretches);
```

### 3.5 `plan_gap_layout` (`core/timemap/`)

```cpp
/// Decide how one gap reaches its target output duration. Gaps are never
/// time-stretched; they are cut or extended so the noise keeps its natural texture.
///  Shorter: keep [start, start + kept_head) and [end − kept_tail, end), cut the middle.
///           kept_head covers the whole tail region (up to max_tail) when possible;
///           kept_tail covers params.preroll_guard plus any breath before the next note.
///           Breath regions are never cut unless the breath is muted and
///           params.cut_muted_breaths is true.
///  Longer:  keep the whole gap and insert room tone at the quietest point
///           (lowest floor frames, never inside tail or breath).
/// Returns the spans for this gap. Pure; thread-safe.
[[nodiscard]] Result<std::vector<TimeSpan>> plan_gap_layout(const Gap& gap,
                                                            Seconds target_duration,
                                                            const EffectiveGap& eff,
                                                            const GapLayoutParams& params);
```

The minimum length a gap can shrink to is `kept_head + kept_tail` (protected regions), or `min_gap` if those are empty.

### 3.6 `effective_gap` (`core/edits/`)

```cpp
/// Fold gap edits (and track-level strip-silence settings) into effective values.
/// Pure; thread-safe.
[[nodiscard]] EffectiveGap effective_gap(const Gap& gap, const EditList& edits);

struct EffectiveGap {
    SegmentId gap;
    Decibels gain{0.0};                      // applied to silence + noise regions (not tail, not breath)
    bool mute = false;                       // gain → −inf (digital silence)
    Decibels noise_gain{0.0};                // extra gain for noise regions (clicks, smacks)
    Seconds fade_out_of_note{0.010};         // ramp from full level after the tail
    Seconds fade_into_note{0.010};           // ramp back before preroll_guard
    std::optional<Seconds> target_duration;  // from tighten/remove; nullopt = layout decides
    InsertFill fill = InsertFill::room_tone; // room_tone | digital_silence
    bool is_edited = false;
};
```

### 3.7 `render_gap` (`core/synthesis/gap/`)

```cpp
/// Render one gap into output time.
///  warp spans   → original samples (slope is always 1 for gaps)
///  cut spans    → nothing; the kept parts on either side are joined with
///                 find_splice_offset + equal-power splice (note-stretching §5)
///  insert spans → room tone: noise shaped by NoiseFloor.band_spectrum at the local
///                 floor level, seeded by hash(gap id, analysis version);
///                 or zeros if fill == digital_silence
///  then gain/mute applied to silence and noise regions with fades at their edges.
/// Breath regions are skipped here (rendered by the breath lane) — see breath-lane.md.
/// If eff.is_edited is false and every span is a slope-1 warp: copies the source
/// exactly (bit-identical). Not RT-safe; runs on render threads.
[[nodiscard]] Result<AudioBuffer> render_gap(const Gap& gap, const EffectiveGap& eff,
                                             const TimeMap& map, const AudioSource& src,
                                             const NoiseFloor& floor,
                                             const GapRenderParams& params);
```

### 3.8 `StripSilence` macro (`macros/strip_silence/`)

```cpp
/// Write gap edits for every gap in the selection. One undo group.
///  Gate:    gain = params.gate_gain (or mute) on gaps; no duration edits.
///  Tighten: target_duration = min(current, params.max_gap) for gaps longer than max_gap.
///  Remove:  target_duration = minimum possible (§3.5).
/// Gaps shorter than params.ignore_shorter_than are left alone (phrasing pauses).
/// Optional: params.include_noise applies noise_gain to clicks/smacks.
class StripSilence final : public Macro {
public:
    [[nodiscard]] std::vector<Edit> run(const Selection&, const AnalysisResult&,
                                        const EditList&, const StripSilenceParams&) const;
};

struct StripSilenceParams {
    enum class Mode { gate, tighten, remove } mode = Mode::gate;
    Decibels gate_gain{-24.0};
    bool mute = false;
    Seconds max_gap{0.400};
    Seconds ignore_shorter_than{0.150};
    bool include_noise = true;
    Decibels noise_gain{-12.0};
};
```

### 3.9 `GapTool` (`tools/gap/`)

Drag in a gap: vertical = gain, horizontal edge drag = target duration (only inside a ripple scope), double-click = mute toggle. Writes `gap` edits. Anchors are placed with a separate gesture and written as `layout` edits.

---

## 4. Edit categories added

| Category | Target | Params | Written by |
|---|---|---|---|
| `gap` | `SegmentId` (gap) | `gain_db`, `mute`, `noise_gain_db`, `fade_out_of_note_s`, `fade_into_note_s`, `target_duration_s`, `fill` | GapTool, StripSilence |
| `layout` | track | `anchor_add`, `anchor_remove` | GapTool (anchor gesture), StripSilence (optional section anchors) |

`EditTarget` is generalised from note IDs to segment IDs (ADR-0008). Existing note edits are unaffected because a `NoteId` is a `SegmentId` of kind note.

---

## 5. Effect on audio

- **Gate mode:** only gap regions of kind silence and noise change level. Notes, tails and breaths are untouched. Output length unchanged.
- **Tighten/remove:** gap middles are cut and spliced; later segments in the scope move earlier; removed time appears as silence before the next anchor. Notes are never stretched by this unless a gap is fully used up.
- **Note moves:** the gap before a moved note is cut or extended with room tone. The neighbour note no longer changes.
- **Bit-identical guarantees:** with no gap or layout edits, every gap renders as a pure copy of the source. Notes and gaps not touched by an edit (and outside splice margins) are bit-identical to the source, shifted in output time only if a ripple moved them. A shifted segment's samples are still the original samples.
- **Room tone** is deterministic (seeded) so renders and caches are repeatable.

## 6. Split / join decisions

- **`SegmentTimeline` is a class** (partition invariant), `Gap` and `Segment` are plain structs (no invariants of their own) — coding-standards §3.
- **`TimeMap` data moves to `core/types`; building stays in `core/timemap`** — different layers need the type, only one module may construct it (split class rule 3).
- **`plan_gap_layout` separate from `render_gap`** — planning is testable without audio and feeds `build_time_map` (split function rule 4).
- **No separate `RoomToneGenerator` class** — it is one function in `core/dsp` (`shaped_noise(spectrum, level, seed, length)`), also used by note-stretching's `noise_regen`. Rule of three not reached for a class.
- **`StripSilence` is one macro with a mode enum, not three macros** — the three modes share all detection and only differ in which edit they write (keep-together rule 3). The mode is an `enum class`, not a bool.

## 7. Tests

**Unit**
- `estimate_noise_floor`: synthetic tone bursts over −60 dBFS pink noise → floor within ±2 dB; spectrum shape matches the pink noise slope.
- `build_segment_timeline`: segments tile the source exactly; legato notes have no gap; tails detected on a decaying tone with reverb; preroll guard extends a soft-onset note.
- `plan_gap_layout`: shortening never cuts tail or breath regions; minimum length = protected regions; lengthening inserts at the quietest point.
- `TimeMap` (property tests): invariants of §1.3 for random layouts; `to_output` returns nullopt exactly inside cut spans; `to_source` returns `GeneratedTime` exactly inside insert spans; round trip `to_source(to_output(s)) == s` for every non-cut `s`.
- `solve_layout`: moving a note earlier by less than its gap leaves both neighbours' durations unchanged; removed time collects before the next anchor; anchors never move.

**Contract**
- No edits → identity `TimeMap`, all gaps rendered bit-identical.
- Gate mode never changes output length.
- In tighten/remove, every note's rendered samples equal the source samples (shifted), outside splice margins.

**Audio scenarios** (`tests/audio/scenarios/gaps/`)
- `gate_minus24_vocal` — output length identical; note regions bit-identical; gap RMS drops by 24 ± 0.5 dB.
- `tighten_400ms_spoken` — gap lengths ≤ 400 ms; click detector on every cut; next-anchor material sample-aligned.
- `remove_all_acappella` — no tail truncated (decay continues to floor + 6 dB); no breath cut.
- `move_note_earlier_50ms` — neighbour note bit-identical; gap shorter by 50 ms.
- `move_note_later_200ms` — room tone inserted; spectrum within ±3 dB of the floor spectrum; no audible loop (listening).

**Listening focus:** chopped reverb tails, clipped soft consonants at phrase starts, "pumping" when gated gaps open, room tone that sounds different from the real floor, unnatural rhythm after tightening.

## 8. Open questions

1. **Default fill for inserted time:** room tone (more natural) or digital silence (cleaner for gated tracks). Proposal: room tone, unless the gap is gated or muted, then match the gated level.
2. **ARA region end:** when ripple pushes material past the host region end, do we warn only, or offer to extend the region (host-dependent)?
3. **Tail threshold:** 6 dB above floor and 1.5 s max are guesses; tune with the listening sheet.
4. **Anchors from the host:** should DAW markers or bar lines become anchors automatically (e.g. one per bar in tighten mode)?
5. **Multitrack (Phase 12):** should ripple be linked across tracks (doubles, harmonies) so they stay aligned?

## Review log

*(structure-reviewer findings go here)*
