# core/render

`render_region` (layer 4): edits -> audio. Reads the analysis and the edit list, asks a
`SynthesisEngine` for each edited note's blend zone, and blends the results into a copy of the
original with crossfades. Samples outside every zone are the source samples, bit for bit; an empty
edit list renders the source exactly (CLAUDE.md rule 5).

Method (design note `docs/design/phase2-render-skeleton.md`):
- `blend_plan` turns the edited notes' sample spans and the crossfade length `m` into ramps
  (pristine -> engine, engine -> engine, engine -> pristine) with one overlap rule; neighbours whose
  zones meet are faded into each other, never through the original.
- `render` applies the plan per channel: plateaus are copies, ramps call `core/dsp` `crossfade` on
  the right slice of the longer ramp. Engines are always asked for the whole zone.
- `render_key` builds the canonical cache key (length-prefixed fields; the plan's ramps; each zone
  note's `EffectiveNote` and a 128-bit fingerprint of the `Note`). `RenderCache` is a mutex-guarded
  LRU of finished results.

References: architecture spec §4.6, §5.5, §5.6; ADR-0002, ADR-0006.
Time (ADR-0008): `render_region` walks TimeMap spans (`RenderInputs::time_map`, or built from the
edits): slope-1 warps copy shifted source samples, inserts are digital silence, cuts have no output.
The cache key includes the whole map.
Stretching (ADR-0023): a slope != 1 warp is part of the unedited base, rendered by `stretch_span` (mono
only); a note whose edits are only timing edits is part of that base. An edited note (pitch, level,
formant) whose zone meets a stretched warp is `not_implemented`, naming the note. With no supplied map
the map is built with `TailPolicy::shift` and whole-sample shifts.
Known limitations: a fractional-sample shift on a slope-1 warp returns `not_implemented`; the cache locks and allocates, so it is not real-time safe (Phase 9); notes are
monophonic (Phase 11); the crossfade default `equal_power` overshoots by up to +3 dB for correlated
signals (listening decision at Phase 4); `-ffp-contract=off` makes results reproducible per build,
not across platforms.
Tuning parameters: `RenderParams::crossfade_length` (0..20 ms, default 10 ms), `crossfade_shape`.
