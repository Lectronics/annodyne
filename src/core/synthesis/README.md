# core/synthesis

One folder per engine, behind the `SynthesisEngine` interface (`engine/`, layer 2).

- `engine/`: the interface (`render_note`, `name`, `version`, `settings_key`). The version and
  settings are part of the render cache key. The spec's `interface/` folder is `engine/` here
  (ADR-0006).
- `engine/source_through_map.h`: `read_through_map`, the "original" on the output axis through a
  TimeMap (shifted slope-1 warps, zeros in inserts; ADR-0008).
- `passthrough/`: returns the original audio of the range (`read_through_map`). The Phase 2
  engine; real engines arrive in Phase 4+.

Engines are called with a note's whole blend zone and never need sub-range consistency; the renderer
slices. A test-only `GainTestEngine` lives in `tests/support/` and is removed in Phase 4.
