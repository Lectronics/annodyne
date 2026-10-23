# core/synthesis/stretch

Renders a TimeMap warp with slope != 1 from the source, pitch and timbre kept (ADR-0023, `docs/design/phase5-stretch-render.md`). A free function, `stretch_span`, not a `SynthesisEngine`: it renders the unedited base of a region, it is not chosen per edited note.

**Method.** Per output sample the source frame decides the path.
- *Voiced* (frame class in `voiced_classes`, finite pitch): TD-PSOLA time scaling with the marks of `core/synthesis/grain`. Synthesis marks are one source period apart; each takes the nearest analysis mark. The first and last mark of a run keep their distance from the run's ends, so the output joins a slope-1 copy in phase; the spacing between them is scaled to fit.
- *Everything else* (noise, silence, breath, plosives, outside notes): sine-window grains at 50 % overlap (power complementary), whole-sample reads at the source time of the grain centre plus a seeded jitter. The jitter derives from (seed, span start, grain index), so a range renders the same samples whichever range is asked for.
- Class edges: equal-power crossfade of `class_edge_fade`.

**Files.** `stretch.{h,cpp}` (params, `stretch_span`, mix), `detail/class_map` (voiced indicator, runs, pitch points), `detail/voiced_stretch`, `detail/noise_stretch`, `detail/geometry.h`.

**Vibrato (ADR-0023 amendment 3).** With `VibratoMode::preserve` (default) the vibrato of a voiced run is regenerated at the source's rate and depth (`detail/vibrato`: windowed DFT for `f_v` in 3..9 Hz, demodulated amplitude and phase, phase closed on a whole cycle where the run joins neighbouring voiced audio); the synthesis marks follow `slow + vibrato`, the grains stay the source's. `retime` keeps the old behaviour.

**Known limits.** A run under one output second that joins voiced audio at its end can change the vibrato rate by up to `0.5/L` Hz; the vibrato is modelled as a sinusoid; pitch and formant edits inside a stretched span still read the re-timed analysis; plosives and sibilants are stretched where the map says; transients inside a stretched span are smeared; a voiced run is rendered from its start each call (cost O(run)); mono only. Built with `-ffp-contract=off`.

**Rubber Band engine (ADR-0026).** `StretchParams::engine = StretchEngine::rubberband` renders a span through Rubber Band R3 (offline, one thread, channels together) instead of the PSOLA/grain paths: any channel count, exact output length, an 8 ms blend into the source copy at both ends. Vibrato is not preserved by it and a span is rendered whole per call. Default: `psola`.
