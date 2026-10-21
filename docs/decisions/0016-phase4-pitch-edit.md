# ADR-0016: Phase 4 pitch edit (offset limit, envelope re-read, chromatic grid, engine scope)

- **Date:** 2026-10-20
- **Status:** accepted 2026-10-20 (owner accepted the 2400-cent gate and asked that it stay easy to change)
- **Phase:** 4

## Context
Phase 4 adds the first audible edit: a per-note `pitch` offset rendered by two engines (`docs/design/phase4-pitch-edit.md`, structure-reviewed in three rounds). The specs leave several points open or differ from what the design needs: no limit exists on `center_offset`, spec §5.3 re-reads harmonic amplitudes from a shifted envelope, the spec names `target_pitch(pitch, grid)` without a grid type, and the engine interface says nothing about channels or unsupported edits.

## Decision
1. **Offset limit.** `k_max_pitch_offset{2400.0}` (a `Cents`` is one named constant in `core/types/edit.h`. It limits the folded `EffectiveNote::center - pitch_center`. It is used in exactly three places: the clamp in `finish()` of `core/edits/effective_state.cpp`, the `set` range of `center_offset` in `param_info` (so `validate_for_add` rejects a `set` outside it), and `pitch_drag_edit` (`out_of_range`). Changing the limit is a one-line edit plus its tests. It limits the **edit value, not the synthesis**: engines render any offset that reaches them. `PsolaParams::max_shift_cents` (default 700) is the only engine-specific limit; beyond it that engine returns `not_implemented`.
2. **Envelope re-read.** After a shift, harmonic amplitudes are `a_k' = a_k * E(f_k') / E(f_k)` (E = the note's envelope, linear in dB across `freqs`, held at the edge value outside them), not spec §5.3's `env'(t, k*f0')`. The ratio keeps the measured fit error of the original partials. Frames whose harmonic amplitudes are all zero are passed through.
3. **Chromatic grid.** `core/tuning` provides `Result<Cents> target_pitch(Cents pitch, const ChromaticGrid&)` with `ChromaticGrid{Hz a4_reference = k_a4_standard_hz}` (reusing `to_hz`/`to_cents`; ties round toward +infinity; non-finite pitch or reference: `invalid_argument`). A grid variant is introduced in Phase 6 with the second grid, not before.
4. **Main Tool.** `pitch_drag_edit(const AnalysisResult&, const PitchDrag&)` returns `Result<EditSpec>`: `op = set`, `center_offset = target - note.pitch_target.target` (idempotent for repeated drags). Kinds `whole`, `settled`, `glide`, `ornament` are accepted; `multi` and `unpitched` are `invalid_argument`. `pitch_target_mode` is not part of Phase 4 (see ADR-0013, amended).
5. **Engine scope.** `HarmonicNoiseEngine` and `PsolaEngine` support mono sources, the `pitch` category (`center`) and `onset_shift` through slope-1 time-map spans. Any other non-neutral effective value, a non-slope-1 span, a stereo source, an inactive note or one with an empty `harmonics` track returns `not_implemented`. A shift within `k_neutral_snap_offset` returns the original samples exactly.
6. **Margin.** The tracks cover only the note. Engines extend the first/last frame model (held frequency and amplitude, running phase; PSOLA: held grain spacing and ratio) into the blend margin so the renderer's ramp (ADR-0006) is the only note-edge fade; where the edge frame is not shifted the margin is the original. Engines fade only at internal boundaries (`attack_end`, edges of unshifted runs).
7. **Residual** is not stored in `Note`; the engine recomputes `source - additive(original harmonics)` with the same float oscillator bank (targets built with `-ffp-contract=off`).

## Alternatives considered
- Fixed engine limit instead of an edit limit: hides the number inside synthesis and breaks tool/UI consistency.
- Spec §5.3 envelope re-read exactly: simpler, but replaces the measured amplitudes with the smoothed envelope and loses fine structure.
- Store the residual in `Note`: faster render, larger analysis and fingerprint; revisit if recompute dominates render time.
- A `PitchGrid` variant now: speculative with one alternative.

## Consequences
`Note` now carries partials, noise bands and envelope: the caller bumps `AnalyseParams::version`, fingerprints and golden hashes are re-baselined, Phase 3 metrics are unchanged. New folders: `analysis/{partials,harmonic_noise,envelope}`, `synthesis/{harmonic_noise,psola}`, `tools/main_tool`. `effective_state` gains a clamp (new unit test). ADR-0015 stays reserved for `pitch_target_mode` (Phase 6). The default engine is chosen after listening gate #1 in a later ADR.

## Spec updates
Architecture spec §5.3 (amplitude re-read, no offset limit), §4.2/§4.3 (`target_pitch` gets a concrete grid type), §4.5 (engine scope) are overridden by this ADR; the specs stay read-only.
