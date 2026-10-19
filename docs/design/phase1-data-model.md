# Design note: Phase 1 — data model and edit contracts

- **Phase:** 1
- **Status:** draft
- **Spec sections:** architecture spec §3 (types), §4.4 (edits), §5.1–5.4, §5.7; ROADMAP Phase 1;
  ADR-0002, ADR-0003, ADR-0005 (proposed with this note)
- **Folder / CMake target:**
  - `src/core/types` (layer 0): new headers, existing target `anno::core_types`. This now includes
    the pure `TimeMap` mapping class (synthesis, layer 2, must be able to use it; review B2).
  - `src/core/edits` (layer 2): new target `anno::core_edits`.
  - `src/core/timemap` (layer 3): new target `anno::core_timemap`; holds `build_time_map`, the one
    function that reads edits to construct a `TimeMap`.

## Purpose
Build the non-destructive model and prove it correct before any audio code depends on it. Phase 1
adds:
- the analysis data types (`Note`, tracks, `AnalysisResult`);
- the edit vocabulary (`EditSpec`, `Edit`, `EditList`);
- the fold from edits to values (`effective_state`);
- the time axis link (`TimeMap`);
- change detection (`dirty_regions`);
- re-attaching edits after re-analysis (`remap_after_reanalysis`).

No DSP, no audio samples, no file I/O. Nothing here reads or writes `AudioBuffer`.

All the spec gaps this note had to fill are collected in §"Decisions that go beyond the spec". They
are recorded in ADR-0005 with status **proposed**. No human was available when this note was
written; the review (B1) asked for acceptance before headers. The decisions were therefore made as
conservatively as possible (closest to the ROADMAP wording; see "Open questions") and a human
confirms or changes them at structure audit #1. Rework cost if they change is small and local.

## Public types

### Layer 0: `core/types`

Plain data unless marked class. All value types; analysis objects are shared as
`std::shared_ptr<const T>` and never mutated (architecture.md).

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `NoteId`, `TrackId` | struct (string wrapper, like `SourceId`) | — (non-empty is checked by validators) | value | value |
| `EditSeq`, `GroupId` | struct (`uint64_t` wrapper) | — | value | value |
| `TimePair` | struct (`SourceTime source`, `OutputTime output`) | — | value | value |
| `TimeMap` | **class** | ≥ 1 anchor; anchors strictly increasing in both axes; slope 1 outside; `is_identity()` ⇒ `to_output(s) == s` exactly | value | `shared_ptr<const TimeMap>` |
| `FrameGrid` | struct | checked by `validate` | value | value |
| `FrameClass`, `OnsetKind`, `SeparationIn`, `Algorithm`, `FadeShape`, `EditCategory`, `EditParam`, `EditOp` | `enum class` | closed sets | — | — |
| `FrameMatrix` | struct (`rows`, `cols`, `std::vector<float>`) | `data.size() == rows*cols`, checked by `validate` | value | value |
| `PitchTrack`, `OnsetList`, `HarmonicTrack`, `NoiseTrack`, `EnvelopeTrack` | struct | sizes checked by `validate` | value | inside `Note` / `AnalysisResult` |
| `Note` | struct | `validate(Note)` | `AnalysisResult` | `shared_ptr<const AnalysisResult>` |
| `AnalysisResult` | struct | `validate(AnalysisResult)`: notes sorted by `source_start`, unique ids, valid notes | creator | `shared_ptr<const AnalysisResult>` |
| `EditTarget` | struct | a recorded edit has `note_ids` or `orphaned_note_ids` non-empty; `add` accepts only specs with non-empty `note_ids` and empty `orphaned_note_ids` | value | value |
| `EditValue` | `std::variant<std::monostate, Cents, Ratio, Decibels, Seconds, FadeShape, TimePair>` | must match the `ValueKind` of the (param, op) pair | value | value |
| `ParamInfo`, `ValueKind` | struct, `enum class` | constexpr table, see below | static | — |
| `NoteIdMap` | `using` = `std::map<NoteId, std::vector<NoteId>>` | — | value | value; lives in `core/types/ids.h` |
| `EditSpec` | struct | `validate(EditSpec)` | tool or macro | value |
| `Edit` | struct (`EditSeq seq`, `GroupId group`, `WallTime created_at`, `EditSpec spec`) | by convention only `EditList` creates these (not enforced by the type; tools return `EditSpec`) | `EditList` | value |
| `EffectiveNote` | struct | produced by `effective_state` | value | value |

