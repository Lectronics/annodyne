# ADR-0024: Segment timeline types and the gap gain edit

- **Date:** 2026-10-22
- **Status:** accepted 2026-10-22 (owner: "fine")
- **Phase:** 5
- **Amends:** ADR-0008 (§1, §6: the timeline lives in `core/types`; gap ids; `AnalysisResult` gains `timeline` and `noise_floor`); ADR-0005 (record: new `EditParam` and a target-kind rule)

## Context
ADR-0008 decided that a track's source time is a partition into note and gap segments and that edits may target `SegmentId`. `docs/design/phase5-gaps-core.md` fixes the types against the code that exists and narrows Phase 5 to the gate.

## Decision
1. **New types in `core/types`:** `GapRegionKind {tail, protected_preroll, silence}`, `GapRegion`, `Gap`, `Segment`, `SegmentTimeline` (class with the partition invariant), `NoiseFloor`, `EffectiveGap`, `k_gap_mute_db`. A gap's id is `SegmentId{gap, "gap:" + <len>:<before> + <len>:<after>}` (length-prefixed note ids; empty side `0:`).
2. **`AnalysisResult`** gains `std::optional<SegmentTimeline> timeline` and `std::optional<NoiseFloor> noise_floor`, empty by default. No `version` bump; outputs that serialise the analysis gain the fields.
3. **One new edit parameter:** `EditParam::gap_gain`, category `gap`, `ValueKind::decibels`, neutral 0 dB, range [`k_gap_mute_db`, +24 dB], ops set and add and reset. **Mute is `gap_gain` at `k_gap_mute_db`** (exactly silent), not a flag. Gate semantics: `silence` regions change by the gain, `tail` and `protected_preroll` regions and notes do not.
4. **Target-kind rule in `validate(EditSpec)`** (exact table in the design note): `gap` -> gap ids, `layout` -> none yet, every other category -> note ids; applies to `segment_ids` and `orphaned_segment_ids` and to every op including reset, mixed targets refused; the one exception is a parameterless reset of `gap`/`layout`. Two existing tests are rewritten (named in the note).
5. **The gate is a gain curve** (`gate_curve`, `core/render/gate`) applied after `render_unedited` and `blend`: it works for any slope, needs no zone and no new engine target. A gap edit with no matching gap in the timeline (or no timeline) is `invalid_argument`. `RenderKeyInputs.gates`, key version 4, `RenderResult.gaps_rendered`, `passthrough` over notes and gaps, `RenderParams.gate`, and `dirty_regions` taking the `SegmentTimeline` are the public changes.
5a. **Gap ids** are length-prefixed (`gap:<len>:<before><len>:<after>`); remap uses the old and new timelines, not the text. `GapRegionKind` is `{tail, protected_preroll, silence}`; the gate leaves `tail` and `protected_preroll` untouched.
6. **Deferred and not decided here:** layout and ripple (own note and ADR amending ADR-0023 §3), room tone, tighten/remove, breath types and detection, `layout`/`breath*` edit params.

## Alternatives considered
- Mute as a boolean flag: needs a new `ValueKind` and a bool in `EditValue` (render key, JSON, validate) for one state a gain can express. Rejected.
- Breath regions now: no consumer before Phase 6, and the detector's design has open findings. Rejected for this ADR.
- Timeline in `analysis/gaps`: `AnalysisResult` is layer 0 and cannot include it. Rejected.

## Consequences
- `validate(AnalysisResult)`, `analysis_json`, the plot output, `remap` (gap ids) and the render cache key change in the slices that add them; the golden/fingerprint outputs are re-baselined deliberately, with the change called out.
- The `gap` edit is the first edit whose target is not a note: every consumer of `EditTarget::segment_ids` that assumed notes (`effective_state`, `dirty_regions`, `remap`) is audited in slice 3.
- `tests/tolerances.h` is the owner's: the gate and floor tolerances are proposed in the note and added only with the owner's say-so.

## Amendment 1 (2026-10-22): final region kinds, and what the gate mutes

`GapRegionKind` is `{tail, protected_preroll, silence, audible, sibilant, breath, plosive}` (appended in that order; nothing serialises the integer, JSON uses the names). **The gate mutes `silence` regions only**; every other kind is left alone, so a kind added later is protected by default. `audible` is a stretch above the floor by level, kind unknown; `sibilant`, `breath` and `plosive` come from the ADR-0017 features run over the gap frames (`docs/design/phase5-gap-consonants.md`, owner 2026-10-22: unvoiced consonants must be detected before anything can mute a gap). `build_segment_timeline` takes the evidence as a required argument (neutral evidence gives the level-only behaviour). `GapParams::min_gap` was dropped: every positive space between notes is a gap (a hole would break the partition). Sibling `core/analysis/*` targets may link each other (the orchestrator and `analysis/gaps` do); `architecture.md` table wording is read that way.

## Amendment 2 (2026-10-23): the public API the gate added (slices 7a-7e)

- `core/render/gate.h`: `GateParams {gate_fade}`, `GatedSpan`, `gated_spans(gap, map, rate)`, `gate_curve(gap, effective, map, rate, range, params)` (the `silence` regions only; ramps inside the region; none at the recording's ends).
- `RenderParams::gate`; `RenderResult::gaps_rendered` (and `passthrough` is false when a gap was gated); `RenderKeyInputs::gates` and `gate_fade_samples`, appended last, key version 4. A gap edit whose id is not in `analysis->timeline`, or any gap edit with no timeline, is `invalid_argument` naming the id in `render_region` and `dirty_regions` (orphaned ids are ignored).
- `DirtyParams::timeline`; a changed gap gain dirties `[gap.start, gap.end]`.
- `core/analysis/gaps`: `analyse_gaps` (floor, evidence, timeline in one call; `analyse()` and the scenario loader use it); `AnalyseParams::gaps` is a `GapAnalysisParams`. `analyse()` requires a source rate of at least 8 kHz.
- `tools/gap` (`gap_gain_edit`, `gap_mute_edit`, `gap_unmute_edit`) and `macros/strip_silence` (`strip_silence_edits`, gate mode) write `gap_gain` edits only.
- Scenario JSON: raw edits may target `{"gap": {"before": id|null, "after": id|null}}`; a hand-given scenario with a gap target gets its noise floor and timeline built from its audio. Analyse-mode scenarios address gaps by the analysed ids as they do notes.
