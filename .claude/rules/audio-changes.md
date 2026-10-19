---
paths:
  - "src/core/synthesis/**"
  - "src/core/render/**"
  - "src/core/edits/**"
  - "src/core/timemap/**"
  - "src/core/analysis/**"
  - "tests/audio/**"
  - "audition/**"
---

# Changes that affect what users hear

You are editing code that changes rendered audio or the analysis it depends on. Follow `docs/AUDIO_VERIFICATION.md`.

Before calling the change done:

1. Run `ctest --preset debug-asan -L audio`. All objective checks must pass: null test, untouched-region test, round-trip accuracy (pitch, timing, level, formant), click detector, length check, NaN/Inf/peak check, determinism.
2. If a tolerance needs loosening to pass, **stop**. Do not loosen it in the same change. Report the measured value and ask.
3. Ask the `audio-verifier` subagent to run the scenarios affected by the change and produce an audition package under `audition/<phase>-<topic>/`.
4. In your summary, list the objective results and say plainly that a human still needs to listen. Never describe the audio as sounding good, clean or natural.
5. If the change could alter output for unedited notes, say so explicitly and show the untouched-region test result.
