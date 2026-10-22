# core/synthesis/stretch

Renders a TimeMap warp with slope != 1 from the source, pitch and timbre kept (ADR-0023, `docs/design/phase5-stretch-render.md`). A free function, `stretch_span`, not a `SynthesisEngine`: it renders the unedited base of a region, it is not chosen per edited note.

**Method.** Per output sample the source frame decides the path.
- *Voiced* (frame class in `voiced_classes`, finite pitch): TD-PSOLA time scaling with the marks of `core/synthesis/grain`. Synthesis marks are one source period apart; each takes the nearest analysis mark. The first and last mark of a run keep their distance from the run's ends, so the output joins a slope-1 copy in phase; the spacing between them is scaled to fit.
- *Everything else* (noise, silence, breath, plosives, outside notes): sine-window grains at 50 % overlap (power complementary), whole-sample reads at the source time of the grain centre plus a seeded jitter. The jitter derives from (seed, span start, grain index), so a range renders the same samples whichever range is asked for.
- Class edges: equal-power crossfade of `class_edge_fade`.

**Files.** `stretch.{h,cpp}` (params, `stretch_span`, mix), `detail/class_map` (voiced indicator, runs, pitch points), `detail/voiced_stretch`, `detail/noise_stretch`, `detail/geometry.h`.

**Known limits.** Vibrato rate changes with the slope (the contour is re-timed, not regenerated); plosives and sibilants are stretched where the map says; transients inside a stretched span are smeared; a voiced run is rendered from its start each call (cost O(run)); mono only. Built with `-ffp-contract=off`.