`WallTime` is `std::chrono::system_clock::time_point`. It is only history metadata and never
influences the fold.

#### Analysis types (spec §3)

```cpp
struct FrameGrid {
    Seconds hop;            // > 0
    SourceTime start;       // source time of frame 0
    std::int64_t n_frames;  // >= 0
};

struct FrameMatrix {        // row-major: frames × bins
    std::int64_t rows = 0, cols = 0;
    std::vector<float> data;
};

struct PitchTrack {
    FrameGrid grid;
    std::vector<Cents> f0;                    // absolute, A4 = 6900; NaN when unvoiced
    std::vector<double> confidence_prob;      // 0..1
    std::vector<double> voicing_prob;         // 0..1
    std::vector<std::vector<Cents>> alternates;
};
struct OnsetList {
    std::vector<SourceTime> times;
    std::vector<double> strength;             // detector units, >= 0
    std::vector<OnsetKind> kind;
};
struct HarmonicTrack { FrameGrid grid; FrameMatrix freq_hz; FrameMatrix amp_linear; FrameMatrix phase_rad; };
struct NoiseTrack    { FrameGrid grid; FrameMatrix band_energy_linear; std::vector<FrameClass> frame_class; };
struct EnvelopeTrack { FrameGrid grid; FrameMatrix log_mag_db; std::vector<Hz> freqs; };
// FrameClass { voiced, sibilant, breath, plosive, silence } — 5 values (spec §3).

struct Note {
    NoteId id;  TrackId track_id;  int voice;
    SourceTime source_start, attack_end, release_start, source_end;
    Cents perceived_pitch, pitch_center;      // absolute
    FrameGrid grid;                           // grid of pitch_drift / pitch_mod / amp
    std::vector<Cents> pitch_drift, pitch_mod;      // relative to pitch_center
    std::vector<Decibels> amp;                      // dBFS
    LoudnessUnits loudness;                         // LUFS
    HarmonicTrack harmonics; NoiseTrack noise; EnvelopeTrack envelope;
    SeparationIn separation_in;
    double confidence_prob;
    bool is_active;
};

struct AnalysisResult {
    std::uint32_t version;  SourceId source_id;  Algorithm algorithm;
    std::optional<PitchTrack> pitch;  OnsetList onsets;  std::vector<Note> notes;
    std::optional<NoiseTrack> residual;  Hz tuning_ref;
};
```

Not in Phase 1 (additive later, no change to anything above): `BeatGrid`, `TempoCurve`,
`KeyEstimate`, `ChordTrack` (Phase 8 defines them and adds the four `std::optional` members to
`AnalysisResult`).

`validate` functions (`Result<void>`, `invalid_argument`) check: `source_start <= attack_end <=
release_start <= source_end`; curve lengths equal `grid.n_frames`; the four grids of a `Note`
(`grid`, `harmonics.grid`, `noise.grid`, `envelope.grid`) are equal; matrix shapes; probabilities in
0..1; sorted note order and unique ids in `AnalysisResult`.

#### Edit types (spec §5.1–5.3)

```cpp
enum class EditCategory { pitch, pitch_mod, pitch_drift, transition, formant, amplitude, fade,
                          sibilant, time, attack, separation, assignment, sound };
enum class EditOp { set, add, scale, reset };
enum class EditParam {
    center_offset,                                      // pitch        Cents
    mod_scale,                                          // pitch_mod    Ratio
    drift_scale,                                        // pitch_drift  Ratio
    transition_in_scale, transition_out_scale,          // transition   Ratio
    formant_offset,                                     // formant      Cents
    gain, within_note_flatten,                          // amplitude    Decibels, Ratio (0..1)
    fade_in, fade_out, fade_shape,                      // fade         Seconds, Seconds, FadeShape
    noise_gain_voiced, noise_gain_sibilant,
    noise_gain_breath, noise_gain_plosive,              // sibilant     Decibels
    onset_shift, length_scale, time_handle,             // time         Seconds, Ratio (> 0), TimePair
    attack_time_scale,                                  // attack       Ratio (> 0)
};
```

