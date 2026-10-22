# core/synthesis

One folder per engine, behind the `SynthesisEngine` interface (`engine/`, layer 2), plus `grain/` and `stretch/`, which are not engines.

- `engine/`: the interface (`render_note`, `name`, `version`, `settings_key`). The version and
  settings are part of the render cache key. The spec's `interface/` folder is `engine/` here
  (ADR-0006).
- `engine/source_through_map.h`: `read_through_map`, the "original" on the output axis through a
  TimeMap (shifted slope-1 warps, zeros in inserts; ADR-0008).
- `passthrough/`: returns the original audio of the range (`read_through_map`). The Phase 2
  engine.
- `harmonic_noise/`, `psola/`: the pitch engines (Phase 4; PSOLA is the default, ADR-0021).
- `grain/`: marks, period curve, fractional reader and grain overlap-add shared by `psola/` and
  `stretch/` (ADR-0023).
- `stretch/`: `stretch_span`, the unedited audio of a TimeMap warp with slope != 1 (Phase 5,
  ADR-0023). A free function, not an engine: the renderer calls it for the base of a region.

Engines are called with a note's whole blend zone and never need sub-range consistency; the renderer
slices. A test-only `GainTestEngine` lives in `tests/support/` and is removed in Phase 4.
