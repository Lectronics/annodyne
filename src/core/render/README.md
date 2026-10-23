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

## The gap gate (ADR-0024)

`gate.h`: `gate_curve` is the per-sample gain a `gap_gain` edit applies to the `silence` regions of its gap (5 ms ramps inside the region where it borders a note or another region; none at the recording's start or end). `render_region` multiplies it in after the blend and skips samples whose gain is 1; tails, the guard before a note and every classified region (sibilant, breath, plosive, audible) are never touched. `RenderResult::gaps_rendered` lists the gated gaps that meet the region; the gates are the last part of the cache key.

## The layout and the splice (ADR-0025)

With no supplied map and a segment timeline in the analysis, `render_region` builds the map with `build_layout_time_map` (`RenderParams::layout`): gaps next to a timing edit are cut or held first, the audio between markers is stretched, the recording keeps its length. `splice.h` smooths the joins: a cut crossfades over `splice_fade` (5 ms) into the audio after it; an insert (digital zeros until room tone) fades the audio on each side to and from zero, inside its silence region. `splice_fade` 0 keeps hard joins. The cache key (version 5) holds the map spans that meet the region widened by the margin and the fade, so a timing edit elsewhere leaves a block's key alone (the first edit from an identity map changes every key once). Limits: an edited note whose zone meets a cut or insert is `not_implemented`; stretched spans need mono.
