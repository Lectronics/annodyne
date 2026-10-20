# ADR-0011: An internal activity stage in Phase 3

- **Date:** 2026-10-19
- **Status:** accepted 2026-10-19 (owner chose "option 1": add it now; breaths later)
- **Phase:** 3

## Context
On 40 real vocals (Vocadito) the Phase 3 analyser found 1353 of ~2200 annotated notes because the pitch tracker called 40 % of the voiced frames unvoiced (`beta_mean` 0.1, tuned on synthetic tones). Loosening the voicing prior recovers them but lets silence and noise be called voiced (false-voiced 0.9 % to 4.8-7.5 %). ADR-0008 and the ROADMAP place noise-floor and gap detection in Phase 5 and breath detection in Phases 5-6, and the owner had asked that none of it start early. The owner then asked to pull the *minimum* forward: label silence first, track pitch only in active audio, leave breaths for later.

## Decision
1. Add `core/analysis/activity` (layer 1): `detect_activity` (noise floor, hysteresis, per-frame `active` flag on the pitch grid) and `gate_pitch` (silent frames forced unvoiced). Add `core/dsp/filter` (`zero_phase_bandpass`) used by both it and the pitch front end.
2. The stage is **internal**: `AnalysisResult`, `Note`, `FrameClass` usage and the edit model are unchanged. No gap, breath, segment-timeline, layout or edit-category behaviour is implemented. Consonants and breaths are not classified.
3. Phase 5 (`analysis/gaps`, noise floor) **extends or replaces** this stage rather than adding a second noise-floor estimator; that reconciliation is part of the Phase 5 design note.
4. `gate_pitch` returns a new `PitchTrack`; `detect_activity` takes the already-read `MonoAudio`; `--json` is not extended (no public silent-range list).
5. The pitch estimator's `version()` becomes 2 (band-pass front end); `AnalyseParams` gains `ActivityParams`.

## Alternatives considered
- Wait for Phase 5: no work now, but Phase 4 (pitch editing) would sit on a tracker that misses 40 % of voiced audio.
- Raise the voicing prior only: simplest, but false voiced frames in silence rise to 5-8 %.
- Implement the public `SegmentTimeline` now: closer to the end state but it pulls gap classification, layout and edit categories into Phase 3, which the owner ruled out.

## Consequences
New files and tests per `docs/design/phase3-activity.md`; the ROADMAP Phase 3 list gains the activity stage; Phase 5's design note must reconcile with it. The Phase 3 golden gate is now measured on real vocals (separate owner decision on thresholds).

## Spec updates
The specs are read-only; this ADR overrides the architecture spec's folder list by adding `core/analysis/activity` and `core/dsp/filter` (as ADR-0008 did for its additions). `docs/ROADMAP.md` Phase 3 gets one line pointing here; the Phase 5 design note must reconcile the per-gap `floor_level` of `silence-and-gaps.md` §2 with this stage's global quantile floor.
