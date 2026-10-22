# ADR-0024: Segment timeline types and the gap gain edit

- **Date:** 2026-10-22
- **Status:** proposed 2026-10-22 (owner chose the core subset of the gap work for Phase 5); structure review round 2 pending
- **Phase:** 5
- **Amends:** ADR-0008 (§1, §6: the timeline lives in `core/types`; gap ids; `AnalysisResult` gains `timeline` and `noise_floor`); ADR-0005 (record: new `EditParam` and a target-kind rule)

## Context
ADR-0008 decided that a track's source time is a partition into note and gap segments and that edits may target `SegmentId`. `docs/design/phase5-gaps-core.md` fixes the types against the code that exists and narrows Phase 5 to the gate.

## Decision
1. **New types in `core/types`:** `GapRegionKind {tail, silence, noise}`, `GapRegion`, `Gap`, `Segment`, `SegmentTimeline` (class with the partition invariant), `NoiseFloor`, `EffectiveGap`, `k_gap_mute_db`. A gap's id is `SegmentId{gap, "gap:<before>|<after>"}`.
2. **`AnalysisResult`** gains `std::optional<SegmentTimeline> timeline` and `std::optional<NoiseFloor> noise_floor`, empty by default. No `version` bump; outputs that serialise the analysis gain the fields.
3. **One new edit parameter:** `EditParam::gap_gain`, category `gap`, `ValueKind::decibels`, neutral 0 dB, range [`k_gap_mute_db`, +24 dB], ops set and add and reset. **Mute is `gap_gain` at `k_gap_mute_db`** (exactly silent), not a flag. Gate semantics: `silence` and `noise` regions change by the gain, `tail` regions and notes do not.
4. **Target-kind rule in `validate(EditSpec)`:** categories `gap` target gap segment ids only; every other existing category targets note ids only (reset of `gap`/`layout` aside as the code already allows). This is new: today nothing rejects a gap id on a note edit.
5. **`render_gap` is a free function** in `core/synthesis/gap` (as `stretch_span`, ADR-0023), called by `render_region` for the gaps with an edit.
6. **Deferred and not decided here:** layout and ripple (own note and ADR amending ADR-0023 §3), room tone, tighten/remove, breath types and detection, `layout`/`breath*` edit params.

## Alternatives considered
- Mute as a boolean flag: needs a new `ValueKind` and a bool in `EditValue` (render key, JSON, validate) for one state a gain can express. Rejected.
- Breath regions now: no consumer before Phase 6, and the detector's design has open findings. Rejected for this ADR.
- Timeline in `analysis/gaps`: `AnalysisResult` is layer 0 and cannot include it. Rejected.

## Consequences
- `validate(AnalysisResult)`, `analysis_json`, the plot output, `remap` (gap ids) and the render cache key change in the slices that add them; the golden/fingerprint outputs are re-baselined deliberately, with the change called out.
- The `gap` edit is the first edit whose target is not a note: every consumer of `EditTarget::segment_ids` that assumed notes (`effective_state`, `dirty_regions`, `remap`) is audited in slice 3.
- `tests/tolerances.h` is the owner's: the gate and floor tolerances are proposed in the note and added only with the owner's say-so.
