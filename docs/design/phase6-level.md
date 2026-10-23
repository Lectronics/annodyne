# Design note: level edits in the renderer (Phase 6)

- **Status:** implemented, as built (ADR-0028)
- **Files:** `src/core/render/level.{h,cpp}` (new), `render.cpp` (`render_zone`), tests `tests/unit/level_test.cpp`, `render_test.cpp`, `tests/contract/render_contract_test.cpp`

## Inputs, outputs, errors
`level_gain(note, eff, zone)` returns one linear float gain per zone sample (absolute output samples; the zone and the note span are given as sample indices, `shift_samples` maps source frame times of the note's `amp` track to the output axis). Units: dB and seconds in `EffectiveNote`, linear gain out. Errors: `invalid_argument` (negative zone, rate not > 0, non-finite level values). `apply_gain` multiplies every channel; `invalid_argument` if the length differs. Pure; thread-safe; not real-time safe (allocates).

## Rules
See ADR-0028. Gain: 10^(dB/20), a mute at or below -120 dB. Fades: x, sin(pi x/2) or a 60 dB exponential, from the note edges. Flatten: (mean - level) x amount per frame, linear in dB between frame centres.

## Tests
Neutral state; constant gain and mute; fade shapes at their midpoints; fades longer than the note; flatten at full and half amount, clamp and the silent-frame floor, the shift of the curve with the note; bad zones; `apply_gain` on two channels; renderer: gain applied with the passthrough engine and nothing else moves, fades on a note; contract: samples outside every note stay bit-identical.

## Limits
Level on a stretched/cut zone, per-class noise gains, loudness (LUFS) leveling are later slices.
