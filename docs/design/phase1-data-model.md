# Design note: Phase 1 — data model and edit contracts

- **Phase:** 1
- **Status:** draft
- **Spec sections:** architecture spec §3 (types), §4.4 (edits), §5.1–5.4, §5.7; ROADMAP Phase 1;
  ADR-0002, ADR-0003, ADR-0005 (proposed with this note)
- **Folder / CMake target:**
  - `src/core/types` (layer 0): new headers, existing target `anno::core_types`.
  - `src/core/edits` (layer 2): new target `anno::core_edits`.
  - `src/core/timemap` (layer 3): new target `anno::core_timemap`.

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
are recorded in ADR-0005 with status **proposed**: a human confirms or changes them at structure
audit #1.

## Public types

### Layer 0: `core/types`

Plain data unless marked class. All value types; analysis objects are shared as
`std::shared_ptr<const T>` and never mutated (architecture.md).

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `NoteId`, `TrackId` | struct (string wrapper, like `SourceId`) | — (non-empty is checked by validators) | value | value |
| `EditSeq`, `GroupId` | struct (`uint64_t` wrapper) | — | value | value |
| `FrameGrid` | struct | checked by `validate` | value | value |
| `FrameClass`, `OnsetKind`, `SeparationIn`, `Algorithm`, `FadeShape`, `EditCategory`, `EditParam`, `EditOp` | `enum class` | closed sets | — | — |
| `FrameMatrix` | struct (`rows`, `cols`, `std::vector<float>`) | `data.size() == rows*cols`, checked by `validate` | value | value |
| `PitchTrack`, `OnsetList`, `HarmonicTrack`, `NoiseTrack`, `EnvelopeTrack` | struct | sizes checked by `validate` | value | inside `Note` / `AnalysisResult` |
| `Note` | struct | `validate(Note)` | `AnalysisResult` | `shared_ptr<const AnalysisResult>` |
| `AnalysisResult` | struct | `validate(AnalysisResult)`: notes sorted by `source_start`, unique ids, valid notes | creator | `shared_ptr<const AnalysisResult>` |
| `EditTarget` | struct | non-empty `note_ids`; orphaned ids are kept separately | value | value |
| `TimeHandle` | struct (`SourceTime source`, `OutputTime target`) | — | value | value |
| `EditValue` | `std::variant<std::monostate, Cents, Ratio, Decibels, Seconds, FadeShape, TimeHandle>` | must match the parameter's `ValueKind` | value | value |
| `EditSpec` | struct | `validate(EditSpec)` | tool or macro | value |
| `Edit` | struct (`EditSeq seq`, `GroupId group`, `WallTime created_at`, `EditSpec spec`) | only `EditList` creates these | `EditList` | value |
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
struct HarmonicTrack { FrameGrid grid; FrameMatrix freq; FrameMatrix amp; FrameMatrix phase_rad; };
struct NoiseTrack    { FrameGrid grid; FrameMatrix band_energy; std::vector<FrameClass> frame_class; };
struct EnvelopeTrack { FrameGrid grid; FrameMatrix log_mag; std::vector<Hz> freqs; };

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
release_start <= source_end`; curve lengths equal `grid.n_frames`; matrix shapes; probabilities in
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
    onset_shift, length_scale, time_handle,             // time         Seconds, Ratio (> 0), TimeHandle
    attack_time_scale,                                  // attack       Ratio (> 0)
};
```

`separation`, `assignment` and `sound` have no parameters yet. They exist as categories so a reset
can name them and the enum matches spec §5.1. Their parameters arrive with Phases 5, 7 and 12
(additive).

A constexpr table `param_info(EditParam)` gives the category, value kind, allowed ops, neutral value
and legal range of each parameter. `validate(EditSpec)` and the fold both read it, so there is one
source of truth.

| Parameter kind | Allowed ops | Neutral |
|---|---|---|
| offset (`Cents`, `Decibels`, `Seconds`) | `set`, `add`, `scale`, `reset` | 0 |
| multiplier (`Ratio`) | `set`, `scale`, `reset` | 1 |
| `fade_shape` | `set`, `reset` | `linear` |
| `time_handle` | `set`, `reset` | no handles |
| (any category, `param` empty) | `reset` only: clears the whole category | |