`separation`, `assignment` and `sound` have no parameters yet. They exist as categories so a reset
can name them and the enum matches spec §5.1. Their parameters arrive with Phases 5, 7 and 12
(additive).

A constexpr table `param_info(EditParam)` gives the category, `ValueKind`, allowed ops, neutral value
and legal range of each parameter. `validate(EditSpec)` and the fold both read it, so there is one
source of truth. The value type is the parameter's kind for every op (an offset's `add` takes the
same unit as its `set`; a multiplier's `scale` takes a `Ratio`), which keeps (param, op) → type
trivial (review B5).

| Parameter kind | Allowed ops | Neutral |
|---|---|---|
| offset (`Cents`, `Decibels`, `Seconds`) | `set`, `add`, `reset` | 0 |
| multiplier (`Ratio`) | `set`, `scale`, `reset` | 1 |
| `fade_shape` | `set`, `reset` | `linear` |
| `time_handle` | `set`, `reset` | no handles |
| (any category, `param` empty) | `reset` only: clears the whole category | |

`time_handle` is a keyed collection. `set` with value `TimePair` replaces the handle whose `source`
is within `k_handle_key` (1 µs) of the new one, or inserts it. `reset` with a `TimePair` value
removes that one handle; `reset` with `monostate` clears all handles (and, with `param` empty, the
whole category).

`noise_gain_*` has four parameters although `FrameClass` has five values: `silence` has no audio to
gain, so `noise_gain(const EffectiveNote&, FrameClass)` returns 0 dB for it.

```cpp
struct EditTarget {
    std::vector<NoteId> note_ids;           // live targets
    std::vector<NoteId> orphaned_note_ids;  // targets that re-analysis could not re-attach
};
struct EditSpec {                           // what a tool or macro returns (a group is a vector)
    std::string origin;                     // "main_tool", "correct_pitch", ...
    EditTarget target;
    EditCategory category;
    std::optional<EditParam> param;         // empty only for op == reset
    EditOp op;
    EditValue value;                        // monostate for reset
};
struct Edit { EditSeq seq; GroupId group; WallTime created_at; EditSpec spec; };

struct EffectiveNote {
    NoteId note_id;
    Cents center;                            // analysed pitch_center + folded center_offset
    Ratio mod_scale, drift_scale, transition_in_scale, transition_out_scale;
    Cents formant_offset;
    Decibels gain;  Ratio within_note_flatten;
    Seconds fade_in, fade_out;  FadeShape fade_shape;   // lengths clamped >= 0
    NoiseGains noise_gain;                   // {voiced, sibilant, breath, plosive}
    Seconds onset_shift;  Ratio length_scale;  std::vector<TimePair> handles;  // sorted by source
    Ratio attack_time_scale;
    bool is_edited;                          // any field differs from neutral
    friend bool operator==(const EffectiveNote&, const EffectiveNote&) = default;
};
```

### Layer 2: `core/edits`

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `EditList` | **class** | `cursor_ <= edits_.size()`; `seq` strictly increasing within `edits_`; `next_seq_` greater than every seq in `edits_` and `discarded_`; every edit passed `validate`; each group is a contiguous run | value | value (copy), `shared_ptr<const EditList>` for snapshots |
| `DirtyParams`, `MatchParams` | struct | — | caller | value |
| `NoteMatching`, `RemapEntry`, `RemapOutcome`, `RemapResult` | struct / enum | — | value | value |

```cpp
class EditList {
public:
    explicit EditList(TrackId track);
    [[nodiscard]] static Result<EditList> from_parts(EditListParts parts);   // persistence, tests

    [[nodiscard]] const TrackId& track_id() const noexcept;
    [[nodiscard]] std::span<const Edit> active() const noexcept;     // [0, cursor): what is folded
    [[nodiscard]] std::span<const Edit> redo_tail() const noexcept;  // [cursor, end): redoable
    [[nodiscard]] std::span<const Edit> discarded() const noexcept;  // superseded branches, kept
    [[nodiscard]] std::size_t cursor() const noexcept;
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;

    [[nodiscard]] Result<EditList> add(std::span<const EditSpec> group, WallTime created_at) const;
    [[nodiscard]] Result<EditList> reset(std::span<const NoteId>, std::span<const EditCategory>,
                                         WallTime created_at) const;
    [[nodiscard]] Result<EditList> undo() const;   // moves the cursor back one whole group
    [[nodiscard]] Result<EditList> redo() const;
    [[nodiscard]] EditList retargeted(const NoteIdMap& mapping) const;
};
```

