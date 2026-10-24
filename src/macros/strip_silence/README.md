# macros/strip_silence

Strip Silence, gate mode: `strip_silence_edits` writes one `gap_gain` set (origin "strip_silence") targeting every gap in scope whose `silence` regions add up to `min_silence`, with the mute (`k_gap_mute_db`) by default or an attenuation.

- Layer 5; links only `core/types`.
- Only `silence` regions are gated, so sibilants, breaths, tails and the guard before a note survive (ADR-0024 amendment 1; the consonant evidence of `analysis/gaps`). The tighten/remove modes (removing the time) wait for the layout work.
- Errors: `invalid_argument` (no segment timeline, a negative or non-finite `min_silence`, a gain outside [-120 dB, +24 dB]).

Design: `docs/design/phase5-gaps-core.md`. Tests: `tests/unit/gap_tools_test.cpp`.
