# Design note: tools/time and tools/attack_speed (Phase 5)

- **Phase:** 5 (Timing)
- **Status:** implemented, as built; was draft
- **Spec sections:** features spec §11 (Time Tool and Time Handles), §12 (Attack Speed Tool); architecture spec §5.1–5.4 (`time` and `attack` categories, `build_time_map`); ADR-0005 (edit model), ADR-0008 §4 (shift first), ADR-0023 (stretched rendering).
- **Folders / CMake targets:** `src/tools/time` (`anno::tools_time`), `src/tools/attack_speed` (`anno::tools_attack_speed`), both layer 5, each linking only `core/types`.

## Purpose

The tools a user gesture on the timeline calls (features spec §11, §12): they read the analysis and return the `EditSpec` the gesture means, like `pitch_drag_edit` (Main Tool, Phase 4). They never touch audio. The CLI scenario reader (`tool` edits) and, later, the UI call them. `tools/note_separation` is **not** in this note: separations are analysis edits (re-segmentation), for which no edit category exists yet; it waits for the phase that adds them (the roadmap line is carried, not dropped).

## Public types and functions

```cpp
namespace anno {

/// Moves a note in time: its onset is played `shift` later (earlier if negative) than analysed.
struct NoteMove {
    NoteId note;
    Seconds shift;     // output onset minus the analysed source onset (relative to the source, not to
                       // an earlier move: a second move replaces the first, op set)
};

/// Sets how long the note's body plays. The body is [attack_end, source_end]; the attack is
/// protected (it only changes with the attack speed tool), so the whole note's output length is
/// the attack's plus `body_length`.
struct NoteLength {
    NoteId note;
    Seconds body_length;   // output duration of the body, > 0
};

/// A breakpoint inside the body: source instant `handle.source` plays at output time `handle.output`.
struct NoteHandle {
    NoteId note;
    TimePair handle;       // source strictly inside (attack_end, source_end)
};

[[nodiscard]] Result<EditSpec> note_move_edit(const AnalysisResult&, const NoteMove&);
[[nodiscard]] Result<EditSpec> note_length_edit(const AnalysisResult&, const NoteLength&);
[[nodiscard]] Result<EditSpec> note_handle_edit(const AnalysisResult&, const NoteHandle&);
/// Removes the handle keyed by `source` (within k_handle_key). Removing a handle that is not there
/// is a silent no-op in the fold; a stale handle outside the current body stays removable.
struct NoteHandleRemoval {
    NoteId note;
    SourceTime source;
};
[[nodiscard]] Result<EditSpec> note_handle_remove_edit(const AnalysisResult&, const NoteHandleRemoval&);

}  // namespace anno

namespace anno {

/// Sharper (< 1) or softer (> 1) attack: the attack segment [source_start, attack_end] plays
/// `time_scale` times as long. 1 = analysed.
struct AttackSpeed {
    NoteId note;
    Ratio time_scale;      // >= k_min_time_scale
};
[[nodiscard]] Result<EditSpec> attack_speed_edit(const AnalysisResult&, const AttackSpeed&);

}  // namespace anno
```

| Edit | origin | category | param | op | value |
|---|---|---|---|---|---|
| move | `"time_tool"` | time | `onset_shift` | set | `shift` |
| length | `"time_tool"` | time | `length_scale` | set | `body_length / (source_end - attack_end)` |
| handle | `"time_tool"` | time | `time_handle` | set | `handle` |
| handle removal | `"time_tool"` | time | `time_handle` | reset | `handle` |
| attack speed | `"attack_speed_tool"` | attack | `attack_time_scale` | set | `time_scale` |

Targets are the note's segment id. Each result has passed `validate_for_add`.

**Errors, in the order checked.** `invalid_argument`: unknown or inactive note (looked up with `find_note` plus an `is_active` check; every note kind is accepted, unlike `pitch_drag_edit`); a non-finite value; `body_length` or `time_scale` not positive; `note_length_edit` on a note with no body (`attack_end == source_end`, compared with `==` as `build_time_map` merges them); `attack_speed_edit` on a note with no attack (`attack_end == source_start`); a handle whose source is not strictly inside the analysed body by more than `k_handle_key`, or whose output is not finite and >= 0 (the builder ignores a handle outside the body silently, so the tool refuses; how a handle's output orders against the note's other anchors is the builder's repair, not the tool's). Then `out_of_range`: a computed `length_scale` or `time_scale` positive but below `k_min_time_scale` (checked before `validate_for_add`, which would otherwise answer `invalid_argument`). A huge `body_length` is accepted (`length_scale` has no maximum). A `shift` below `k_neutral_snap_offset` (1e-9 s) or a scale within 1e-12 of 1 folds to neutral: the edit is a no-op, not an error.

