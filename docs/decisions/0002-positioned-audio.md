# ADR-0002: `AudioBuffer` has no `start_time`; positioned audio is `PositionedAudio<Axis>`

- **Date:** 2026-10-19
- **Status:** accepted
- **Phase:** 0

## Context
In `annodyne-core-architecture.md` §3, `AudioBuffer.start_time` is a `float` "in source time or
output time; the owner says which". That is an untyped time value crossing module boundaries, which
CLAUDE.md rule 3 forbids ("never pass raw double seconds across module boundaries") and which the
strong types in `core/types/units.h` cannot check.

## Decision
- `AudioBuffer` holds samples, channel count and sample rate only. It has no position.
- Audio placed on a time axis is
  `template <class Axis> struct PositionedAudio { AudioBuffer audio; TimePoint<Axis> start; };`
  with aliases `SourceAudio` and `OutputAudio`.
- Replacements for the spec signatures:

| Spec | Becomes |
|---|---|
| `SynthesisEngine.render_note(...) -> AudioBuffer` ("in output time") | `-> OutputAudio` |
| `separate_notes(...) -> dict[note_id, AudioBuffer] + residual AudioBuffer` | per-note `SourceAudio` map + residual `SourceAudio` |
| `split_harmonic_noise(...) -> (NoiseTrack, residual AudioBuffer)` | residual is `SourceAudio` |
| `istft(...) -> AudioBuffer` | stays `AudioBuffer`; frames are relative, position belongs to the caller |
| `crossfade(a, b, shape, length) -> AudioBuffer` | operates on `AudioBuffer`s the caller has aligned |
| `RenderResult.buffer: AudioBuffer`, `out_range` | `buffer: OutputAudio`; `out_range` is **dropped**, derived by `output_range(const OutputAudio&)` |
| `RenderResult.source_range` | `SourceRange` (`TimeRange<SourceAxis>`) |
| `AudioSource.read(start, count) -> AudioBuffer` | unchanged; the position is the request (a source-axis sample index) |

## Alternatives considered
- **Make `AudioBuffer` a template on the axis.** Every scratch and engine buffer would then have to
  pick an axis it doesn't have.
- **Keep `start_time` as `double` with a comment.** That is exactly what rule 3 forbids.
- **Keep `out_range` next to `OutputAudio`.** It stores the same fact twice, and the two copies can
  disagree.

## Consequences
- Axis mistakes in render and synthesis are compile errors.
- Phase 2–4 design notes use the signatures in the table above.

## Spec updates
`docs/specs/annodyne-core-architecture.md` §3 (`AudioBuffer`) and §4.5/§4.6 (`render_note`,
`RenderResult`) get a pointer to this ADR.
