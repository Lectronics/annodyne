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

    /// A map of warp spans only, linear between anchors. (0,0) is an implicit anchor when the first
    /// anchor is not at source 0; after the last anchor the map continues at slope 1 to
    /// `source_length` (no span if the last anchor is already at source_length, as with the builder's
    /// fixed end anchor). Errors: invalid_argument if anchors are empty, not finite, not strictly
    /// increasing on both axes, or the last anchor's source exceeds source_length.
    [[nodiscard]] static Result<TimeMap> from_anchors(std::vector<TimePair> anchors, Seconds source_length);

    /// Right-continuous: at a span boundary the span with positive SOURCE length that starts there
    /// answers (a warp: its output_start; a cut: empty). s == source_length() maps to
    /// output_length(). An insert has no source, so it never answers. Pre (asserted in debug,
    /// clamped in release): 0 <= s <= source_length().
    [[nodiscard]] std::optional<OutputTime> to_output(SourceTime s) const noexcept;

    /// Left-continuous variant for the END of a source range: at a boundary the span with positive
    /// source length that ENDS there answers (a warp: its output_end; a cut: empty). Equal to
    /// to_output elsewhere. Used for note ends. Pre: 0 < s <= source_length().
    [[nodiscard]] std::optional<OutputTime> to_output_end(SourceTime s) const noexcept;

    /// Right-continuous. At the start of an insert: GeneratedTime{offset 0}; at its end: the
    /// following span's answer (so to_source(to_output(s)) == s holds for non-cut s). Cuts have zero
    /// output length and never answer. tau == output_length(): the end of the last span with output
    /// length (a warp: its source_end; an insert: GeneratedTime{offset == its length}). The empty
    /// map answers SourceTime{0} at tau 0. Pre: 0 <= tau <= output_length().
    [[nodiscard]] SourceLookup to_source(OutputTime tau) const noexcept;

    /// dtau/ds at tau (right-continuous): the warp's slope; 0 inside an insert span (a sentinel:
    /// never divide by it). Same precondition as to_source.
    [[nodiscard]] Ratio slope_at(OutputTime tau) const noexcept;

    [[nodiscard]] std::span<const TimeSpan> spans() const noexcept;
    [[nodiscard]] Seconds source_length() const noexcept;
    [[nodiscard]] Seconds output_length() const noexcept;
    /// Structural: the empty map, or exactly one warp with output_start == source_start == 0 and
    /// output_end == source_end (exact comparison of the stored doubles). Then to_output(s) == s.
    [[nodiscard]] bool is_identity() const noexcept;
};
```

- Time axes: `to_output` takes `SourceTime`, `to_source` takes `OutputTime`; lengths are `Seconds`. Raw seconds only inside `core/types` (`unchecked_*`).
- Ownership: value type, immutable; shared by const reference.
- Real-time safe: all queries (binary search over spans; no allocation). `make`/`from_anchors` allocate.
- **Slope 1.** A warp has slope 1 when `output_end - output_start == source_end - source_start` within 1e-9 s (exact for the builder's data); `read_through_map` applies the sample-level test of §3.
- **Domain.** Finite. Queries outside it are a precondition violation (asserted in debug, clamped in release), not an error value, so the query return types stay `optional` / `variant`.
- **`stretch_ratio(SourceTime)` and `anchors()` are removed** (callers: tests only). `slope_at` and `spans()` replace them.
- **Invariants** (contract tests): spans tile both axes exactly; both axes monotonic; `to_output` empty exactly inside cuts; `to_source` generated exactly inside inserts; `to_source(*to_output(s)) == s` within 1e-9 s for non-cut `s`; `identity(L)` is exact.

## 2. `SegmentId` and `EditTarget` (`core/types/ids.h`, `edit.h`)

- `enum class SegmentKind { note, gap }` and `struct SegmentId { SegmentKind kind; std::string value; }` (ordered, comparable). The kind is part of the identity, so a gap id can never equal a note id with the same text. `SegmentId segment_id(const NoteId&)` gives `{note, id.value}`; `std::optional<NoteId> as_note_id(const SegmentId&)` is the inverse (empty for gaps). `NoteId` stays its own type (the edit-log text of a note id is unchanged). `BreathId` is added in Phase 5 with `BreathEvent`.
- `EditTarget` fields `note_ids` / `orphaned_note_ids` become `segment_ids` / `orphaned_segment_ids` (`std::vector<SegmentId>`). The fold and `remap` use `as_note_id`; gap targets are ignored by the note fold and pass through `remap` unchanged (a `SegmentIdMap` for gaps arrives with gap analysis in Phase 5). `NoteIdMap` is unchanged. `EditList::reset(span<const NoteId>)` converts with `segment_id`; `retargeted`/`remapped_ids` map note-kind targets through `NoteIdMap`. Scenario JSON keeps `note_id` / `note_index`; the parser converts.
- No project or edit-log serialisation exists yet (Phase 7), so renaming the fields breaks no stored data.
- `EditCategory` gains `gap`, `layout`, `breath`, `breath_chain`, `breath_assignment`. They have no `EditParam`s yet: Like `separation`, `assignment` and `sound` they have no `EditParam` yet, so `validate` and `validate_for_add` already refuse any set/add/scale on them (`invalid_argument`: "only a reset may omit the parameter"); only a parameterless reset can be recorded, and the fold ignores it. `to_string`/`parse_edit_category` know them. No new switch is needed: nothing switches over `EditCategory`.

## 3. Reading source through a map (`core/synthesis/engine/source_through_map.h`) — new

```cpp
/// Output samples [begin, begin+count) of `source` as played through `map`.
///  slope-1 warp  -> source samples, shifted by a whole number of samples (copied bit for bit)
///  insert        -> zeros (digital silence)
///  cut           -> contributes nothing (it has no output)
/// Sample rate is the source's. Output span boundaries are snapped to samples with Rounding::nearest;
/// a warp's shift d = round((output_start - source_start) * rate) must be a whole number of samples
/// (within 1e-6 sample) and its output length must equal its source length (within 1e-9 s): output
/// sample i reads source sample i - d, and that is always inside the warp's source range. A warp
/// that is not sample-aligned slope 1 (a fractional shift or a stretch) is not_implemented; Phase 5's
/// layout solver must produce whole-sample shifts.
/// Errors: not_implemented (as above); out_of_range (range outside [0, output frames of the map at
/// the source's rate]); invalid_argument (map.source_length() differs from the source duration by
/// more than half a sample).
/// Pure; not real-time safe (allocates the buffer).
[[nodiscard]] Result<AudioBuffer> read_through_map(const AudioSource& source, const TimeMap& map,
                                                   Samples begin, Samples count);
