# ADR-0004: Test folders

- **Date:** 2026-10-19
- **Status:** accepted
- **Phase:** 0

## Context
`CLAUDE.md` and ROADMAP Phase 0 use `tests/{unit,contract,audio,golden,torture}` with CTest labels
of the same names. `annodyne-core-architecture.md` §6 lists `tests/{golden,null,roundtrip,torture}`.
`docs/AUDIO_VERIFICATION.md` §1 already places the null and round-trip checks under `tests/audio/`
with label `audio`.

## Decision
`CLAUDE.md` wins. Test folders are `tests/{unit,contract,audio,golden,torture}`, each with the
matching CTest label. The null, undo-null, untouched-region and round-trip checks are `audio`-label
tests under `tests/audio/`.

## Alternatives considered
- **Follow spec §6.** That conflicts with CLAUDE.md, the ROADMAP and AUDIO_VERIFICATION, and has no
  home for unit or contract tests.

## Consequences
None for code yet. Phase 3 puts the null test in `tests/audio/`.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §6 `tests/` tree is updated.
