---
paths:
  - "src/**"
  - "tests/**"
  - "CMakeLists.txt"
  - "**/CMakeLists.txt"
  - "cmake/**"
---

# Architecture and dependency rules

## Layers

Each folder under `src/` is its own CMake library target. A target may only link targets in **its own layer or a lower one**. Breaking this is a blocking review finding.

| Layer | Targets | May use |
|---|---|---|
| 0 | `core/types`, `core/dsp` | standard library, FFT library |
| 1 | `core/analysis/*`, `core/tuning`, `core/models` | layer 0 |
| 2 | `core/edits`, `core/synthesis/*` | layers 0–1 |
| 3 | `core/timemap` | layers 0–2 (reads edits) |
| 4 | `core/render` | layers 0–3 |
| 5 | `tools/*`, `macros/*`, `features/*` | `core/types`, `core/edits`, `core/tuning`, read-only analysis results. **Not** `dsp`, `synthesis`, `render`. |
| 6 | `project` | layers 0–5 |
| 7 | `host/*`, `ui`, `cli/*` | everything |
| 0 + restricted | `core/io` (WAV file I/O, ADR-0001) | itself links only `core/types`. **May only be linked by layers 6–7 and tests.** |

`features/dna` and `features/sound_editor` may call analysis functions (layer 1) to request re-analysis, but still never synthesise audio themselves.

These rules are enforced at configure time by `cmake/MeloLayers.cmake` (`MELO LAYER VIOLATION: R<n>`) and at include level by `scripts/check_includes.py`; see `docs/design/phase0-foundations.md` §2.

## What goes where

- **Data with no behaviour** (a note, a pitch track, an edit) → `core/types`. Plain structs, no I/O, no DSP.
- **Signal maths reused by more than one module** (FFT, windows, filters, crossfades, resampling) → `core/dsp`.
- **Anything that measures audio** → `core/analysis/<stage>/`. Returns a new analysis object; never mutates its input.
- **Anything that produces samples** → `core/synthesis/<engine>/` behind the `SynthesisEngine` interface.
- **Anything that decides what to render, caches, schedules** → `core/render`.
- **Anything a user gesture or button does** → `tools/`, `macros/` or `features/`. Output is a list of `Edit`s.
- **Save/load, versioning, edition limits** → `project`.
- **JUCE, ARA, plug-in formats, audio devices** → `host/` only. **Drawing** → `ui/` only.

## Public API boundaries

- Each target exposes headers in its folder root; implementation details go in a `detail/` subfolder or an anonymous namespace and are not included from outside the target.
- Cross-module calls go through the functions listed in the architecture spec (section 4). Adding a new cross-module function needs a design note.
- Analysis results are shared as `std::shared_ptr<const T>` snapshots. Nobody holds a mutable reference to shared analysis.

## The core contracts (do not change without an ADR)

- `AnalysisResult`, `Note`, track types, `Edit`, `EditList`, `EffectiveNote`, `TimeMap`, `RenderResult` as defined in `docs/specs/melodyne-core-architecture.md`.
- `effective_state(note, edits)` is a pure fold in `seq` order up to the undo cursor.
- `render_region` output outside dirty regions equals the source bit-for-bit.
- Polyphonic change-only rendering: `out = original − Σ separated_original[edited] + Σ render(edited)`.
