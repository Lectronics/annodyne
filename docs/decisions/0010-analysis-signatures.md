# ADR-0010: Phase 3 analysis function signatures

- **Date:** 2026-10-19
- **Status:** accepted 2026-10-19 (owner approved the Phase 3 plan; this records the signature differences found while writing the design notes)
- **Phase:** 3

## Context
Architecture spec §4.2 lists Python-style signatures. Four of them do not fit the typed data model (`Note` stores centre + drift + mod only, source audio is read through `AudioSource`, strong types) or would create hazards.

## Decision
1. **`segment_notes` returns `NoteSpan`s, not `Note`s.** `NoteSpan` = id, the four times, rough pitch, boundary type, confidence (declared in `core/analysis/segmentation`). A `Note` with empty curves and loudness 0 would pass `validate(Note)` yet be meaningless; the orchestrator alone builds finished `Note`s. It does not take the source (the energy cue arrives through the onsets).
2. **`decompose_pitch_curve(span, pitch, amp, params)`** takes the `NoteSpan` and the `PitchTrack` and returns `PitchDecomposition` (centre, drift, mod); the raw f0 exists only in the track.
3. **`measure_loudness(plain, k_weighted, span, grid, params)`** takes the already-read mono signals (read and K-weighted once per source) and returns `NoteLoudness` (single LUFS value + amp curve on the note grid). Loudness is integrated over the note's own span with the absolute gate only (no 400 ms/75 % blocks, no relative gate), floor -70 LUFS.
4. **`detect_onsets`** has two overloads: spectral onsets, and spectral plus pitch-change onsets when a `PitchTrack` is given (`OnsetKind::consonant` needs voicing).
5. **`perceived_pitch(note, params)`** and `decompose_pitch_curve` share one stability-weight definition (`StabilityWeightParams`, `stability_weights`) in the `perceived_pitch` target.
6. **Note grid rule:** the grid of a note is the pitch-track frames in `[source_start, source_end)` (`note_grid`); curves are values at those frames.

## Alternatives considered
- Return `Note`s with placeholder fields (the spec's reading): rejected, see 1.
- Interpolate curves at arbitrary note-relative times: more code, no benefit before Phase 4.

## Consequences
Spec §4.2 is updated to point at this ADR. `Note` and `AnalysisResult` are unchanged. A contract test asserts that every note produced by `analyse()` is complete.
