# Annodyne C++ Implementation Roadmap

This is the plan Claude Code follows to build the editor. It is ordered so that **structure is settled and checked before code depends on it**, and so that **something audible exists early** and gets listened to at every stage.

**Current phase:** Phase 3 *(update this line when a phase closes)*

---

## How every phase runs

Each phase uses the same seven-step cycle. Steps 2, 5 and 7 are gates: work does not continue until they pass.

| Step | What happens | Output |
|---|---|---|
| **1. Plan** | Start in plan mode. Read the specs for this phase. List the types, functions and files to create. For each public function: parameters, return type, error cases, units, time axis, thread/RT safety. | Design note(s) in `docs/design/` from `DESIGN_NOTE.md` |
| **2. Structure review** *(gate)* | `structure-reviewer` subagent checks the design notes against the specs, `architecture.md` and `coding-standards.md`. Fix every blocking finding; re-review. | Review findings appended to the design note |
| **3. Contracts** | Write headers and tests first. Tests compile and fail. Contract tests for invariants. | Headers + failing tests |
| **4. Implement** | One function/class at a time. Build and run its tests after each. Commit small. | Passing tests |
| **5. Verify** *(gate)* | Full build with sanitizers, all tests, clang-tidy. If audio changes: `audio-verifier` runs objective checks and builds audition packages. | Verification report |
| **6. Listen** | A human listens to the audition package and fills in the listening sheet. *(Audio phases only.)* | `docs/reviews/<phase>-listening.md` |
| **7. Close** *(gate)* | Phase review from `PHASE_REVIEW.md`: what was built, deviations from the specs (with ADRs), open issues, structure audit result. Update "Current phase" above. Tag the commit `phase-N`. | `docs/reviews/<phase>-review.md`, git tag |

**Structure audits** (a full read-through of all public headers by `structure-reviewer`, not just the new ones) happen at the close of Phases 1, 5, 9 and 12. They look for drift: duplicated concepts, classes that should be split or joined, layer violations, inconsistent return types.

**If something in a spec is wrong**, stop, explain the problem, propose a fix, and write an ADR once a human agrees. Never silently diverge.

---

## Phase 0 — Foundations

**Goal:** a repo where the right thing is the easy thing.

- CMake ≥ 3.25 with presets: `debug-asan`, `debug-tsan`, `release`. One library target per `src/` folder with link rules enforcing the layers in `.claude/rules/architecture.md`.
- `.clang-format`, `.clang-tidy`, warnings as errors, CI (build + test on Linux, macOS, Windows).
- Catch2 v3, `tests/tolerances.h`, test labels `unit`, `contract`, `audio`, `golden`, `torture`.
- `core/types/units.h`: strong types `SourceTime`, `OutputTime`, `Seconds`, `Samples`, `Cents`, `Decibels`, `Hz` with explicit conversions only.
- `anno::Result<T>` (expected-style) and `anno::Error`.
- WAV read/write (float32), `AudioSource` with read-only access, `AudioBuffer`.
- `anno-cli` skeleton with `info <file>`.
- A layer-violation test: a CMake check (or a script in CI) that fails if a forbidden link is added.

**Verify:** CI green on all platforms; a deliberately forbidden link fails the build.

**Starter prompt:**
> Read CLAUDE.md, docs/ROADMAP.md (Phase 0) and docs/specs/annodyne-core-architecture.md sections 1–3 and 6. Enter plan mode and propose the Phase 0 repository setup: CMake targets, presets, units.h design and Result<T>. Write the design note, then have structure-reviewer review it before creating files.

---

## Phase 1 — Data model and edit contracts (no DSP)

**Goal:** the non-destructive model exists and is proven correct before any audio code depends on it.

- All types from architecture spec §3: tracks, `Note`, `AnalysisResult`, `Edit`, `EditTarget`, `EditList`, `EffectiveNote`.
- `EditList`: append-only, `add`, `undo`, `redo`, `reset(note_ids, categories)`, grouping, cursor.
- `effective_state(note, edits)`: the fold, with `set`/`add`/`scale` semantics per category.
- `TimeMap`: anchors, forward and inverse maps, slope, monotonicity enforcement, consonant protection.
- `dirty_regions(old, new)`.
- `remap_after_reanalysis` (stub behaviour defined and tested: keep, split, merge, orphan).

