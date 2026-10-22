# Design note: Clips, layers, selections and groups (copy / paste / harmonise)

- **Phase:** data model reserved in 7 (project file format); render, `features/clipboard`, selection queries in 12; drawing and gestures in 10
- **Status:** draft, revision 1 (owner's scenario 2026-10-22). Not yet structure-reviewed; no ADR yet (to be written with the Phase 7 model, see "Where it goes").
- **Spec sections:** architecture spec §6 (tool table: "Copy/paste adds virtual notes"), `annodyne-features.md` §21, ADR-0008 (`EditTarget`, `SegmentId`), ADR-0025 (pinned markers; this note reuses them for group moves), `phase5-layout.md`.
- **Folders / CMake targets:** `src/core/types` (`Clip`, `Layer`, ids), `src/core/render` (layer mix), `src/features/clipboard` (paste, selection, grouping; layer 6, reads analysis and returns edits), UI only draws.

## The scenario this has to support (owner)

The user drags a rectangle over a run of notes (each drawn as its own waveform on a time x pitch plane) and presses copy, then paste. A popup asks for a transposition; "a 3rd" puts a copy of every selected note a third above the original, drawn in a different colour. The pasted notes stay selected, so the user moves them all a quarter note later. At the end of the phrase the user stretches the *original* last note by a quarter note so it ends with the harmony. Nothing is destroyed: the original recording is untouched, and every step can be undone.

## Model

1. **Clip** (the spec's "virtual note"). A clip plays the source span of one real note, placed at its own output time, with its own edits. It is not part of the recording's note list.
2. **Layer.** A layer is an ordered set of clips plus a gain. The recording itself is the **main layer** (the real notes and gaps, as today). Pasting creates a new layer, so the harmony has its own colour, gain, mute and solo.
3. **Render = main layer + the sum of the other layers.** The main layer renders exactly as today (layout, gaps, stretch). Each clip renders as an independent note (source span, its own pitch/formant/level edits, placed at its time) and is added into the output. With no clips, or every layer muted, the output is the main layer bit for bit (rule 5).
4. **Clips float.** A clip is placed at an absolute output time. It takes no part in the main layer's gap absorption: moving the harmony a quarter note later does not push or squeeze anything in the lead. Moving or lengthening a clip changes only that clip.
5. **Selection** is UI state plus a pure query (`select_notes`): the notes (real or clip) whose box intersects a rectangle in time and pitch. It is never stored in the project.
6. **Group.** A group is a named set of ids (notes or clips). A group edit is the same edit fanned out to each member by a tool or macro, so the core sees ordinary per-note edits (rule 2: tools only return edits).

### How each step of the scenario maps

| Step | What happens |
|---|---|
| Drag a rectangle | `select_notes(effective, TimeRange, PitchRange, policy) -> NoteSelection` (UI state) |
| Copy, paste, "a 3rd" | `paste(selection, PasteParams) -> PasteEdits`: one new layer, one clip per selected note, each with `center_offset` = the transposition |
| Harmony in another colour | UI colours by `LayerId`; the core only stores the layer's label and id |
| All pasted notes still selected, move 1/4 note later | One `clip_move` edit per clip, the same delta (a group move). Beat length comes from the tempo (Phase 8); the tool converts to seconds |
| Stretch the original last note by 1/4 note | An ordinary timing edit on the main layer (`note_length_edit`). The pinned-marker layout (ADR-0025) lets the silence after the note absorb it. The clip is unaffected |

### Group move on the main layer (reuses ADR-0025, no new solver)

Moving a group of main-layer notes by one delta is one `note_move_edit` per note with the same shift. Their markers all move together, so the intervals *between* them keep their lengths; only the two intervals at the *ends* of the group change, and they take the change in their gaps first. This is the pinned-marker rule applied to a run, which is why ripple is not needed.

## Public types (sketch; to be fixed in the Phase 7 ADR)

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `LayerId`, `ClipId` | struct | non-empty value; `ClipId` is a `SegmentId` kind (`SegmentKind::clip`) so `EditTarget` addresses it | caller | value |
| `Clip` | struct | `source` names a real note of the track's analysis; `placement` is an output time | `ClipSet` | value |
| `Layer` | struct | unique id; clips ordered by id | `ClipSet` | value |
| `ClipSet` | class | ids unique across layers; every clip's source exists in the analysis it is validated against | project | `shared_ptr<const>` |
| `NoteSelection` | struct | sorted unique ids | UI | value |
| `PasteParams` | struct | see below | caller | value |

```cpp
struct Clip {
    ClipId id;
    NoteId source;            // the real note whose span this clip plays (source time axis)
    OutputTime placement;     // where the clip's source note start plays (output axis)
};
struct Layer {
    LayerId id;
    std::string label;        // "Harmony +3rd"; the colour is UI/project metadata, not core
    Db gain_db{0.0};          // sum gain; mute = k_gap_mute_db style floor
    bool muted = false;
    std::vector<Clip> clips;
};
struct PasteParams {
    Cents transpose{0.0};     // chromatic cents; 0 = no pitch change (a plain copy)
    Seconds offset{0.0};      // placement shift of the whole selection
    bool keep_formants = true;// the copy keeps the original timbre (no chipmunk); spec §21
};
```

## Public functions (sketch)

```cpp
/// Notes (real and clips) whose box intersects the rectangle. Pure; UI-thread.
[[nodiscard]] NoteSelection select_notes(const std::vector<EffectiveNote>&, const ClipSet&,
                                         TimeRange, PitchRange, SelectPolicy);
/// Phrase / word groups from the timeline: notes separated by gaps >= min_gap start a new group.
[[nodiscard]] std::vector<NoteGroup> group_by_gaps(const SegmentTimeline&, Seconds min_gap);
/// New layer + clip edits for the selection. Returns edits, never touches audio (rule 2).
[[nodiscard]] Result<PasteEdits> paste(const NoteSelection&, const AnalysisResult&,
                                       const PasteParams&, LayerId new_layer);
/// Core render: main layer, plus layers summed (core/render).
[[nodiscard]] Result<RenderResult> render_region(const RenderInputs&, OutputRange, ...);
```

`RenderInputs` gains an optional `shared_ptr<const ClipSet>`; the cache key hashes the clip set (key version bump).

| | |
|---|---|
| Time axes | `Clip::source` is a source-axis note, `placement` is output axis; the clip's own edits are relative to its note, so no axis is mixed |
| Errors | `invalid_argument` (unknown source note, duplicate id, empty selection), `out_of_range` (placement outside the output length, policy below), `not_implemented` (polyphonic / multichannel as the engines today) |
| Real-time safe | the mix is block-wise additions of already rendered clips; clips render off the audio thread, like every other render (rule 6) |

## Effect on audio

- Adds sound: the sum of clip renders on top of the main layer. Only the mix is new; a clip is rendered by the existing engines (PSOLA for pitch, level, formant), so no new DSP in the first version.
- **Same pitch and same time = a loud double.** An exact copy at 0 cents and 0 offset adds the same wave to itself (+6 dB, no width). Real doubles drift in pitch and timing. A later optional `humanise` (small random pitch and timing offsets per clip) can address this; it is out of scope for the first version.
- **Sums can exceed full scale.** The core renders in float and does not limit; the level checker flags it, and the layer gain is the user's control. (A limiter is a host/output stage, not core.)
- Objective checks: main layer unchanged when the clip set is empty (bit-exact); a clip's pitch = source + transposition within 10 cents (the Phase 4 check); linearity (render with the layer = render without + the layer alone, to float rounding); placement is sample exact; no click at clip edges; a muted layer = no layer. The human listening checks: that the harmony sounds like a second singer, not a chipmunk (formants), which cannot be judged without a person.

## Split / join decisions

- Clips are **separate from real notes** (not "notes with a flag"): a real note's id is a property of the analysis and survives re-analysis remapping; a clip's source reference needs remapping when the analysis changes (Phase 7 `NoteIdMap`). A clip whose source note vanishes is reported, not silently dropped.
- Layers are **sets with a gain**, not full tracks: a track in this codebase has its own source audio and analysis; a layer reuses the track's.
- Selection is **not** in the core: it is transient UI state (coding standards §5: do not store derived or UI state in the model).
- Clips float rather than ripple or absorb. Interacting with the main layout would make a harmony edit move the lead, which the user never wants.

## Where it goes (recommendation)

| Part | Phase | Why |
|---|---|---|
| `Clip`, `Layer`, `ClipSet`, ids, `SegmentKind::clip`, project format carries `layers` | **7** (persistence) | The file format must be able to store clips from its first version, or a format migration is needed later |
| Tempo grid, key (for "a 3rd" as a scale degree, "quarter note") | 8 | Already planned there |
| UI: rectangle selection, layer colours, paste popup | 10 | Gestures only produce the edits below |
| Layer mix in `render_region`, `features/clipboard`, `group_by_gaps` | 12 (as the ROADMAP has it) | Needs the UI and tempo to be usable; the mix itself is small |
| Words / word groups | later (13, optional models) | Needs word-level segmentation (ASR/alignment) the analysis does not produce. Phrase groups by silence come free from `SegmentTimeline` |

The ADR for the model (call it "clips and layers") is written with the Phase 7 slice, not now.

## Dependencies

`core/types` only for the types; `core/render` links the layer mix; `features/clipboard` reads analysis and returns edits (no link to synthesis/render/dsp). No external library.

## Tests

- Unit: `ClipSet` invariants; `select_notes` boundaries (policy: any overlap vs fully inside); `group_by_gaps` on a fixture; `paste` produces one clip per note with the transposition, never audio.
- Contract: empty clip set renders bit-exactly; edit-list `EditTarget` addressing a clip; cache key changes with the clip set.
- Audio: harmony scenario (copy a three-note phrase, +400 cents, +0.25 s, lengthen the last original note): objective checks above and an `audition/` package for a human.

## Open questions (owner)

1. **Diatonic vs chromatic "3rd".** Chromatic (a fixed 3 or 4 semitones, simple) or diatonic (follows the key: major or minor third by scale degree, needs Phase 8 key)? I suggest both, chromatic first.
2. **Zero transposition.** I read "zero would do nothing" as "no pitch change", so paste still makes the (loud) double. Should it instead do nothing at all?
3. **Selection policy.** Any overlap with the rectangle, or fully inside? Suggestion: any overlap, with a modifier for fully inside.
4. **Are clips edits or a separate set?** I suggest edits in the `EditList` (a `clip` category with create and delete), so undo, redo and persistence are the one mechanism. It needs an ADR.
5. **Do you want auto-humanise for doubles** (small pitch/timing drift) as a paste option?
6. **Words.** Groups by silence are available. Real word grouping needs an alignment model; Phase 13?

## Review log

Not yet reviewed. Run the `structure-reviewer` before the Phase 7 slice builds anything from this.
