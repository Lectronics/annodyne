# core/analysis/gaps

The gap work of Phase 5 (layer 1; links `core/types`, `core/dsp`, `analysis/activity`).
Design: `docs/design/phase5-gaps-core.md`, ADR-0024.

- `estimate_noise_floor.h` — the noise floor over time (a low percentile of the frame levels outside
  the notes, in a sliding window) and the average floor spectrum in log-spaced bands. One level
  convention: RMS dBFS (a full-scale sine reads -3.01 dB).
- `classify_gap_frames.h` — the ADR-0017 features (centroid, ZCR, flux, high-band share, burst test)
  run over the whole recording as unvoiced, kept as a class and an excess over the floor per frame
  (`GapEvidence`); `neutral_gap_evidence` for sources below 8 kHz. Links `analysis/harmonic_noise`.
- `build_segment_timeline.h` — the recording as notes and the gaps between them (every positive space
  is a gap; regions in order: `tail` (the previous note's decay while the level is above floor + margin, capped), `sibilant` / `breath` / `plosive` (classified runs of the evidence above the event margin; a
  sibilant or plosive cuts a tail short, a breath inside a tail is the tail), `audible` (any later
  run above floor + margin, wherever it sits, that no class covers), `silence`, and the
  `protected_preroll` before the next note; a gap's floor is the median of the floor curve over it). `analyse()` stores both results in `AnalysisResult`.