```cpp
struct EditTarget {
    std::vector<NoteId> note_ids;           // live targets
    std::vector<NoteId> orphaned_note_ids;  // targets that re-analysis could not re-attach
};
struct EditSpec {                           // what a tool or macro writes
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
    Cents center;                            // analysed pitch_center + center_offset
    Cents center_offset;                     // the folded pitch offset (0 = untouched)
    Ratio mod_scale, drift_scale, transition_in_scale, transition_out_scale;
    Cents formant_offset;
    Decibels gain;  Ratio within_note_flatten;
    Seconds fade_in, fade_out;  FadeShape fade_shape;   // lengths clamped >= 0
    std::array<Decibels, k_frame_class_count> noise_gain;  // indexed by FrameClass
    Seconds onset_shift;  Ratio length_scale;  std::vector<TimeHandle> handles;  // sorted by source
    Ratio attack_time_scale;
    bool is_edited;                          // any field differs from neutral
    friend bool operator==(const EffectiveNote&, const EffectiveNote&) = default;
};
```

### Layer 2: `core/edits`

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `EditList` | **class** | `cursor_ <= edits_.size()`; `seq` strictly increasing; every edit passed `validate(EditSpec)`; each group is a contiguous run of edits | value | value (copy), `shared_ptr<const EditList>` for snapshots |
| `DirtyParams` | struct | — | caller | value |
| `NoteMatching`, `MatchParams`, `RemapReport` | struct | — | value | value |