### Layer 3: `core/timemap`

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `Anchor` | struct (`SourceTime s`, `OutputTime tau`) | — | value | value |
| `TimeMap` | **class** | ≥ 1 anchor; anchors strictly increasing in both `s` and `tau`; slope 1 outside the anchor range; consecutive anchors are at least `min_segment` apart in both axes | value | `shared_ptr<const TimeMap>` |
| `TimeMapParams` | struct | — | caller | value |

## Public functions

```cpp
// core/types
[[nodiscard]] Result<void> validate(const FrameGrid&);
[[nodiscard]] Result<void> validate(const Note&);
[[nodiscard]] Result<void> validate(const AnalysisResult&);
[[nodiscard]] Result<void> validate(const EditSpec&);
[[nodiscard]] const ParamInfo& param_info(EditParam) noexcept;
[[nodiscard]] const Note* find_note(const AnalysisResult&, const NoteId&) noexcept;  // null if absent

// core/edits
/// The fold: every active edit that targets `note`, in seq order, applied onto the analysed values.
[[nodiscard]] EffectiveNote effective_state(const Note& note, const EditList& edits);

struct DirtyParams { Seconds crossfade_margin{0.050}; };
/// Source ranges whose rendered content may differ between two edit lists of the same track.
[[nodiscard]] Result<std::vector<SourceRange>> dirty_regions(std::span<const Note> notes,
    const EditList& old_edits, const EditList& new_edits, const DirtyParams& params);

struct MatchParams { Ratio keep_overlap{0.8}; Ratio child_coverage{0.5}; };
[[nodiscard]] NoteMatching match_notes(const AnalysisResult& old_a, const AnalysisResult& new_a,
                                       const MatchParams&);
struct RemapResult { EditList edits; std::vector<RemapEntry> report; };
[[nodiscard]] Result<RemapResult> remap_after_reanalysis(const EditList&, const AnalysisResult& old_a,
                                                         const AnalysisResult& new_a,
                                                         const MatchParams&);

// core/timemap
struct TimeMapParams { Seconds min_segment{0.001}; };
[[nodiscard]] Result<TimeMap> build_time_map(std::span<const Note> notes, const EditList& edits,
                                             const TimeMapParams& params);
```

`TimeMap` members (all pure, thread-safe, real-time safe, `noexcept` unless noted):
`identity()`, `from_anchors(vector<Anchor>) -> Result<TimeMap>`, `to_output(SourceTime) ->
OutputTime`, `to_source(OutputTime) -> SourceTime`, `stretch_ratio(SourceTime) -> Ratio` (right
derivative at an anchor), `anchors() -> span<const Anchor>`, `is_identity()`.

### `EditList` semantics

| | |
|---|---|
| `add` | Validates every spec; assigns `seq` from the explicit `next_seq_` member (never reused), `group` = the first `seq` of the call; appends. **If the cursor is not at the end, the redo tail moves to `discarded()` first** (it can no longer be redone, but nothing is deleted: the full history is `discarded() + active() + redo_tail()`, so a persisted log stays append-only and seqs are never reused). One call = one group = one undo step. A drag gesture batches its ticks in the tool and calls `add` once on release. Errors: `invalid_argument` (empty group, invalid spec). |
| `reset` | One `reset` edit per category, origin `"reset"`, targeting all the notes, in one group. Keeps history. Errors: `invalid_argument` for empty note or category lists. |
| `undo` / `redo` | Move the cursor over one whole group. Nothing is deleted. Error: `out_of_range` when there is nothing to undo or redo. |
| `retargeted` | Rewrites `note_ids` through a `NoteIdMap` (old id → new ids), in all three partitions (active, redo tail, discarded). Ids absent from the mapping, or mapped to nothing, move to `orphaned_note_ids` (they never come back). Same seqs, groups and cursor. Duplicates in the result are removed. |
| `from_parts` | `EditListParts {track, edits, cursor, discarded, next_seq}`. For persistence (Phase 7) and tests. Errors: `corrupt_data` if `seq` is not strictly increasing, `next_seq` is not above every seq, a group is not contiguous, a spec is invalid or the cursor is past the end. Recorded (retargeted) edits with only orphaned ids are valid. |
| Pure? | Yes. Every method returns a new list; nothing mutates. Copy is O(n) in the edit count (see Open questions). |
| Thread safety | Concurrent const access is safe. |
| Real-time safe | No (allocates). |

