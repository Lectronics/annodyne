# Phase 5 review: Timing

- **Date closed:** 2026-10-23
- **Git tag:** `phase-5` (local; push, PR and merge wait for the owner). Interim tag `rubber-band` marks the commit where Rubber Band became the default engine.
- **Listening gate:** held; see [`phase5-listening.md`](phase5-listening.md).

## Built
Design notes (all marked as built): [`phase5-transient-pipeline`](../design/phase5-transient-pipeline.md), [`phase5-time-tools`](../design/phase5-time-tools.md), [`phase5-gaps-core`](../design/phase5-gaps-core.md), [`phase5-gap-consonants`](../design/phase5-gap-consonants.md), [`phase5-stretch-render`](../design/phase5-stretch-render.md), [`phase5-layout`](../design/phase5-layout.md), [`phase5-vibrato-stretch`](../design/phase5-vibrato-stretch.md); research input [`tsm-research`](../design/tsm-research.md). ADRs: 0020-0022 (onsets, default pitch engine), 0023 (stretched spans, amendments 1 and 3), 0024 (segment timeline and gap edits), 0025 (pinned-marker layout, amendments 1-3), 0026 (Rubber Band engine, amendment 1).

- **Onsets:** staged onset pipeline in `analysis/onsets`, the default of `analyse()` (ADR-0022).
- **Gaps:** `analysis/gaps` (noise floor, gap classification, `SegmentTimeline`), the gate in `core/render/gate`, `tools/gap`, `macros/strip_silence`, consonant-aware gap regions.
- **Time tools:** `tools/time`, `tools/attack_speed`; shift grid (`core/timemap/shift_grid`).
- **Layout:** `core/timemap` `layout_markers`, `solve_layout`, `build_layout_map`, `build_layout_time_map`, `changed_source_ranges`: pinned markers, unedited notes pinned and released to Flex when a pause is used up, Flex first (gaps under 100 ms of silence are never spliced; soft slope band 0.8-1.25), cut/insert in the silence nearest the edited note. `core/render/splice` (equal-power crossfades at cuts, fades at inserts). Cache key version 7 (local map spans).
- **Stretch:** `core/synthesis/stretch` with two engines: **Rubber Band R3 (vendored, default for renders; any channel count; 100 ms of context and slope-1 ramps at each end of a span; coherence-aware edge blend)** and the retained PSOLA alternative (mono; vibrato regeneration that keeps rate and depth; voiced-dropout bridging; noise-path edge anchoring). CLI scenario keys `stretch_engine`, `stretch_vibrato`.
- **Third party:** `third_party/rubberband` (GPL-2.0-or-later, unmodified), `cmake/AnnoRubberBand.cmake`, AnnoLayers allowlist.
- **Process:** CLAUDE.md rule 9 (reuse before building).

## Gates
| Gate | Result | Evidence |
|---|---|---|
| Structure review of design notes | Layout, vibrato and per-slice notes reviewed (vibrato: CHANGES REQUIRED, blocking findings 1-4 answered in revision 1) | design notes' review logs |
| Build + all tests + sanitizers + clang-tidy | debug-asan: see "Objective numbers"; clang-tidy clean on every changed file; `check_includes` 243 files, 0 violations | local runs 2026-10-23 |
| Objective audio checks | Every Phase 5 scenario passes the six structural checks (null, untouched, click, sanity, determinism, length), PSOLA and Rubber Band renders; `ctest -L audio` 14/14 | audio-verifier reports; `audition/phase5-*` reports |
| Human listening | held in words and times; Rubber Band accepted | `phase5-listening.md` |
| Structure audit #2 | **APPROVED WITH FIXES**, no blocking findings; fixes S1-S2, S9-S10, S12-S15, S18 (docs) done, `.claude/rules` change declined (not mine to edit on a subagent's say-so); S3-S7, S16, S19 carried | structure-reviewer report 2026-10-23 |

