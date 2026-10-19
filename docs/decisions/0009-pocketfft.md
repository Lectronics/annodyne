# ADR-0009: pocketfft as the FFT library

- **Date:** 2026-10-19
- **Status:** accepted 2026-10-19 (owner chose pocketfft)
- **Phase:** 3

## Context
`core/dsp` needs a real FFT for the STFT, the YIN difference function and the onset detector. The architecture rules allow "an FFT library" in layer 0 but none is chosen. Phase 3 also needs any length (e.g. 2048 and non-power-of-two sizes for resampling filters).

## Decision
Use **pocketfft** (C++ header-only version, BSD-3-Clause, `mreineck/pocketfft`, branch `cpp`, pinned by commit in `cmake/AnnoPocketfft.cmake`), fetched with FetchContent like Catch2 and nlohmann/json. It may be linked **only by `core/dsp`** (`cmake/AnnoLayers.cmake` external allowlist), privately: no pocketfft header appears in a public anno header; `RealFft` in `core/dsp` wraps it.

## Alternatives considered
- Own radix-2 FFT: power-of-two only, slower, we would own its correctness. Rejected.
- FFTW: GPL, rejected for licence. KissFFT, PFFFT: fewer lengths or SIMD-only constraints; pocketfft is simpler to vendor and tested.

## Consequences
New third-party dependency in the build (fetched at configure time; `FIND_PACKAGE_ARGS` not available, so an offline build needs the FetchContent cache). The layer check gets an allowlist entry and a fixture (`allow_dsp_pocketfft`, `deny_analysis_pocketfft`). Determinism per build still holds (`-ffp-contract=off`).
