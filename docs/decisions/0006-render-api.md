# ADR-0006: Phase 2 render API

- **Date:** 2026-10-19
- **Status:** accepted (owner approved starting Phase 2; the deviations below are implementation choices
  the spec does not make, flagged for review at the Phase 2 close)
- **Phase:** 2

## Context
The spec (§4.5, §4.6, §5.5) gives Python-style signatures for `render_region`, `RenderCache` and the
`SynthesisEngine`, and assumes a `Project` (Phase 7) and analysis (Phase 3) that do not exist yet.

## Decision
1. `render_region(project, track_id, out_start, out_end, quality)` becomes
   `render_region(RenderInputs, OutputRange, const SynthesisEngine&, RenderParams)` returning
   `Result<shared_ptr<const RenderResult>>`. `RenderInputs` = source, analysis snapshot, edit list.
   `quality` is dropped until an engine needs it.
2. `RenderCache.get/put` hold `RenderResult` entries (the cache key is a canonical byte string, see the
   design note), not bare buffers. Invalidation is key-based: an edit that changes an `EffectiveNote`
   changes the key; undo returns to the old key. `dirty_regions` is not consumed by the renderer; it
   is the independent oracle for the "untouched region" contract test and what a UI uses to repaint.
3. The synthesis interface folder is `core/synthesis/engine/` (spec: `interface/`). `SynthesisEngine`
   gains `version()` and `settings_key()` for the cache key, and is always asked for a note's whole
   blend zone (the renderer slices), so engines need no sub-range consistency.
4. Crossfade: span kernel with ramp offset/length (not `crossfade(a, b, shape, length) -> AudioBuffer`
   of the ADR-0002 table). Adjacent edited notes whose margins overlap are blended engine-to-engine
   (edited runs), not each through the original. `k_max_crossfade` = 20 ms; `dirty_regions` margins
   must be at least that plus one sample.
5. `ErrorCode::not_implemented`: a valid request that this build or engine cannot do yet. Used for
   non-identity TimeMaps until Phase 6.
6. A test-only `GainTestEngine` lives under `tests/support/`, never in `src/`, and is removed in
   Phase 4.
7. Phase 2 rejects timing edits (`not_implemented`); stretch is Phase 6. The cache key will then include
   the TimeMap anchors.

## Alternatives considered
- Keep `get/put` of `AudioBuffer`: loses `notes_rendered`/`passthrough` on a hit.
- Invalidate by `dirty_regions` ranges: needs shared mutable state between edit and cache layers.
- Per-note ramp to the original everywhere: passes legato phrases through the original between notes.

## Consequences
Phase 4 adds real engines behind the same interface; Phase 6 adds the stretch path and the key field;
Phase 9 adds a real-time reader next to the mutex cache.

## Spec updates
Pointers in `annodyne-core-architecture.md` §4.5, §4.6, §5.5 and the folder tree.