**Interactions to know.** (1) A handle is an absolute output anchor (ADR-0005 §16) while a move, a length or an attack change moves the note's other anchors: moving a note after placing a handle leaves the handle where it was, and `build_time_map` repairs the conflict (`repaired_notes` rises). The caller re-issues handles after such an edit; making handles shift-relative needs an ADR and is not done here. (2) ADR-0023: a note whose zone meets a stretched part of the map cannot also carry a pitch, level or formant edit (`not_implemented`, named in the error), so a time edit and a Phase 4 edit on the same note or a stretched neighbour do not render together yet; stereo sources do not render stretches. (3) The map is built by the caller: with the default `keep_length` tail and a known end, a move or length edit stretches the following unedited notes and gaps until the shift-first layout of ADR-0008 §4 lands; `render_region` builds with the shifted tail.
Pure; thread-safe; not real-time safe (allocates the spec).

## Effect on audio

None by itself (edits only). The edits change the TimeMap through `build_time_map` (exactly, under `sample_rate` 0; to a whole sample under `render_region`'s settings) and render through `stretch_span` (ADR-0023) for mono sources, with the limits under "Interactions".

## Split / join decisions

- **Two folders** (`time`, `attack_speed`): different edit categories and different user tools (features spec §11 and §12); one file pair each (new folder rule: the roadmap names both).
- **`note_move_edit`, `note_length_edit`, `note_handle_edit` in one header**: same tool, same validation of the note; the shared lookup is one private function.
- **No shared helper between the two folders**: both use `find_note` from `core/types` plus an `is_active` check (a shared helper would need a third target).
- **The tools do not know the other edits** (`attack_time_scale` already applied, neighbours): a length is "body length" so it does not depend on them.
- **`NoteMove` carries a relative `Seconds`**, not an `OutputTime`: the UI subtracts the analysed onset in one place (its own drag code); revisit when the UI exists.

## Dependencies

`core/types` only. Tests link `tools/time`, `tools/attack_speed`, `core/edits`, `core/timemap` (to check the built map). CMake: `add_subdirectory(tools/time)` and `add_subdirectory(tools/attack_speed)` in `src/CMakeLists.txt` next to `tools/main_tool`; one `anno_add_test` each in `tests/unit/CMakeLists.txt`; a `README.md` in each folder.

## Tests (written first)

- Each edit has the right origin, category, param, op, value and target and passes `validate_for_add`.
- A second move/length edit replaces the first when both go through `EditList` and `effective_state` (set semantics); a handle is keyed by its source time.
- **Through `build_time_map`, exact tests:** `TimeMapParams{}` with no known end (so the tail shifts), `sample_rate` 0: a move shifts the note's onset in the map by `shift`; a length edit makes the body `body_length` long (attack unchanged); a handle puts its source time at its output time; an attack speed changes only the attack segment; all to 1e-9 s and `repaired_notes == 0`.
- **Rounding:** the same with `sample_rate` set: the shift lands on a whole sample within half a sample.
- **Fixed tail:** with `keep_length` and a known end the following unedited note is stretched (recorded, until ADR-0008 §4).
- **Handles:** a handle placed and then the note moved: `repaired_notes` rises (recorded); a near-duplicate key (within `k_handle_key`) replaces the first; handles sort by source; removal by `NoteHandleRemoval` through `EditList`; removing an absent handle is a no-op; a category reset clears all handles; a handle within `k_handle_key` of the attack end is refused.
- **Order of errors:** a scale below `k_min_time_scale` is `out_of_range`, not `invalid_argument`.
- Errors: unknown/inactive note, NaN/inf/zero/negative values, no body, no attack, scale under `k_min_time_scale` (`out_of_range`), a handle at the attack end / source end / outside the note.
- The scenario JSON gets these as `tool` edits (`time_move`, `time_length`, `time_handle`, `attack_speed`) in a follow-up commit, with parser tests; `docs/AUDIO_VERIFICATION.md` §2 and `scenario_json.h` document them.

## Review log

- **Round 1 (2026-10-22): CHANGES REQUIRED, B1 (test parameters) and S1–S8**, answered in this revision: B1 (exact tests under `sample_rate` 0 with a shifted tail, rounding and fixed-tail tests separate), S1 (handles are absolute: documented and tested), S2 (output finite and >= 0, ordering is the builder's), S3 (`NoteHandleRemoval`), S4 (relative shift, sub-threshold no-op), S5 (check order), S6 (render interaction), S7 (CMake, READMEs, `find_note`), S8 (handle tests). `tools/note_separation` is carried in the ROADMAP.
