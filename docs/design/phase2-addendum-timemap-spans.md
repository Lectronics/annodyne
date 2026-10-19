# Design note: Phase 2 addendum — TimeMap spans, SegmentId, render walks spans

- **Phase:** 2 addendum (branch `phase-2-addendum`; the `phase-2` tag is not moved)
- **Status:** draft
- **Spec sections:** architecture §2, §3, §5.1–5.2, §5.4, §4.6; ADR-0008 (this change), ADR-0005 §11/§16 (amended), ADR-0006 (amended)
- **Folders / targets:** `src/core/types` (layer 0: `TimeMap`, `SegmentId`, edit categories), `src/core/dsp` (layer 0: `read_through_map`), `src/core/synthesis/{engine,passthrough}` (layer 2), `src/core/timemap` (layer 3: `build_time_map` emits spans), `src/core/render` (layer 4), `src/core/edits`, tests.
- **Not in scope (their phases):** `SegmentTimeline`, noise floor, gap detection, `solve_layout`, `plan_gap_layout`, gap/breath rendering, dynamics, macros, UI; `AnalysisResult` fields `timeline`, `noise_floor`, `breaths`; stretching a warp (slope ≠ 1, Phase 6).

## Purpose

Make the time model able to say "this source was removed" and "this output has no source" (ADR-0008), without implementing any feature that needs it. The renderer is taught to walk spans so that, when Phase 5 builds maps with cuts and inserts, rendering already handles them and is tested with hand-built maps.

## 1. `TimeMap` (`core/types/time_map.h`) — rewrite

```cpp
enum class SpanKind { warp, cut, insert };

/// One piece of the map. Both ranges are always present; the one a kind does not use has zero length
/// and sits at the boundary position.
///  warp:   source range and output range both have positive length; slope = out/src (1 = untouched)
///  cut:    source range positive, output range zero length (source removed)
///  insert: source range zero length, output range positive (output with no source)
struct TimeSpan {
    SpanKind kind;
    SourceTime source_start, source_end;
    OutputTime output_start, output_end;
    std::optional<SegmentId> segment;   // the segment that produced it; empty for maps built without a timeline
};

struct GeneratedTime {          // a position inside an insert span
    std::optional<SegmentId> segment;
    Seconds offset;             // from the start of the insert span; 0 <= offset <= length
};
using SourceLookup = std::variant<SourceTime, GeneratedTime>;

class TimeMap {
public:
    /// Source [0, L) played back unchanged: one slope-1 warp. identity(0) is the empty map.
    [[nodiscard]] static TimeMap identity(Seconds source_length);

    /// Errors: invalid_argument if a time is not finite or a span violates its kind's shape; the
    /// first span does not start at (0, 0); spans are not contiguous on BOTH axes (each start equals
    /// the previous end exactly); a span has negative length. Empty `spans` is the empty map.
    /// Whether cut/insert lie inside gap segments cannot be checked here (no timeline); build_time_map
    /// and solve_layout check it (Phase 5). Not real-time safe.
    [[nodiscard]] static Result<TimeMap> make(std::vector<TimeSpan> spans);

    /// A map of warp spans only: slope-1 before the first anchor and after the last, linear between
    /// (the old anchor form). anchors strictly increasing on both axes, first anchor output >= 0
    /// after extending back to source 0, last anchor source < source_length. Errors: invalid_argument.
    [[nodiscard]] static Result<TimeMap> from_anchors(std::vector<TimePair> anchors, Seconds source_length);

    /// Pre (asserted in debug, clamped in release): 0 <= s <= source_length().
    /// Empty inside a cut. s == source_length() maps to output_length().
    [[nodiscard]] std::optional<OutputTime> to_output(SourceTime s) const noexcept;

    /// Pre: 0 <= tau <= output_length(). GeneratedTime inside an insert span. tau == output_length()
    /// maps to the end of the last span that has output length.
    [[nodiscard]] SourceLookup to_source(OutputTime tau) const noexcept;

    /// dtau/ds at tau: the warp's slope; 0 inside an insert span. Same precondition as to_source.
    [[nodiscard]] Ratio slope_at(OutputTime tau) const noexcept;

    [[nodiscard]] std::span<const TimeSpan> spans() const noexcept;
    [[nodiscard]] Seconds source_length() const noexcept;
    [[nodiscard]] Seconds output_length() const noexcept;
    /// One slope-1 warp from (0,0): to_output(s) == s exactly. True for the empty map.
    [[nodiscard]] bool is_identity() const noexcept;
};
```