### `effective_state` semantics (spec §5.3)

- Start from neutral values per parameter (table above).
- Walk `active()` in order. An edit applies if `note.id` is in `target.note_ids`.
  - `set`: value replaces the accumulator of **that parameter**.
  - `add`: accumulator += value (offsets only). `scale`: accumulator *= value (multipliers only).
  - `reset`: parameter back to neutral, or all parameters of the category if `param` is empty.
  - `time_handle`: keyed set/remove as described above.
- Finish (review S4), in this order:
  1. clamp to the legal range: fade lengths ≥ 0, `within_note_flatten` in [0, 1], `length_scale` and
     `attack_time_scale` ≥ `k_min_time_scale` (1e-3);
  2. snap float dust to neutral: an offset with `|x| <= k_neutral_offset_abs` (1e-9) becomes 0, a
     multiplier within `k_neutral_ratio_abs` (1e-12) of 1 becomes 1. So `+0.1 +0.2 −0.3` is
     neutral and the note stays bit-exact passthrough (CLAUDE.md rule 5);
  3. `center = note.pitch_center + center_offset`;
  4. if both fades are 0, `fade_shape` is the neutral value (equal-sounding states compare equal);
  5. `is_edited` = any field differs from neutral (exact comparison after the snap).
- Precondition: `note.track_id == edits.track_id()` (`ANNO_EXPECTS`). Edits that name unknown ids
  simply never match. Orphaned ids are ignored by the fold.
- Pure; real-time safe: **no** (allocates for `handles`, copies the id).
- Cost: O(active edits × targets per edit) per note, so O(N²) for a track-wide group edit. Accepted
  until measured; a sorted-id index can be added inside `EditList` without an API change.

### `dirty_regions` semantics

- Meaning: *content-dirty* source ranges, a safe over-approximation of where rendered samples may
  differ. Inactive notes (`is_active == false`, ghost candidates) are ignored here, in
  `build_time_map` and in the overlap check: they have no audio.
- For each active note: fold under both lists. If the two `EffectiveNote`s differ, the note's range
  `[source_start − margin, source_end + margin]` is dirty.
- If a *timing* field differs (`onset_shift`, `length_scale`, `handles`, `attack_time_scale`), the
  TimeMap changes there and overlap repair can cascade later notes. Conservatively, everything from
  the end of the previous active note (or the origin for the first note) to the end of the last
  active note plus margin is dirty. This is deliberately coarse; a TimeMap diff can replace it in
  Phase 5 without an API change.
- Result: sorted, merged, non-overlapping ranges, clamped at the source origin.
- Errors: `invalid_argument` if the two lists have different `track_id`s or a note's `track_id`
  differs from theirs.
- Deviation from spec §4.4: the signature takes `notes` (spec has only the two edit lists), because a
  range cannot be computed from note ids alone.

### `match_notes` and `remap_after_reanalysis` (spec §5.7)

`match_notes` is geometry only. Let `overlap(a, b)` be the length of the intersection of the source
spans and `len(x)` the span length. For each old note `o`:

1. **keep**: a new note `n` with `overlap / max(len(o), len(n)) >= keep_overlap`. `o → [n]`.
2. **split**: otherwise, two or more new notes `n_i` with `overlap(o, n_i) >= child_coverage *
   len(n_i)` that together cover `>= keep_overlap * len(o)`. `o → [n_1 … n_k]`.
3. **merge**: otherwise, a new note `n` with `overlap(o, n) >= child_coverage * len(o)`. Several old
   notes can point at the same `n`; the **longest** of them (ties: earliest) maps to `n`, the others
   are `merged_away` and map to nothing.
4. **orphan**: nothing matched. `o → []`.