```cpp
class EditList {
public:
    explicit EditList(TrackId track);
    [[nodiscard]] static Result<EditList> from_log(TrackId, std::vector<Edit>, std::size_t cursor);

    [[nodiscard]] const TrackId& track_id() const noexcept;
    [[nodiscard]] std::span<const Edit> active() const noexcept;   // [0, cursor): what is folded
    [[nodiscard]] std::span<const Edit> log() const noexcept;      // everything incl. the redo tail
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
| `add` | Validates every spec; assigns `seq` = next counter value (never reused), `group` = the first `seq` of the call; appends. **If the cursor is not at the end, the redo tail is discarded first** (linear undo). One call = one group = one undo step. Errors: `invalid_argument` (empty group, invalid spec). |
| `reset` | One `reset` edit per category, targeting all the notes, in one group. Keeps history. Errors: `invalid_argument` for empty note or category lists. |
| `undo` / `redo` | Move the cursor over one whole group. Nothing is deleted. Error: `out_of_range` when there is nothing to undo or redo. |
| `retargeted` | Rewrites `note_ids` through a mapping (old id → new ids). Ids absent from the mapping, or mapped to nothing, move to `orphaned_note_ids`. Same seqs, groups and cursor. Duplicates in the result are removed. |
| `from_log` | For persistence (Phase 7) and tests. Errors: `corrupt_data` if `seq` is not strictly increasing, a group is not contiguous, a spec is invalid or the cursor is past the end. |
| Pure? | Yes. Every method returns a new list; nothing mutates. Copy is O(n) in the edit count (see Open questions). |
| Thread safety | Concurrent const access is safe. |
| Real-time safe | No (allocates). |

### `effective_state` semantics (spec §5.3)

- Start from neutral values per parameter (table above).
- Walk `active()` in order. An edit applies if `note.id` is in `target.note_ids`.
  - `set`: value replaces the accumulator of **that parameter**.
  - `add`: accumulator += value. `scale`: accumulator *= value.
  - `reset`: parameter back to neutral, or all parameters of the category if `param` is empty.
  - `time_handle` `set`: replaces the handle with the same `source`, or inserts one.
- Finish: `center = note.pitch_center + center_offset`; fade lengths clamped to ≥ 0; if both fades
  are 0, `fade_shape` is reported as the neutral value (so equal-sounding states compare equal);
  `is_edited` = any field differs from neutral.
- Pure; real-time safe: **no** (allocates for `handles`, copies the id).
- Cost: O(active edits × targets per edit) per note.

### `dirty_regions` semantics

- For each note: fold under both lists. If the two `EffectiveNote`s differ, the note's range
  `[source_start − margin, source_end + margin]` is dirty.
- If the *timing* fields differ (`onset_shift`, `length_scale`, `handles`, `attack_time_scale`),
  the gaps to the previous and next notes are dirty too, because the TimeMap interpolates across
  them.
- Result: sorted, merged, non-overlapping ranges, clamped at the source origin.
- It does **not** model the output-time displacement of later audio when a timing edit pushes
  neighbours. The renderer (Phase 2/5) keys its cache on the TimeMap for that.
- Errors: `invalid_argument` if the two lists have different `track_id`s.
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

A split/keep/merge match of a note is only used if it is the unique best (largest overlap) for that
new note, so two old notes never both "keep" the same new note.

`remap_after_reanalysis` = `match_notes` + `EditList::retargeted` + a report with one entry per
old note that has edits: `{old_id, outcome (kept, split, merged_kept, merged_away, orphaned),
new_ids}`. The edits of orphaned and merged-away notes stay in the list under `orphaned_note_ids`:
**nothing is dropped**. Errors: `invalid_argument` if the two analyses have different `source_id`s
or the edit list is for a different track than the notes.

### `build_time_map` semantics (spec §5.4)

Inputs: the track's notes (sorted by `source_start`, **non-overlapping**; polyphonic overlap is
Phase 11 → `invalid_argument`) and the edit list.

1. Anchor `(0, 0)` at the origin (the output starts at 0; the first gap stretches).
2. For each note, in order, with `eff = effective_state(note, edits)`:
   - `tau_start = start + onset_shift`
   - `tau_attack_end = tau_start + (attack_end − start) * attack_time_scale`
   - `tau_end = tau_attack_end + (source_end − attack_end) * length_scale`
   - `release_start` is placed proportionally in the body (linear).
   - Time handles inside `(attack_end, source_end)` become anchors at their `target`.
3. Overlap repair, left to right, whole-note granularity: if a note's `tau_start` is less than the
   previous note's `tau_end + min_segment`, the **whole note (and its handles) shifts later** so the
   consonant/attack segment keeps its shape. Handle targets are clamped inside the body. The number
   of repairs is available as `TimeMap::repair_count()` so a tool can tell the user.
4. Body shorter than `min_segment`: clamped to `min_segment`.
5. Gaps between notes need no code: linear interpolation between the neighbouring anchors stretches
   the gap, so there is never silence or double audio.
6. Outside the anchors the slope is 1, continuing the offset of the last anchor.

Errors: `invalid_argument` (overlapping notes, edit list for another track), `internal` if the
repaired anchors are still not strictly increasing (a bug).

Identity: with no time/attack edits every anchor is `(s, s)`, so `to_output(s) == s` exactly.
Round trip: `to_source(to_output(s)) == s` within `k_timemap_roundtrip` (1e-9 s).

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
- **`TimeMap` is a class, `Anchor` a struct**: the invariant (monotone) must hold at all times.
- **`match_notes` separate from `retargeted`** (split function rules 1, 4): geometry is testable with
  plain notes; the retargeting is testable with plain id maps.
- **Param table instead of per-category structs**: 19 parameters × ops × units would otherwise
  repeat in the validator, the fold and the reset. One table is read by all three.
- **No `Tool`/`Macro` interfaces yet** (join rule 5): they arrive with the first tool in Phase 4.
- **`NoteIdMap`** = `std::map<NoteId, std::vector<NoteId>>` (ordered, so output is deterministic).

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
13. Spec's per-note `pitch_drift`/`pitch_mod`/`amp_db` live on a `Note::grid` that the spec does not
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
`k_fold_commute_abs`, `k_fold_commute_seconds`).

- **Unit**
  - validators: each rejected shape has a test;
  - param table: every `EditParam` has a category, kind and neutral;
  - `EditList`: add/undo/redo/reset/from_log, group contiguity, redo-tail discard, `retargeted`;
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
  - `TimeMap` strictly monotonic for random edits; `to_source(to_output(s)) == s` within 1e-9 s;
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
3. **Linear undo** (redo tail discarded on `add`) vs keeping dead branches in the log.
4. **Default `FadeShape`** (neutral): `linear` chosen arbitrarily; Phase 6 may change it.
5. **`EditList` copy cost** is O(n) per `add`. Fine for thousands of edits; a persistent structure
   can replace the vector internally without an API change.
6. **Match thresholds** (`keep_overlap` 0.8, `child_coverage` 0.5) are untuned guesses until Phase 7
   has real re-analysis data.

## Review log
(structure-reviewer findings go here)
