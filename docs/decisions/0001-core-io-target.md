# ADR-0001: `core/io` target for audio file I/O

- **Date:** 2026-10-19
- **Status:** accepted
- **Phase:** 0

## Context
ROADMAP Phase 0 requires WAV read/write, and `anno-cli info` needs it. The folder layout in
`annodyne-core-architecture.md` §6 has no place for audio file I/O:
- `core/types` must be plain data with no I/O (`architecture.md`, "What goes where").
- `project` (layer 6) is about the document model, not audio decoding.
- CLAUDE rule 2 says only `src/core/` touches audio samples, so the code belongs under `core/`.

If it sat at layer 0 like other `core/` targets, analysis (1), synthesis (2) and render (4) could
link it and do file I/O. That would break spec §4 ("each function is pure") and invite I/O near the
real-time path (`realtime-audio.md`).

`cli/commands` (a library holding the testable CLI commands) is also missing from the layer table.

## Decision
- Add `src/core/io/` → CMake target `anno_core_io`. For **its own** dependencies it is layer 0: it
  may link only `core_types` and the standard library.
- **Who may link it** is restricted (rule R4 in `cmake/AnnoLayers.cmake`): only layer 6–7 targets
  (`project`, `host/*`, `ui`, `cli/*`) and test executables. Any other link fails configuration
  with `ANNO LAYER VIOLATION: R4`.
- `cli/*` (including `cli/commands`) is layer 7.

## Alternatives considered
- **Put WAV in `project`.** That keeps the layer table unchanged, but mixes file formats with the
  document model and breaks CLAUDE rule 2 (audio samples outside `core/`).
- **Put `core/io` at plain layer 0.** Simpler, but analysis, synthesis and render could then read
  files.
- **Use a third-party reader (dr_wav, libsndfile).** That is a new dependency for about 250 lines of
  well-specified code; it could be revisited if more formats are needed.

## Consequences
- Analysis, synthesis and render receive `AudioSource`/`AudioBuffer` values and never paths.
- Golden and audio tests load files directly, because tests are not layered targets.
- A fixture test (`deny_tool_to_io`) proves the restriction.

## Spec updates
- `.claude/rules/architecture.md` layer table: adds `core/io` and `cli/*` rows.
- `CLAUDE.md` layout summary: adds `io` to `src/core/{…}`.
- `docs/specs/annodyne-core-architecture.md` §6: adds `core/io/`.
