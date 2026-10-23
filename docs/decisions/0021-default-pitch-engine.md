# ADR-0021: PsolaEngine is the engine for pitch edits (listening gate #1)

- **Date:** 2026-10-22
- **Status:** accepted 2026-10-22 (owner listening, see `docs/reviews/phase4-listening.md`)
- **Phase:** 4

## Context
Phase 4 built two engines for the per-note pitch edit and left the default to listening gate #1 (ADR-0016). The owner listened to the synthetic and real-voice packages. For every shift heard, PSOLA was "much better" than `HarmonicNoiseEngine`, which added audible distortion on synthetic and real voices alike. PSOLA's first version had hiccups on real voices and dropped the noise bed; both were fixed (waveform-matched marks; period-synchronous separation, ADR-less engine changes recorded in `docs/design/phase4-pitch-edit.md` stages 9b-9d) and the owner kept the result with `separation_cycles` 1 ("decent"). Remaining PSOLA faults the owner named: the first formant moves with the shift (deferred to the Phase 6 `formant` tool), and vibrato rate/extent measure slightly off on a few edits.

## Decision
1. **The engine for `pitch` edits is `PsolaEngine`** with its defaults (`separation_cycles` 1, `class_edge_fade` 4 ms, shifted classes voiced and breathy, `max_shift` 700 cents).
2. **`HarmonicNoiseEngine` is kept**, not default: it is the engine for a shift beyond PSOLA's `max_shift` (700 cents, where PSOLA returns `not_implemented`) up to the `k_max_pitch_offset` limit of 2400 cents, and the comparison engine for the formant work. Any caller that chooses an engine for an edit uses PSOLA first and falls back to harmonic_noise on `not_implemented`; the fallback policy lives in the render orchestration of the project layer (Phase 7) and in the CLI's explicit `--engine` until then.
3. **The CLI default stays `passthrough`** (the null engine the Phase 2 contract tests rely on); `--engine psola` is how a pitch scenario selects it. `scripts/phase4_audition.py` keeps rendering both engines for comparison.
4. **Provisional parameters stay provisional** (ADR-0018 decision 1): `max_shift` 700, `separation_cycles` 1, the noise-gain model. They are tuned on the owner's listening and the committed Vocadito subset.

## Alternatives considered
- Harmonic_noise as the default: rejected by listening (distortion on every file heard).
- Always auto-select per edit now: needs the render orchestration of Phase 7; an explicit choice is enough while the CLI is the only caller.
- Remove harmonic_noise: it is the only engine for shifts beyond 700 cents and the reference for the formant work.

## Consequences
- The ROADMAP Phase 4 section is closed with PSOLA as the pitch engine; `docs/reviews/phase4-review.md` records the open items (formant drift, vibrato jitter, the 700-cent limit, the noise-bed model).
- `HarmonicNoiseEngine` quality is not pursued further in Phase 4; if shifts beyond 700 cents matter, revisit with the Phase 6 formant tool and ADR-0019's marks.
- No spec change.