After these rules, if several old notes map to the same single new note, the longest keeps it
(ties: earliest) and the others become `merged_away` (map to nothing). Anything ambiguous therefore
degrades to an orphan report, never to a silent drop. Only active notes of both analyses take part.

`remap_after_reanalysis` = `match_notes` + `EditList::retargeted` + a report with one entry per
old note that has edits: `RemapEntry {NoteId old_id; RemapOutcome outcome (kept, split, merged_kept,
merged_away, orphaned); std::vector<NoteId> new_ids}`. `NoteMatching` is the `NoteIdMap` plus the
outcome per old note. A split copies a time handle to every child; only the child whose body
contains the handle's source uses it (the others ignore it, see below). The edits of orphaned and merged-away notes stay in the list under `orphaned_note_ids`:
**nothing is dropped**. Errors: `invalid_argument` if the two analyses have different `source_id`s
or the edit list is for a different track than the notes.

### `TimeMap` (class, `core/types`) and `build_time_map` (`core/timemap`)

`TimeMap` is the pure mapping: a list of `TimePair` anchors, strictly increasing in both axes
(no minimum spacing), slope 1 outside the first and last anchor, linear in between. Members (pure,
thread-safe, real-time safe unless noted): `identity()`, `from_anchors(vector<TimePair>) ->
Result<TimeMap>` (`invalid_argument` if not strictly increasing; allocates), `to_output`,
`to_source`, `stretch_ratio(SourceTime) -> Ratio` (right derivative at an anchor),
`anchors()`, `is_identity()` (true iff every anchor has `source` and `output` at the same offset 0,
i.e. `output == source` exactly; then both conversions return their input bit-exactly).
`TimeMap` uses raw seconds internally, allowed in `core/types` (ADR-0003).

`build_time_map` returns `Result<TimeMapBuild>`, `TimeMapBuild {TimeMap map; int repaired_notes;}`
(review S8: a diagnostic does not belong in the mapping class).

Inputs: the track's **active** notes, sorted by `source_start` and non-overlapping (polyphony is
Phase 11 → `invalid_argument`), and the edit list (`track_id` must match).

1. Anchor `(0, 0)`.
2. Per note, with `eff = effective_state(note, edits)`, desired anchors:
   - `start → start + onset_shift`
   - `attack_end → start_out + (attack_end − start) * attack_time_scale`
   - `source_end → attack_out + (source_end − attack_end) * length_scale`
   - `release_start` is not an anchor (linear in the body, so it adds nothing).
   - Each handle of `eff` with `attack_end < source < source_end` is an anchor at its absolute
     `output` target (**handles are absolute and win over `length_scale` inside the body**; a handle
     outside the body is ignored by this note; handles in inter-note gaps are not supported in
     Phase 1).
3. Merge coincident source points (a note starting at 0, abutting notes, a zero-length attack, a
   release at the end): one anchor remains, with the **later** `output` of the candidates, which
   stretches the earlier segment rather than creating a jump.
4. Repair, left to right, with `min_segment` as the minimum output spacing between consecutive
   anchors:
   - if a note's first anchor is not at least `min_segment` after the previous anchor's output, the
     **whole note shifts later**, keeping its attack segment's shape (consonant protection);
   - a handle whose target does not fit strictly between its neighbours is clamped; if no room
     remains it is dropped;
   - a body shorter than `min_segment` is extended to `min_segment`;
   - each repaired note (shifted, clamped, dropped or extended) counts once in `repaired_notes`.
   Negative shifts before the origin, crossing handles and zero-length segments are ordinary user
   input handled here; none is an error.
5. Gaps between notes need no code: interpolation between neighbouring anchors stretches them.
6. `internal` is returned only if the repaired list still fails `from_anchors` (unreachable).

Errors: `invalid_argument` (overlapping/unsorted notes, other track).

Identity: with no time/attack edits every anchor has `output == source`; the builder returns
`TimeMap::identity()` exactly, so `to_output(s) == s`. Round trip: `to_source(to_output(s))` within
`k_timemap_roundtrip` (1e-9 s) otherwise.

## Effect on audio
None in Phase 1 (nothing renders). The contracts here fix what Phase 2+ must honour:
- `is_edited == false` ⇒ passthrough (bit-exact, CLAUDE.md rule 5).
- Identity `TimeMap` ⇒ no re-synthesis for time.
- `dirty_regions` over-approximates by `crossfade_margin` only.

