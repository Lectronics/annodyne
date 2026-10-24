# ADR-0033: store the maximum voiced frequency per frame in NoiseTrack

- **Date:** 2026-10-23
- **Status:** proposed
- **Phase:** 6

## Context
ADR-0031 item 4 asks that pitch shifting move only the periodic part of breathy and mixed frames and leave the noise alone. The analysis already computes a per-frame maximum voiced frequency (`FrameMetrics::mvf_hz`) to classify `breathy` frames, then discards it. ADR-0017's amendment of 2026-10-21 deferred storing it. ADR-0031 listed WORLD D4C and a published estimator as candidate sources.

## Decision
- `NoiseTrack` gains `std::vector<Hz> max_voiced_hz` (per frame; 0 = no periodic part; empty = not measured). It is a derived curve for consumers: 0 on silence, plosive, sibilant, breath and probability-unvoiced frames, otherwise the raw `mvf_hz` clamped to `[mvf_floor, analysis ceiling]` and median-smoothed within runs of non-zero frames. The classifier keeps reading the raw metric; frame classes stay byte-identical.
- The value comes from the in-repo estimate (option A). WORLD D4C is a bench-only cross-check run outside the build; nothing from it enters `third_party` or any target. This departs from ADR-0031's candidate list on purpose (a 3 kHz aperiodicity grid is coarser than the estimate we already have).
- `validate(NoiseTrack)` checks length and finiteness/sign only (a track has no sample rate); the ceiling is guaranteed by `split_harmonic_noise`.
- The render key format version goes 8 -> 9; engine versions and `settings_key()` change when a consumer reads the field.
- Consumers (harmonic+noise engine, Rubber Band `ShiftScope::below_mvf`) are opt-in and off by default until the owner has listened. Detail: `docs/design/phase6-voiced-frequency.md`.

## Alternatives considered
- D4C in the build: tested code, but a dependency and coarser resolution for no gain over the existing estimate. Kept as a bench check.
- Drugman and Stylianou estimator: only if A measures badly.
- Recompute on demand in the engine: duplicates the analysis and cannot be cached with the note.

## Consequences
`Note` carries one more vector (re-baselines fingerprints; the cache key changes once). No audio changes until an option is switched on. New `split_band` dsp function. The abandoned branch `phase-6-core-only` also used key version 9; a revival must renumber.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §3 (`NoiseTrack`); ADR-0017's per-frame MVF clause is realised by this ADR.
