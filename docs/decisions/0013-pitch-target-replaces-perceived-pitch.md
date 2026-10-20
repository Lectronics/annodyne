# ADR-0013: PitchTarget replaces Note::perceived_pitch

- **Date:** 2026-10-20
- **Status:** accepted 2026-10-20 (owner: replace; "that concept is more accurate")
- **Phase:** 3 (follow-up slice)

## Context
`Note::perceived_pitch` is one number from a stability-weighted average. Real singing has rolls into and out of the target, overshoot, glides, ornaments and creak; averaging puts rolls into the pitch (owner review of Vocadito tracks 13 and 28). `docs/design/note-pitch-target.md` defines a `PitchTarget` (kind, target, secondary, core range, approach/release labels, flags, confidence) chosen from level and moving segments.

## Decision
1. `Note` loses `perceived_pitch` and gains `PitchTarget pitch_target` (types in `core/types/pitch_target.h`; analysis in `core/analysis/pitch_target/`, layer 1).
2. `PitchTarget::target` is always a finite `Cents` (A4 = 6900). For kind `unpitched` it equals `pitch_center` and means "no target". For `glide` it is the end target (start in `secondary`); for `ornament` the main note (other pitch in `secondary`).
3. `validate(Note)` checks: target finite; core inside `[source_start, source_end]`; segments sorted, non-overlapping, inside the note; `glide`/`ornament` have `secondary`; `unpitched` has target == `pitch_center`. A default `PitchTarget` validates, so hand-built notes (scenarios, fixtures) stay valid.
4. `render_key` hashes `pitch_target.target` only (never segments, flags or confidence), so cache keys do not change for no audible reason.
5. JSON export replaces `perceived_pitch_cents` by a `pitch_target` object (`kind`, `target_cents`, `secondary_cents`, `core_start_s`, `core_end_s`, `approach`, `release`, `flags`, `confidence`); plots read `target`.
6. `pitch_target_mode` (a per-note override) is NOT part of this ADR; it is an edit-model change decided with Correct Pitch in Phase 4 (ADR-0014). Analysis stores `target_whole`, `target_settled`, `target_end` candidates so a tool can switch mode without re-analysis.
7. The analysis version (`AnalysisResult::version`) changes; stored edits re-map through `remap_after_reanalysis`.

## Alternatives considered
- Keep `perceived_pitch` beside `pitch_target`: two names for one idea and a consistency invariant to police; rejected by the owner.
- `optional<Cents>` target for unpitched notes: forces every consumer to branch; a kind plus a finite fallback is simpler.

## Consequences
About 17 use sites change (types, validation, orchestrator, render key, CLI JSON and plots, scenario fixtures, tests). Phase 3 golden metrics must be re-baselined (ADR-0012 thresholds only go up). Phase 4 Correct Pitch reads the target, kind and flags.

## Spec updates
Architecture spec §3 (`Note.perceived_pitch`) and §4.2 (`perceived_pitch`, `estimate_pitch_target`, `annotate_take`, folder `pitch_target`) are overridden by this ADR; the specs stay read-only.