## Split / join decisions
- **`EditSpec` vs `Edit`** (join test "data clump" rejected): tools produce content; only the list
  may assign `seq`/`group`/`created_at`. One struct would let a tool forge sequence numbers.
- **`Edit` has no `id`** next to `seq` (spec has both): `seq` is unique and never reused, so `id`
  would be a duplicate of it. ADR-0005.
- **`EditList` vs `effective_state` vs `dirty_regions`** (split a class, rule 3 "different reasons to
  change"): the list is bookkeeping, the fold is maths, dirty regions are policy. The last two are
  free functions.
- **`TimeMap` is a class in `core/types`, `build_time_map` is in `core/timemap`** (split class, rule
  3): the mapping depends only on units and must be usable by layer-2 synthesis; building it reads
  edits. `Anchor` and the time handle are one `TimePair` (join rule 1).
- **`match_notes` separate from `retargeted`** (split function rules 1, 4): geometry is testable with
  plain notes; the retargeting is testable with plain id maps.
- **Param table instead of per-category structs**: 19 parameters × ops × units would otherwise
  repeat in the validator, the fold and the reset. One table is read by all three.
- **No `Tool`/`Macro` interfaces yet** (join rule 5): they arrive with the first tool in Phase 4.
- **`NoteIdMap`** = `std::map<NoteId, std::vector<NoteId>>` (ordered, so output is deterministic).
- **`match_notes` is kept, not deferred**: it is small, pure and gives the report its meaning; its
  thresholds are open question 6.

## Decisions that go beyond the spec (ADR-0005, proposed)
1. `Edit` has no `id`; `seq` is the identity. `GroupId` is the first `seq` of the group.
2. `EditSpec` (tool output) is separate from `Edit` (recorded).
3. `param` is a closed `EditParam` enum with a typed `EditValue`, not `str` + `float | Curve`. No
   `Curve` type yet (no §5.3 parameter needs one).
4. `EditOp::reset` added, so "reset keeps history" is an ordinary edit in the fold.
5. `EditTarget` has no `source_range` and no `track_id`. The list is per track (as is the TimeMap and
   the log file in §5.8). `orphaned_note_ids` is new.
6. `set` replaces earlier edits of the same **parameter**, not of the whole category. For categories
   with one parameter this is the same thing as the ROADMAP wording.
7. `add` after `undo` discards the redo tail (linear undo).
8. `created_at` is a parameter of `add`, not read from the clock, so the list stays pure and
   deterministic.
9. `EffectiveNote` additionally carries `center_offset`, `onset_shift`, `length_scale`, `handles`
   (the TimeMap needs them); lacks `harmonic_gains_db` until Phase 12.
10. `dirty_regions` takes the notes.
11. Free functions `validate(...)`, `find_note`, `match_notes`; `TimeMap::repair_count`.
12. `AnalysisResult` omits `beats`, `tempo`, `key`, `chords` until Phase 8.
13. `TimeMap` lives in `core/types` (layer 0) and `build_time_map` in `core/timemap` (spec §4.4
    lists it under edits). `Anchor`/handle are one `TimePair`; `TimeMapBuild` carries the repair
    count.
14. Tools and macros return `EditSpec` (a group), not `Edit` (spec §5.1, CLAUDE.md rule 2 wording
    "Edit objects"): `seq`, `group` and `created_at` belong to the list.
15. The redo tail is kept in `discarded()` after an `add`, so the history is append-only.
16. Offsets allow `set`/`add`, multipliers `set`/`scale`; spec §5.1 allows all three ops on every
    parameter but only these combinations have a meaning.
17. Spec's per-note `pitch_drift`/`pitch_mod`/`amp_db` live on a `Note::grid` that the spec does not
    name. Names change by unit rule: `loudness_lu` → `loudness` (LUFS), `amp_db` → `amp` (dBFS),
    `confidence` → `confidence_prob`.

## Dependencies
- `core/types`: no dependencies.
- `core/edits`: PUBLIC `anno::core_types`.
- `core/timemap`: PUBLIC `anno::core_types`, `anno::core_edits`.
- Raw seconds (`unchecked_*`) are used only in `core/types` and `core/timemap`; `core/edits` uses
  `SourceTime` arithmetic and `Seconds` only.
- `scripts/check_includes.py` and the CMake layer check must pass unchanged.
- No external libraries.

## Tests
Test names describe behaviour; tolerances in `tests/tolerances.h` (new engineering entries
`k_fold_commute_abs`, `k_fold_commute_seconds`, `k_neutral_offset_abs`, `k_neutral_ratio_abs`).

- **Unit**
  - validators: each rejected shape has a test;
  - param table: every `EditParam` has a category, kind and neutral;
  - `EditList`: add/undo/redo/reset/from_parts, group contiguity, redo-tail kept in `discarded()`
    with seqs never reused, `retargeted`, retarget → `from_parts` round trip, orphan-only edits;
  - `effective_state`: each parameter's op semantics, ignoring other notes, ignoring the undone tail;
  - `TimeMap`: forward/inverse/slope on hand-built anchors, `from_anchors` rejections;
  - `build_time_map`: attack protection, gap stretch, overlap repair, handles, errors;
  - `dirty_regions`; `match_notes` (keep, split, merge, orphan, ties); `remap_after_reanalysis`.
- **Contract** (property-based with a fixed-seed generator; N = 500 cases each)
  - no edits ⇒ `effective_state` equals the analysed values and `is_edited == false`;
  - `add` edits commute (within tolerance); `scale` edits commute;
  - `set` overrides earlier edits of its parameter;
  - undo restores the previous effective state **exactly**; any random sequence followed by full undo
    gives the original state; undo/redo round trip;
  - `TimeMap` strictly monotonic for random edits (including touching notes, a note at 0, a
    zero-length attack, crossing handles, negative shifts); `to_source(to_output(s)) == s` within 1e-9 s;
    identity with no time edits (exact);
  - `dirty_regions` covers every note whose effective state changed, and the result lies within
    `margin` (+ neighbour gaps for timing edits) of those notes;
  - remap: no edit is ever dropped (kept + orphaned account for every target id).
- **Audio scenarios:** none (no rendering).

## Open questions
Answered by a safe default now; a human decides at audit #1. Each is cheap to change.

1. **Overlap policy in `build_time_map`:** moving a note onto its neighbour pushes the neighbour
   later (whole-note shift). The alternative, shortening the moved note, would change the length the
   user asked for. Which does Melodyne-style editing want?
2. **`set` scope:** per parameter (chosen) vs per category.
3. **Undo model:** chosen is linear undo with superseded branches kept in `discarded()`. A real
   branching history would need a tree.
4. **Default `FadeShape`** (neutral): `linear` chosen arbitrarily; Phase 6 may change it.
5. **`EditList` copy cost** is O(n) per `add`. Fine for thousands of edits; a persistent structure
   can replace the vector internally without an API change.
6. **Match thresholds** (`keep_overlap` 0.8, `child_coverage` 0.5) are untuned guesses until Phase 7
   has real re-analysis data.

## Review log
### Round 1 — CHANGES REQUIRED (7 blocking, 9 should-fix, 6 notes)
- B1 (ADR acceptance): cannot be resolved without a human. Mitigated by choosing the ROADMAP-closest
  options and listing the decisions in "Open questions"; ADR-0005 stays `proposed`. **Needs a human.**
- B2: `TimeMap` moved to `core/types`; builder stays in `core/timemap`.
- B3: `next_seq_` explicit; redo tail moved to `discarded()`; `from_parts` carries both.
- B4: orphan-only edits valid when recorded; `add` stays strict; round-trip test added.
- B5: ops restricted per kind; value type = parameter kind.
- B6: coincident anchors merged, repairs defined, no input-driven `internal`; contract cases added.
- B7: `freq_hz`, `amp_linear`, `band_energy_linear`, `log_mag_db`.
- S1: keyed handle set/remove; S2: handle rules; S3: conservative timing dirty range, ghosts
  excluded; S4: final clamps and float-dust snap; S5: `NoiseGains`; S6: types defined; S7:
  `TimePair`; S8: `TimeMapBuild`; S9: batching in tool, `reset` origin, `center_offset` dropped,
  track precondition, grid equality, extra tests.
- N1: ambiguity degrades to orphan reports; N2/N3 recorded in ADR-0005.
