# ADR-0009: pocketfft as the FFT library

- **Date:** 2026-10-19
- **Status:** accepted 2026-10-19 (owner chose pocketfft)
- **Phase:** 3

## Context
`core/dsp` needs a real FFT for the STFT, the YIN difference function and the onset detector. The architecture rules allow "an FFT library" in layer 0 but none is chosen. Phase 3 also needs any length (e.g. 2048 and non-power-of-two sizes for resampling filters).

## Decision
Use **pocketfft** (C++ header-only version `pocketfft_hdronly.hpp`, BSD-3-Clause, `mreineck/pocketfft`, branch `cpp`, **pinned to commit `4d865cba24359b05b37da4e75aee24498c3b78e7`** in `cmake/AnnoPocketfft.cmake`), fetched with FetchContent like Catch2 and nlohmann/json. It may be linked **only by `core/dsp`** (`cmake/AnnoLayers.cmake` external allowlist), privately: no pocketfft header appears in a public anno header; `RealFft` in `core/dsp` wraps it.

Details: `cmake/AnnoPocketfft.cmake` creates the INTERFACE target `anno_pocketfft` (include directory marked SYSTEM so `-Werror -Wconversion -Wshadow` do not fire in third-party headers; compile definition `POCKETFFT_NO_MULTITHREADING`, so no thread dependency and no `Threads::Threads` allowlist entry). The external allowlist in `cmake/AnnoLayers.cmake` gains exactly one clause: `folder STREQUAL core/dsp` and dep `anno_pocketfft`. `scripts/check_includes.py` ignores angle-bracket third-party includes (checked; a fixture proves an analysis target including `<pocketfft_hdronly.hpp>` is still rejected by the CMake rule). The licence text is added to `THIRD_PARTY.md`. pocketfft's function API (`r2c`/`c2r`) keeps its plan cache behind an internal mutex: `RealFft` is thread-safe but not real-time safe.

## Alternatives considered
- Own radix-2 FFT: power-of-two only, slower, we would own its correctness. Rejected.
- FFTW: GPL, rejected for licence. KissFFT, PFFFT: fewer lengths or SIMD-only constraints; pocketfft is simpler to vendor and tested.

## Consequences
New third-party dependency in the build (fetched at configure time; `FIND_PACKAGE_ARGS` not available, so an offline build needs the FetchContent cache). The layer check gets an allowlist entry and a fixture (`allow_dsp_pocketfft`, `deny_analysis_pocketfft`). Determinism per build still holds (`-ffp-contract=off`).
