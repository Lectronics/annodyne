# core/analysis/gaps

The gap work of Phase 5 (layer 1; links `core/types`, `core/dsp`, `analysis/activity`).
Design: `docs/design/phase5-gaps-core.md`, ADR-0024.

- `estimate_noise_floor.h` — the noise floor over time (a low percentile of the frame levels outside
  the notes, in a sliding window) and the average floor spectrum in log-spaced bands. One level
  convention: RMS dBFS (a full-scale sine reads -3.01 dB).
- `build_segment_timeline.h` — the recording as notes and the gaps between them (every positive space
  is a gap; one `silence` region per gap until the tail and preroll regions of slice 6; a gap's floor
  is the median of the floor curve over it). `analyse()` stores both results in `AnalysisResult`.
