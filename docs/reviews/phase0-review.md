# Phase 0 review: Foundations

- **Date closed:** 2026-10-19
- **Git tag:** `phase-0`

## Built
Design note: [`docs/design/phase0-foundations.md`](../design/phase0-foundations.md).

- **Build system.** CMake ≥ 3.25 with presets `debug-asan`, `debug-tsan` and `release` (Ninja,
  `build/<preset>`). Warnings are errors on GCC/Clang and MSVC.
- **Layer enforcement.**
  - `cmake/AnnoLayers.cmake` holds the layer table for every spec §6 folder and checks rules R0–R6
    at configure time. All violations are reported together, each prefixed
    `ANNO LAYER VIOLATION: R<n>`.
  - `scripts/check_includes.py` applies the same rules to `#include` lines. It also enforces the
    `detail/` boundary and the ADR-0003 name restrictions.
- **`core/types`** (layer 0):
  - `units.h`: `Quantity`, the `TimePoint` axes (`SourceTime`, `OutputTime`), `Seconds`, `Samples`,
    `Cents`, `Decibels`, `Hz`, `Ratio`, `LinearGain`, `LoudnessUnits`, and explicit conversions.
  - `time_range.h`: `TimeRange<Axis>` and the serialised-seconds pair.
  - `error.h`, `result.h`: `ErrorCode`, `Error`, `Unexpected`, `fail()` and `Result<T>`.
  - `contract.h`: `ANNO_EXPECTS` and `ANNO_ASSERT`.
  - `audio_buffer.h`: `AudioBuffer`, `PositionedAudio`, `output_range()` and `source_range()`.
  - `audio_source.h`: `AudioSource` (an immutable shared backend with a content-hash `SourceId`).
- **`core/io`** (ADR-0001): `read_wav_info`, `read_wav` (PCM16/24/32, float32, extensible) and
  `write_wav` (float32).
- **CLI:** `anno-cli info <file>`. The commands live in the `anno_cli_commands` library so they can
  be tested.
- **Tests:** `tests/tolerances.h` holds every AUDIO_VERIFICATION §1 tolerance (human-gated) plus the
  engineering tolerances. There are 83 CTest tests, labelled `unit` (56) and `contract` (27).
- **Tooling:** `.clang-format`, `.clang-tidy` and GitHub Actions CI. Format and tidy in CI are
  pinned to LLVM 23.1.3, the version used locally.

## Gates
| Gate | Result | Evidence |
|---|---|---|
| Structure review of design notes | APPROVED WITH FIXES (round 3), all fixes applied | Review log in the design note |
| Build + all tests + sanitizers + clang-tidy | Pass: Linux, macOS, Windows; ASan/UBSan, TSan, release; format; tidy | GitHub Actions run 39204762343 |
| Forbidden link fails the build | Pass | 9 negative fixtures each fail with their exact rule; 3 positive controls configure cleanly. A manual proof in the real tree (adding `core_types → core_io`) gives R3, R4 and R5, and a plain `add_library` gives R0. |
| Objective audio checks | n/a: no rendered audio yet | — |
| Human listening | n/a | — |
| Structure audit | Not scheduled for Phase 0 (first audit at Phase 1 close) | — |

## Deviations from the specs
- **ADR-0001:** new `core/io` target. Its own links follow layer 0, and only layers 6–7 and tests may
  link it.
- **ADR-0002:** `AudioBuffer` has no `start_time`. `PositionedAudio<Axis>` replaces it, and
  `RenderResult.out_range` is derived from the buffer rather than stored.
- **ADR-0003:** units vocabulary. Adds `Ratio`, `LinearGain`, `LoudnessUnits`, `TimeRange`, the
  `unchecked_*` and serialised-seconds restrictions, and the `_prob`/`_rad` suffixes.
- **ADR-0004:** test folders follow `CLAUDE.md`, not spec §6.
- **Not an ADR, recorded here:**
  - The serialised-seconds functions live in `time_range.h` instead of `units.h` (the design note's
    §4 placement), so `units.h` does not depend on `result.h`.
  - The project was renamed Melo → Annodyne (code prefix `anno`). References to Celemony's Melodyne
    in the specs are unchanged.

## Objective numbers
No audio processing yet. Unit-conversion accuracy is checked against `tests/tolerances.h`:
- cents: 1e-9 absolute;
- Hz and gain: 1e-12 relative;
- seconds: 1e-12 s.

Float32 WAV write followed by read is bit-exact.

## Open issues carried forward
- **Phase 1:** structure audit #1 covers every public header, including Phase 0's.
- **Phase 1:** `TimeMap` is the only planned user of `unchecked_*` outside `core/types`, and the
  include checker already allows it there.
- **Phase 2:** extend the external-library allowlist for the FFT library in `core/dsp` through a
  design note.
- **Phase 9:** add the ARA-backed `AudioSource` behind the existing interface. The interface is
  deliberately not abstract yet (join rule #5).
- **Process:** the Phase 0 implementation was committed as one commit instead of small slices. Later
  phases commit per slice.

## Split/join and cleanup done this phase
- `wav.cpp`:
  - `locate_data` (clang-tidy cognitive complexity 48) was split into `check_riff_header`,
    `parse_fmt_chunk`, `parse_sample_format` and the chunk walk (split rule #3).
  - The `Reader` wrapper around `std::ifstream&` was removed in favour of free functions (join rule
    #2, middle man).
- `cli`: compute and print are separated (`make_info_report` / `format_info`), and the commands live
  in a library separate from `main` (split rule #4).
