# ADR-0030: touching notes with different offsets glide instead of stepping

- **Status:** accepted (decided during the owner's listening round, 2026-10-23; reversible: `glide_time` 0 restores the constant offset per note, the context is optional)
- **Phase:** 6
- **Related:** ADR-0027 (Rubber Band pitch engine), ADR-0006 (render API), `docs/design/pitch-glide-transitions.md`

## Context
The owner heard hitches in the Rubber Band Correct Pitch render at 4.322 s and near 6.61-6.63 s of `vocadito_2`: "about 12 cycles after look different than the rest of the waves around it". Both places are boundaries between two corrected notes (a 10 ms gap and a touching pair) whose offsets differ (+11 and -39 cents against their neighbours' values; 87 cents between `n006525` and `n006625`). Each note was shifted by its own constant, so the pitch stepped at the boundary and the two zone renders, blended over the 10 ms margin, disagreed in phase. Rubber Band's own options (transients, detector, phase, window, pitch consistency) change nothing for the finer engine (seven variants rendered the same file).

## Decision
1. `SynthesisEngine::render_note_in_context(..., const TouchingNotes&)` (default: ignore the context, call `render_note`). `TouchingNotes` names the previous and next active note when the gap to it is at most `k_touching_gap` (30 ms), with the offset it ends up with and whether an engine renders it (a pitch-edited note) or the base audio plays there. The renderer always calls it; the context is part of the cache key (key version 8).
2. `RubberBandPitchEngine` follows one pitch curve through such a boundary: with a rendered neighbour the offset moves between the two values over `glide_time` (40 ms, raised cosine) centred on the boundary, so both notes' zones agree where they overlap; with an unrendered neighbour the note ramps from 0 over `glide_time` inside itself. Equal offsets or no touching neighbour: unchanged (bit-identical to `render_note`). A gliding note is rendered by the live shifter (as for the modulation and drift scales).
3. PSOLA and the harmonic engine ignore the context (unchanged).

## Consequences
- Measured on `macro_correct_pitch_chromatic`: the 4.31-4.36 s window has no cycle-similarity dip any more (was 1); whole-file dips 162 -> 147 (PSOLA 145); the 6.60-6.65 s window did not improve (4 dips, was 3). Those two notes are breathy (79 % and 62 % of their frames), where pitch is not well defined and an 87-cent jump in 40 ms is a large scoop; the next step there is to shift only voiced frames (class routing, as PSOLA does) and/or a longer glide.
- The objective cycle-similarity and third-difference measures do not separate PSOLA from Rubber Band note by note (7 notes better for Rubber Band, 11 for PSOLA, 26 ties, mostly by one or two frames), so a programmatic engine choice needs the owner's per-note listening labels first.