**Contract tests (property-based where possible):**
- `effective_state` with no edits equals the analysed values.
- `add` edits commute; `set` overrides earlier edits in its category; undo restores the previous effective state exactly.
- Any random sequence of edits followed by full undo gives the original state.
- `TimeMap` is strictly monotonic, `M⁻¹(M(s)) == s` within 1e-9 s, identity when there are no time edits.
- `dirty_regions` covers every note whose effective state changed, and nothing else beyond crossfade margins.

**Exit gate:** structure audit #1 (all headers). This is the most important review in the project: every later phase builds on these types.

---

## Phase 2 — Render pipeline skeleton (passthrough)

**Goal:** the plumbing from edits to audio, proven bit-exact before any synthesis exists.

- `SynthesisEngine` interface; a `PassthroughEngine` that returns the source segment.
- `render_region`, `RenderResult`, `RenderCache` (keyed by source, analysis version, relevant edits, engine settings).
- Crossfade utilities (equal-power, linear, log) in `core/dsp`.
- *(Addendum, ADR-0008)* The pipeline handles `warp`, `cut` and `insert` spans: slope-1 warps copy shifted source samples, `insert` spans render digital silence, `cut` spans render nothing. `TimeMap` is the span model; `SegmentId` and the generalised `EditTarget` exist; the new edit categories exist as enum values only. A warp with slope ≠ 1 is still `not_implemented` (Phase 6).
- `anno-cli render <source> --edits edits.json --out out.wav` and `anno-cli audition` (writes the package layout from `AUDIO_VERIFICATION.md` §3).
- Objective check framework in `tests/audio/`: null test, untouched-region test, click detector, sanity, determinism.

**Verify:** null and undo-null tests bit-exact; a deliberate gain-only edit (a temporary test engine that applies gain) passes the untouched-region and click tests.

---

## Phase 3 — Monophonic analysis

**Goal:** accurate notes from a vocal. No editing of sound yet.

- `core/dsp`: STFT/ISTFT, windows, resampling, K-weighting.
- `analysis/pitch`: pYIN-style DSP estimator first (no ML dependency), Viterbi smoothing, sub-cent refinement, alternates. Interface ready for a neural estimator later.
- `analysis/onsets`: SuperFlux; vibrato suppression.
- `analysis/segmentation`: HMM note tracker fusing pitch, onsets and energy.
- `analysis/perceived_pitch`: stability-weighted pitch; `decompose_pitch_curve` (centre/drift/mod).
- `analysis/loudness`: per-note loudness and amplitude curve.
- `analysis/orchestrator`: `analyse()` for the `melodic` algorithm.
- `anno-cli analyse <file> --json` and a simple plot export (CSV or PNG) of pitch curve + notes for humans to inspect.

**Verify:**
- Synthetic signals: pure tones, vibrato tones, glides, harmonic tones in noise → pitch within ±3 cents (10-cent RPA ≥ 95% on clean synthetic).
- Golden vocals with hand-checked note lists: onset ±20 ms, no octave errors on the golden set.
- Human check: look at the exported note plots for 5 vocal files and mark wrong notes.

---

## Phase 4 — First audible edit: pitch

**Goal:** move a note's pitch and have it come out right. This is the first listening gate.

- `analysis/partials` (f0-guided harmonic tracking, non-stationary estimator), `analysis/harmonic_noise` (residual + frame classes), `analysis/envelope` (True Envelope).
- `synthesis/harmonic_noise` engine: rebuilds the edited pitch curve, re-reads harmonic amplitudes from the envelope, keeps noise unshifted, crossfades at note edges.
- `synthesis/psola` as a second engine for comparison on clean voice.
- Main Tool pitch drag and snap in `tools/main_tool` (writes `pitch` edits only).

**Verify:** pitch round-trip, vibrato preservation, formant preservation, noise-not-shifted, click detector, untouched regions. Audition package `phase4-pitch` with both engines side by side.

**Listening gate #1:** a human compares engines and picks a default (ADR).

---

## Phase 5 — Timing

**Goal:** move and stretch notes cleanly.

- TimeMap-driven rendering: note-aware stretching with consonant/attack protection; `synthesis/transient` for attacks.
- `tools/time` (moves, lengths, handles), `tools/attack_speed`, `tools/note_separation`.
- `macros/quantize_time` (grid), with perceptual onsets.
- *(ADR-0008)* Segment timeline (`analysis/gaps`): noise floor, gap detection and classification, `SegmentTimeline` in `AnalysisResult`. Gap layout (`solve_layout`, `plan_gap_layout`, `build_time_map` on spans; shift first, stretch only when a gap is exhausted). `gap` and `layout` edits, `tools/gap`, `macros/strip_silence`, `render_gap`. Breath detection (`analysis/breaths`). Note stretching (`docs/design/note-stretching.md`).