## Deviations from the specs
- Layout, gaps, splices, stretched base clip, shift grid: ADR-0023/0024/0025 (override spec sections 5.3-5.5).
- Stretch engine: Rubber Band as a vendored dependency and the render default: ADR-0026 (GPL consequence from ADR-0014).
- `docs/specs/annodyne-core-architecture.md` was edited on this branch for the Phase 5 plan; it names `plan_gap_layout`/`render_gap` that were not built as named (the gate and `build_layout_time_map` are the as-built entry points); the ADRs override it (audit S17, note).

## Objective numbers
- Vibrato (PSOLA, regenerated, synthetic `gap_fricative`): rate 5.4-5.7 Hz against 5.7 Hz, depth 34-37 against 37 cents (before: 9.1, 3.9 and 3.0 Hz at slopes 0.6, 1.4, 2.0).
- Real `vocadito_2` (PSOLA, HEAD of that round): no third-difference outlier above 8x local rms (source maximum 5.4x); level steps at the owner's times at most 1.8 dB, explained by the source envelope. Rubber Band renders: levels at span edges within 0.6 dB of the source (before the context/ramp change: dips of up to 9 dB at a span end); click detector 0 new detections in all four.
- Rubber Band: exact output length; 5.5 Hz vibrato measures 3.6 Hz after x1.5 (retimed, not preserved); deterministic per build.
- Tests: **980/980** in the debug-asan build on the closing commit (979 in the full run, plus `anno_cli.render_timing_edit_exits_1`, whose scenario now asks for the PSOLA engine because Rubber Band, the default, renders it; re-run and passing). Release: stretch, render and layout tests pass.

## Open issues carried forward
| Item | Owner phase |
|---|---|
| Pitch-edit engine: SPTK/REAPER marks (ADR-0019) or Rubber Band pitch shifting, A/B by ear | 6 or 7 |
| Rubber Band renders a span whole per requested range: needs a per-span cache for interactive block rendering | when the editor renders blocks (7+) |
| Rubber Band retimes vibrato (the PSOLA path keeps it); vibrato tools need the regenerated contour exposed | 6 |
| Audit S3-S7: `StretchParams` PSOLA-only fields in their own struct and one default engine; slope types as `Ratio` with one definition of the limits; `LayoutParams::note_margin`/`splice_fade` duplicate render params; shift step derived inside `build_layout_time_map`; `GapEditKind` enum | 6 |
| Audit S16: wire `changed_source_ranges` into the caller of `dirty_regions` (it still dirties origin-to-end for timing) | 7 |
| Third-difference "tick" check next to the click detector (the detector missed two 14x steps); threshold from the source maximum | owner's OK needed |
| Tolerances proposed for `tests/tolerances.h` (owner's file) | owner |
| Spread of a change over neighbouring notes (soft slope band 0.8-1.25): tune by ear | owner |
| Vowel/consonant-aware blending and a periodicity-checked resynthesis (owner idea): adopt existing code first (rule 9) | 8+ |
| Room tone for inserts (slice 9); 10-minute timing test of the evidence pass; no-timeline `TailPolicy` default; stereo PSOLA | later |
| Tempo, key, chords: adopt c4dm qm-dsp / qm-vamp-plugins / nnls-chroma (vendored, ADR) | 8 |
| Pre-release licence decision must now name Rubber Band as well as c4dm/pyin (GPL-2.0-or-later, no closed edition) | before release |
| Stretched audio is bit-identical per platform only (vDSP on Apple, built-in FFT elsewhere) | release |
| CI on the push of `phase-5` (earlier runs showed "cancelled") | at push |

## Split/join and cleanup done this phase
Moved the anchor machinery shared by `build_time_map` and the layout into `core/timemap/detail`; moved the grain/mark/period code shared by PSOLA and stretch into `core/synthesis/grain`; removed the 4-8 kHz gap-evidence fallback (`neutral_gap_evidence` is now a test helper); the PSOLA stretch path, vibrato clock and class map are retained as the alternative engine (audit section 5).
