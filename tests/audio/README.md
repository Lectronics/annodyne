# tests/audio — label `audio`

Objective audio checks from `docs/AUDIO_VERIFICATION.md` §1: null test, undo-null, untouched regions,
round-trips (pitch, timing, level, formant), click detector, length, sanity, determinism.
Tolerances come from `tests/tolerances.h` only. First tests arrive with rendering (Phase 3); until
then `ctest -L audio` finds nothing and exits 0 (ADR-0004, design note §3).
