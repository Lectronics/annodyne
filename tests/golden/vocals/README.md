# tests/golden/vocals

Real vocal recordings with hand-checked answers, for the Phase 3 gate (ROADMAP: onsets within +-20 ms, no
octave errors). Supplied by the project owner; each recording needs three files with the same stem:

| File | Content |
|---|---|
| `<name>.wav` | the recording (mono or stereo, any common rate, float32 or PCM) |
| `<name>.notes.json` | `{"notes": [{"start_s": 1.20, "end_s": 1.85, "midi": 64}, ...]}`: every sung note, start/end in seconds on the source axis, pitch in MIDI (A4 = 69) good to +-0.5 semitone |
| `<name>.pitch.json` | `{"spots": [{"time_s": 1.50, "midi": 64.0}, ...]}`: at least 10 hand-checked VOICED instants per file (the octave check; the estimate must be voiced and within 50 cents, never +-1200) |

Licences and sources (fill in when adding a file; do not commit audio you may not redistribute):

| Recording | Source / performer | Licence | Length |
|---|---|---|---|
| *(none yet)* | | | |

`golden_vocals_test` (label `golden`) analyses every recording that has all three files:
onset F-measure >= 0.9 at +-20 ms against the note starts, matched notes' perceived pitch within 50 cents,
and no octave error at the spots. With no recordings it only prints a warning; the Phase 3 close runs it with
`ANNO_REQUIRE_GOLDEN=3` (`ANNO_REQUIRE_GOLDEN=3 ctest --preset release -L golden`), which fails unless at
least three complete recordings are present.

Plots for the human check: `anno-cli analyse <name>.wav --plot-dir <dir>` (see
`docs/reviews/phase3-plots.md`).