**Verify:** timing round-trip, length check, click detector, no flams in overlap tests. Audition `phase5-timing`.

**Exit gate:** listening sheet + structure audit #2.

---

## Phase 6 — Expression tools and macros

- `tools/pitch` (transitions), `pitch_modulation`, `pitch_drift`, `formant`, `amplitude`, `fade`, `sibilant`.
- `macros/correct_pitch`, `macros/leveling`.
- *(ADR-0008)* Breath chain (`core/dsp/dynamics`: `Compressor`, `Limiter`, `HighPass`; `render_breath_event`), the `breath`, `breath_chain` and `breath_assignment` edits and the breath macros (`features/breaths`: `BreathLevel`, `MuteSmallBreaths`, `RemoveBreaths`).
- `core/tuning`: chromatic and scale grids, `target_pitch`.

**Verify:** modulation/drift scaling, formant round-trip, level round-trip, noise gain applies only to noise frames. Audition `phase6-expression` covering every tool.

**Listening gate #2.**

---

## Phase 7 — Project persistence and re-analysis

- `project/`: save/load project, append-only edit log (`.jsonl`), analysis cache, versioning, edition limits.
- `tools/note_assignment`: octave moves, split/merge, re-analysis with `remap_after_reanalysis`.
- Selective reset by category.

**Verify:** save → load → render is bit-identical to before saving; re-analysis keeps edits on unchanged notes; orphaned edits are reported, never dropped.

---

## Phase 8 — Harmony, tuning and tempo

- `analysis/harmony`: tuning reference, key, scale extraction, chord recognition.
- `analysis/rhythm`: beats, tempo curve.
- `features/scales`, `features/chords`, `features/tempo`; adaptive just intonation solver in `core/tuning`.

**Verify:** key/chord accuracy on a small labelled set; tempo-flatten audition; JI audition on sustained chords (human check for beating).

---

## Phase 9 — Host integration and real-time playback

- `host/plugin` (VST3/AU/AAX via JUCE), `host/ara` (ARA 2 document controller and playback renderer), `host/standalone`.
- Background render scheduler (priority near the playhead), lock-free cache publication, fallback to original audio when a block is not ready.

**Verify:** RT-safety tests (no allocation in `process`), TSan stress test (edit while playing), performance budget recorded, plug-in validators (pluginval, auval). Manual DAW test in at least two ARA hosts.

**Exit gate:** structure audit #3.

---

## Phase 10 — Note editor UI

- Blobs, pitch curves, grids, tool palette, inspectors, undo UI, playback cursor. The UI only reads analysis/effective state and sends gestures to tools.
- *(ADR-0008)* Breath Lane UI (`ui/breath_lane`), and the playhead and gap display treating `insert` spans as generated audio.

**Verify:** UI never blocks the audio thread; every gesture results in the same edits as the equivalent CLI scenario (so UI bugs can be reproduced headless).

---

## Phase 11 — Polyphonic (DNA)

- `analysis/polyphonic` (transcription, starting with a DSP multi-f0 + NMF approach; neural transcriber behind the estimator interface), `analysis/separation` (score-informed NMF, residual).
- Change-only polyphonic rendering in `core/render`.
- `features/dna`, ghost notes in Note Assignment.

**Verify:** unedited notes and residual bit-exact; edited-note bleed measured; polyphonic torture scenarios. Audition `phase11-dna`. **Listening gate #3.**

---

## Phase 12 — Studio features

- `features/multitrack` (cross-track selection, cross-track macros, quantize to reference track with DTW).
- `features/sound_editor` (harmonic gains, spectral macros, timbre transfer).
- `features/midi_export`, `features/clipboard` (virtual notes, harmonies).

**Exit gate:** listening sheet + structure audit #4.

---

## Phase 13 — Optional neural models

- `core/models` with ONNX Runtime: neural pitch estimator, neural transcriber, optional neural vocoder engine.
- Each model sits behind an existing interface, is compared against the DSP version on the same scenarios, and only becomes default through an ADR with objective numbers and a listening sheet.

---

## Working tips for long sessions

- Use one Claude Code session per step or per slice; start each by reading `CLAUDE.md`, this file's current phase and the relevant design note.
- Use `/clear` (or a new session) between unrelated slices so old context doesn't leak into new decisions.
- Keep the design note as the memory of *why*; keep the code as the memory of *what*.
