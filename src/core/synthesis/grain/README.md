# core/synthesis/grain

Marks, period curve, fractional-position reader and grain overlap-add shared by the PSOLA engine and the stretch renderer (ADR-0023, `docs/design/phase5-stretch-render.md`). Moved out of `psola_engine.cpp` unchanged: the pinned-hash test in `tests/unit/psola_engine_test.cpp` shows the PSOLA output is bit-identical before and after (macOS).

- `signal.h` — `Signal`, Catmull-Rom reads at fractional global sample positions; samples outside count as zero.
- `period_curve.h` — `PeriodCurve` over (position, absolute cents) points: linear in cents, held beyond the ends, period clamped to 40..2000 Hz.
- `marks.h` — `analysis_marks`: marks one period apart, each placed where the waveform best matches the previous mark's (the Phase 4 fix for the "hiccups").
- `overlap_add.h` — `Grain`, `Accumulator`, `add_grain` (two-period Hann grains).

Built with `-ffp-contract=off` (reproducible renders, ADR-0006); `signal.h` is header-only, so each includer needs the same flag (psola and stretch have it). Links only `core/types`.
