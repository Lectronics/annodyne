# core/analysis/orchestrator

`analyse()` for the `melodic` algorithm (layer 1; links the other analysis targets and `core/dsp`). Design:
`docs/design/phase3-orchestrator-cli.md` §1, ADR-0010.

Order: `read_mono` + `k_weight` once (loudness path) -> pYIN pitch -> onsets (with the pitch track) ->
`segment_notes` -> per `NoteSpan`: `note_grid`, `measure_loudness` (amp curve), `decompose_pitch_curve`
(uses the amp curve), a `Note` with centre/drift/mod/amp/loudness, `perceived_pitch` on that note ->
`validate(AnalysisResult)`. The orchestrator is the only place that builds a `Note`; harmonics, noise and
envelope stay empty until Phase 4. The result is a `shared_ptr<const AnalysisResult>`; the same source
and parameters give a bit-identical result on one build. A failing per-note stage aborts the whole
analysis with the note id in the message (segmentation guarantees at least 10 voiced pitch frames per
note, so this is a bug indicator, never a silent drop).

**Limitations:** `pitch` and `onsets` each read their own mono mix (a time cost only); there is no cancel
token or progress callback until the Phase 9 scheduler; only `Algorithm::melodic` is implemented.
