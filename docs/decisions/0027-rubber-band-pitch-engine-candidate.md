# ADR-0027: Rubber Band pitch scaling as a candidate pitch-edit engine (A/B only)

- **Status:** accepted for the A/B (decided autonomously during the owner's absence, 2026-10-23; reversible: delete the folder and the CLI choice). The pitch-edit engine decision itself stays with the owner (ROADMAP Phase 6/7 item).
- **Phase:** 6
- **Related:** ADR-0026 (Rubber Band vendored), ADR-0021 (default pitch engine), ADR-0019 (SPTK marks), CLAUDE.md rule 9.

## Decision
Add `core/synthesis/rubberband_pitch` (`RubberBandPitchEngine`, a `SynthesisEngine`) so Rubber Band R3's pitch scaling (formant-preserving by default, offline, one thread, any channel count) can be A/B-compared with `PsolaEngine` and `HarmonicNoiseEngine` on the same scenarios: CLI `--engine rubberband`. It realises `center` (and `onset_shift` through the TimeMap) and refuses everything else with `not_implemented`, as the Phase 4 engines do. The whole zone is shifted (voiced and unvoiced alike, including the margin beyond the note, which the renderer fades). No default changes: `psola` stays the default pitch engine (ADR-0021).

## Consequences
- `anno_rubberband` may be linked by `core/synthesis/rubberband_pitch` as well as `core/synthesis/stretch` (AnnoLayers allowlist).
- First numbers (`audition/phase6-pitch-engine/INDEX.md`): no gain in the Phase 4 objective rows on the real voice (pitch round trip off by 3.5 and 14 cents at +200/+700); the decision needs listening.
- Not realised: continuous (time-varying) pitch curves; R3's real-time mode with a changing pitch scale is the way to try that, not offline mode.
