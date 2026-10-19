# Phase 2 review: Render pipeline skeleton (passthrough)

- **Status:** CLOSED 2026-10-19. CI green on `phase-2` (run 39255721167: Linux, macOS, Windows, tidy,
  format). The open items below are carried forward, not blockers.
- **Git tag:** `phase-2`

## Built
Design note: [`docs/design/phase2-render-skeleton.md`](../design/phase2-render-skeleton.md).
ADRs: 0006 (render API), 0007 (nlohmann/json), both accepted by the owner.

- **`core/dsp`:** `crossfade_gains`, sliced `crossfade` (equal-power, linear, log); `-ffp-contract=off`.
- **`core/synthesis/engine`, `passthrough`:** `SynthesisEngine` (name, version, settings key,
  `render_note`) and `PassthroughEngine`.
- **`core/render`:** `plan_blend` (one overlap rule), `render_region`, `RenderCache` (LRU), canonical
  cache key with note fingerprints.
- **`core/types`:** `ErrorCode::not_implemented`; `to_string`/`parse_*` for the edit enums.
- **`core/edits`:** `k_max_crossfade` (20 ms) with a static_assert against the dirty margin.
- **CLI:** `render`, `audition` (package of `AUDIO_VERIFICATION` §3), JSON scenarios
  (`cli/scenario`, `cli/scenario_json`), shared measurements in `cli/checks`.
- **Tests:** 303 CTest tests (237 unit, 58 contract, 8 audio) incl. property tests with 300 seeded
  cases for: empty edits == source, edits + full undo == source, samples outside `dirty_regions`
  bit-identical, passthrough == source, aligned halves == whole, determinism and cache hit == miss,
  blend-plan invariants.

## Gates
| Gate | Result | Evidence |
|---|---|---|
| Structure review of the design note | Round 1 CHANGES REQUIRED (6 blocking), round 2 CHANGES REQUIRED (3 blocking), all fixed before coding | design note, Review log |
| Build + tests, ASan/UBSan, TSan, release | 303/303 locally in each preset | local runs |
| clang-tidy, clang-format, include/layer check | clean on changed files (tidy found ~60 style findings in new code; all fixed) | local runs |
| CI (Linux, macOS, Windows, tidy) | Green (after fixing a Phase 1 contract test that probed one ulp past the range end on Linux, and missing includes) | run 39255721167 |
| Objective audio checks | Pass (null, undo-null, untouched, click calibration, sanity, determinism, length); audio-verifier report | `audition/phase2-passthrough/` |
| Human listening | not required: no engine changes audio yet (passthrough is bit-exact; the test gain engine is not in `src/`). A package was built for the CLI path | `audition/phase2-passthrough/INDEX.md` |
| Structure audit | not due (Phases 1, 5, 9, 12) | |

## Deviations from the specs
All in ADR-0006 / ADR-0007: `render_region` takes explicit inputs (no `Project` yet), `quality`
dropped, cache entries are `RenderResult`s with key-based invalidation, `synthesis/engine/` instead
of `interface/`, engine `version()`/`settings_key()`, edited-run crossfades, `not_implemented`,
nlohmann/json for the CLI.

## Objective numbers
Click detector: 0 detections for a 10 ms crossfade on a +12/-12 dB gain pair; detected for a 0 ms
splice (calibration pair, permanent tests). Audition fixture: DC -85.7 dBFS, 0 NaN/Inf. No timing
or pitch round-trip yet (no engine).

## Open issues carried forward
- **Phase 1 tag:** `phase-1` points at a commit whose CI failed on Linux/MSVC (missing
  `<algorithm>`). The fix is on `phase-2`. Moving the tag needs a force-push the owner has not allowed.
- **Click-detector thresholds** (ratio 6, window 128, floor 1e-3) are initial values calibrated on
  synthetic signals only; review before Phase 4 relies on them (owner: human, before Phase 4).
- **Crossfade default `equal_power`** can add up to +3 dB to correlated signals; decide at the Phase 4
  listening (owner: Phase 4).
- **Timing edits** return `not_implemented`; Phase 6 adds the stretch path and the TimeMap anchors to
  the cache key. **Order of stretch and pitch correction** (ADR-0005 §16) is still undecided.
- **Engine margin contract:** the renderer does not verify that an engine leaves the margin outside
  the note unchanged where it should; real engines (Phase 4) need a test for that (audio-verifier note).
- `RenderCache` locks and allocates; the real-time reader is Phase 9.
- `anno-cli audition` only has the passthrough engine; the first meaningful scenarios arrive with
  Phase 4 (no `tests/audio/scenarios/` exist yet).

## Split/join and cleanup done this phase
`cli/checks` extracted so `audition` and the tests share one implementation of the measurements;
`scenario_json` parse functions split to stay under the cognitive-complexity limit.

## Closing
Done: CI green, ROADMAP current phase set to 3, tag `phase-2`. Still the owner's call: moving the
`phase-1` tag (needs a force-push) and the open items above.