- Time axes: `to_output` takes `SourceTime`, `to_source` takes `OutputTime`; lengths are `Seconds`. Raw seconds only inside `core/types` (`unchecked_*`).
- Ownership: value type, immutable; shared by const reference.
- Real-time safe: all queries (binary search over spans; no allocation). `make`/`from_anchors` allocate.
- **Domain.** Finite. Queries outside it are a precondition violation (asserted in debug, clamped in release), not an error value, so the query return types stay `optional` / `variant`. This amends the wording of ADR-0008 decision 2.
- **`stretch_ratio(SourceTime)` and `anchors()` are removed** (callers: tests only). `slope_at` and `spans()` replace them.
- **Invariants** (contract tests): spans tile both axes exactly; both axes monotonic; `to_output` empty exactly inside cuts; `to_source` generated exactly inside inserts; `to_source(*to_output(s)) == s` within 1e-9 s for non-cut `s`; `identity(L)` is exact.

## 2. `SegmentId` and `EditTarget` (`core/types/ids.h`, `edit.h`)

- `struct SegmentId { std::string value; }`, same shape as `NoteId` (ordered, comparable). `SegmentId segment_id(const NoteId&)` converts explicitly (a note's segment id has the same text); `NoteId` stays its own type. `BreathId` is added in Phase 5 with `BreathEvent`.
- `EditTarget` fields `note_ids` / `orphaned_note_ids` become `segment_ids` / `orphaned_segment_ids` (`std::vector<SegmentId>`). The fold and `remap` look notes up by `NoteId`; they compare against the segment id of the note. `NoteIdMap` is unchanged (re-analysis lineage of notes). Scenario JSON keeps `note_id` / `note_index` in its `target` (a scenario names notes); the parser converts.
- `EditCategory` gains `gap`, `layout`, `breath`, `breath_chain`, `breath_assignment`. They have no `EditParam`s yet: `validate_for_add` rejects them with `not_implemented` ("Phase 5/6"), `to_string`/`parse_edit_category` know them, and the exhaustive switches handle them without behaviour. `effective_state` ignores them.

## 3. Reading source through a map (`core/dsp/map_read.h`) — new

```cpp
/// Output samples [begin, begin+count) of `source` as played through `map`.
///  slope-1 warp  -> source samples, shifted (copied bit for bit)
///  insert        -> zeros (digital silence)
///  cut           -> contributes nothing (it has no output)
/// Span boundaries are snapped to whole samples (Rounding::nearest) on both axes. A warp whose
/// snapped source and output lengths differ by more than one sample has slope != 1: not_implemented.
/// Errors: not_implemented (slope != 1); out_of_range (range outside [0, output frames], or a warp
/// reads outside the source); invalid_argument (sample-rate/length mismatch between map and source).
/// Pure; not real-time safe (allocates the buffer).
[[nodiscard]] Result<AudioBuffer> read_through_map(const AudioSource& source, const TimeMap& map,
                                                   Samples begin, Samples count);
```

One function used by `PassthroughEngine`, the test gain engine and the renderer's "pristine" slice, so all three agree on what "the original" is when time has moved. It lives in `core/dsp` (layer 0; links `types` only) because both `synthesis` (layer 2) and `render` (layer 4) need it and it is a sample-level utility. Phase 5's layout solver is responsible for sample-aligned shifts; a one-sample rounding difference is absorbed by reading `output length` samples from the snapped source start.

## 4. Engines (`SynthesisEngine::render_note`)

Signature unchanged (`source, note, eff, const TimeMap&, OutputRange`). `OutputRange` is now relative to the map's output axis. Contract amended: the engine renders the output range; for a slope-1 map region it must use the same samples `read_through_map` gives. `PassthroughEngine` returns `read_through_map` for the range (`not_implemented` only for slope ≠ 1). The test gain engine does the same, then applies its gain. Neither rejects a non-identity map any more.

## 5. `render_region` (`core/render`)

- `RenderInputs` gains `std::optional<TimeMap> time_map`. If set, it is authoritative (the caller guarantees it matches the edits; Phase 5's `build_time_map` will produce it). If empty it is built from the edits with `build_time_map` and must be the identity, else `not_implemented` as before (stretching is Phase 6).
- Frame counts in the plan are **output** frames: `round(map.output_length * rate)`; `snap_region` checks against them. The zone clamp, region and ramps are all output samples. Note spans for the blend plan are `to_output(note.source_start)`..`to_output(note.source_end)` snapped; both exist because notes lie inside warps (a note whose start or end maps to a cut: `invalid_argument`).
- The pristine slice and each zone's reference are `read_through_map`. A slope ≠ 1 warp anywhere in the region: `not_implemented`.
- `RenderResult.source_range` is **removed** (no single source range exists once there are cuts and inserts). Its two users are tests. `buffer.start` (output time) stays.
- The cache key (`make_render_key`) includes the map: a canonical list of `(kind, source_start, source_end, output_start, output_end)` for spans meeting the region (same length-prefixed writer), whenever the map is not the identity. The Phase 6 TODO ("include anchors") is thereby done early.
- Guarantees: with an identity map nothing changes (null and undo-null tests). With a map containing a cut and an insert: output length = `output_length`; insert samples are digital silence; every other sample equals the source sample the map names, bit for bit, outside blend zones of edited notes; an empty edit list gives exactly `read_through_map` of the whole range.

## 6. `build_time_map`

Emits spans via `from_anchors(anchors, source_length)`. Behaviour (Flex-style, ADR-0005 §16 as amended) is unchanged in this addendum: the shift-first / auto-handle-at-next-transient rule (ADR-0008 decision 4) arrives with the layout solver in Phase 5. The domain length is `params.source_end`; if that is the "unknown" origin value it is the largest source time among the notes and anchors. `TimeMapBuild::map` is the only API change.

## 7. Layer and CMake

No new target or dependency edge: `core/types` stays data-only (`make` validates data); `core/dsp` links `core/types` already (layer 0 → 0 is not allowed for other `core/types` dependents? `dsp` links `types` today for `FadeShape`; unchanged). `core/synthesis/*` and `core/render` already link `core/dsp`/`types`. No change to `AnnoLayers.cmake`.

## 8. Tests

Contract (property-based, seeded): tiling and both-axis monotonicity for random valid span lists; `to_output` empty exactly inside cuts; `to_source` generated exactly inside inserts; round trip for non-cut times; `identity(L)` exact; `make` rejects each invariant violation. Unit: `from_anchors` agrees with the old anchor behaviour (ported tests); `SegmentId` conversion; new categories parse/print and are rejected by `validate_for_add`; `read_through_map` (shifted copy bit-exact, insert zeros, cut nothing, slope ≠ 1 rejected, sample snapping); render: **gate test** (map with a cut and an insert: length, silence in the insert, source samples elsewhere, bit-identical outside spliced regions; with and without an edited note inside a shifted warp); cache key differs for different maps; null and undo-null unchanged.

## 9. Split / join decisions

- `read_through_map` is a free function in `core/dsp`, not a method of `TimeMap` (`types` is data only) and not duplicated in each engine (three callers would drift).
- `TimeSpan` is a plain struct with a `kind` tag, not three types (`std::variant`): every consumer switches on kind anyway and all three share the tiling invariant.
- `from_anchors` stays as a thin constructor rather than making `build_time_map` write spans: Phase 1's builder is anchor-shaped, and Phase 5 replaces it.

## Open questions

1. None blocking. Sample alignment of shifts is delegated to Phase 5's layout solver (§3).

## Review log

*(structure-reviewer findings go here)*