```

One function used by `PassthroughEngine`, the test gain engine and the renderer's "pristine" slice, so all three agree on what "the original" is when time has moved. It lives in `core/synthesis/engine` (layer 2) because it is an AudioSource-plus-TimeMap policy, not signal maths (so `core/dsp` stays pure), and `render` (layer 4) may link `synthesis`. No rule change.

## 4. Engines (`SynthesisEngine::render_note`)

Signature unchanged (`source, note, eff, const TimeMap&, OutputRange`). `OutputRange` is now relative to the map's output axis. Contract amended: the engine renders the output range; for a slope-1 map region it must use the same samples `read_through_map` gives. `PassthroughEngine` returns `read_through_map` for the range (`not_implemented` only for a warp that is not sample-aligned slope 1). The test gain engine does the same, then applies its gain. Neither rejects a non-identity map any more.

## 5. `render_region` (`core/render`)

- `RenderInputs` gains `std::shared_ptr<const TimeMap> time_map` (null = build from edits). If set it is authoritative and wins over any timing edits in the edit list: the engine receives that map, and `effective_state`'s timing fields (`onset_shift` etc.) are not applied a second time (the engine never realises timing; the map is the timing). Phase 5's `build_time_map` will produce it. If null it is built from the edits with `build_time_map` with the recording's exact length and must be the identity, else `not_implemented` as before (stretching is Phase 6). `render_region` always passes the recording's duration as the map's `source_length` (never the "largest note" fallback of §6).
- **Validation up front** (`invalid_argument`): a supplied map's `source_length` must match the recording's duration within half a sample.
- Frame counts in the plan are **output** frames: `round(map.output_length * rate)`; `snap_region` checks against them. The zone clamp, region and ramps are all output samples. A note's span for the blend plan is `to_output(note.source_start)` .. `to_output_end(note.source_end)`, snapped; a note whose start or end maps to a cut: `invalid_argument` (Phase 5 never produces one: cuts are inside gaps). `render_zone` passes the actual map to the engine.
- The pristine slice and each zone's reference are `read_through_map`. A warp that is not sample-aligned slope 1 anywhere in the region (or a zone's margin): `not_implemented`.
- `dirty_regions` is not consumed by the renderer (ADR-0006 item 2); it stays source-axis and is revisited with the layout solver in Phase 5, so nothing breaks now.
- `RenderResult.source_range` is **removed** (no single source range exists once there are cuts and inserts). Its two users are tests. `buffer.start` (output time) stays.
- The cache key (`make_render_key`) includes the **whole** map unless it is the identity: `source_length`, `output_length`, and every span as `(kind, source_start, source_end, output_start, output_end)` (same length-prefixed writer). Whole-map hashing cannot miss a span a zone reads. The Phase 6 TODO ("include anchors") is thereby done early.
- Guarantees: with an identity map nothing changes (null and undo-null tests). With a map containing a cut and an insert: output length = `output_length`; insert samples are digital silence; every other sample equals the source sample the map names, bit for bit, outside blend zones of edited notes; an empty edit list gives exactly `read_through_map` of the whole range.

## 6. `build_time_map`

Emits spans via `from_anchors(anchors, source_length)`. Behaviour is unchanged in this addendum: it stays **Flex-style until Phase 5** (ADR-0005 §16 as written). ADR-0008 decision 4 is the target behaviour from Phase 5: `onset_shift`, `length_scale` and `attack_time_scale` follow shift-first with an auto handle at the next transient when a gap is used up; only an explicit `time_handle` is an absolute output anchor. The domain length is `params.source_end` (always the recording's duration when called from `render_region`); if that is the "unknown" origin value, the domain ends at the largest source time among the notes and anchors, which is for map-only use in tests and must not be used to render. The builder's fixed end anchor `(source_end, source_end)` is accepted by `from_anchors` (§1). `TimeMapBuild::map` is the only API change.

## 7. Layer and CMake

No new target or dependency edge: `core/types` stays data-only (`make` validates data). `read_through_map` is in `core/synthesis/engine`, which already links `core/types`; `core/synthesis/passthrough` links `engine`; `core/render` already links `synthesis`. No change to `AnnoLayers.cmake`.

## 8. Tests

Contract (property-based, seeded): tiling and both-axis monotonicity for random valid span lists; `to_output` empty exactly inside cuts; `to_source` generated exactly inside inserts; round trip for non-cut times; `identity(L)` exact; `make` rejects each invariant violation; **boundary points tested explicitly** (every span start and end, `s == L`, `tau == output_length`, the empty map, a cut at the start and at the end of the domain, a leading insert, an adjacent cut+insert at one position, `to_output_end` at every boundary). Unit: `from_anchors` agrees with the Phase 1 builder's maps (fixed-end and unknown-end cases from `build_time_map_test`; the old "slope 1 before the first anchor" case becomes the implicit (0,0) anchor); `SegmentId` conversion and kind separation; new categories parse/print and are rejected by `validate_for_add`; `read_through_map` (shifted copy bit-exact, insert zeros, cut nothing, slope ≠ 1 rejected, sample snapping); render: **gate test** (map with a cut and an insert: length, silence in the insert, source samples elsewhere, bit-identical outside spliced regions; with and without an edited note inside a shifted warp; a supplied map wins over timing edits; a fractional-sample shift is `not_implemented`; a map of the wrong length is `invalid_argument`); cache key differs for different maps; null and undo-null unchanged.

## 9. Split / join decisions

- `read_through_map` is a free function in `core/synthesis/engine`, not in `core/dsp` (policy, not signal maths), not a method of `TimeMap` (`types` is data only) and not duplicated in each engine (three callers would drift).
- `TimeSpan` is a plain struct with a `kind` tag, not three types (`std::variant`): every consumer switches on kind anyway and all three share the tiling invariant.
- `from_anchors` stays as a thin constructor rather than making `build_time_map` write spans: Phase 1's builder is anchor-shaped, and Phase 5 replaces it.

## Open questions

1. None blocking. Sample alignment of shifts is delegated to Phase 5's layout solver (§3).

## Review log

*(structure-reviewer findings go here)*
