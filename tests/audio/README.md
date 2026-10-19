# tests/audio — label `audio`

Objective audio checks from `docs/AUDIO_VERIFICATION.md` §1. Phase 2 has: null, undo-null,
untouched regions, click detector (with its calibration pair: a 10 ms crossfade is clean and a hard
splice is detected), sanity, determinism, length (`render_audio_checks.cpp`), driven by the render
pipeline with the test-only `GainTestEngine`. Round-trip checks (pitch, timing, level, formant)
arrive with the engines that change them.

Tolerances come from `tests/tolerances.h` only. The click-detector thresholds are initial values,
pending human review. Measurements shared with `anno-cli audition` live in `src/cli/checks`.
