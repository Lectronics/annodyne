# Design note: rolling pitch changes (glides) and continuous pitch and formant curves

- **Phase:** 6 (`tools/pitch` transitions, `macros/correct_pitch`). Nothing here is built in Phase 4.
- **Status:** draft (owner request 2026-10-21, saved for Phase 6)
- **Spec sections:** `annodyne-features.md` Main Tool ("note transitions": the glide between consecutive notes can be made faster or slower), `annodyne-implementation-guide.md` pitch contour handling
- **Folder / CMake target:** `src/tools/pitch` (layer 5, emits edits), engines in `src/core/synthesis/*` (layer 2)

## Purpose
In Phase 4 a pitch drag is one constant `center_offset` per note. Moving one note and not its neighbour puts a hard step in the pitch at the boundary. A singer slides between notes, so a hard step can sound like a mechanical jump. The target behaviour is a rolling change: the offset ramps in over the transition and ramps out into the next note.

## What the model has to say
For each boundary between two voiced notes (and a note edge next to silence or a consonant):

| Field | Meaning | Default |
|---|---|---|
| `transition_in` (Seconds, shape) | How long the offset takes to reach its full value at the note's start | the source's own pitch-glide time at that boundary (from the stored `pitch_drift`), clamped to 30..150 ms (provisional parameters, ADR-0018 decision 1) |
| `transition_out` (Seconds, shape) | The same at the note's end | same |
| shape | `raised_cosine` (S-curve in cents), later `linear` | raised cosine |

The effective offset curve is `offset_cents(t) = note_offset * ramp(t)`, a time-varying cents value added on top of drift and vibrato. Vibrato and drift are not touched, so the singer's own movement rides on the glide.

## Consequences for the existing design
- **Edits.** A transition is a new edit category or new parameters on `center_offset`; the ADR is **ADR-0020 (provisional number)**, written before any code. `EffectiveNote` gains the two transition fields. Undo, `dirty_regions` and the fold stay as they are.
- **Engines.** ADR-0016 decision 5 says engines realise only a constant offset (`not_implemented` otherwise) and decisions 1 and 5 fix the `center_offset` range and the `finish()` clamp; ADR-0020 supersedes those clauses. A glide makes the ratio change over time. `PsolaEngine` already derives its synthesis period from a per-frame curve (`PeriodCurve`), so a per-frame ratio is a small change: the ratio becomes a function of position, not one number. `HarmonicNoiseEngine` shifts per frame and needs the same. The 700-cent `max_shift` check then applies to the peak of the ramp.
- **Hiccups.** The grain repeat/skip rate is `F0 * |ratio - 1|` (`psola-vibrato-hiccups.md` section 4). A ramp sweeps that rate from 0 up to its final value, so the stall/jump events appear and slow down at the edges of every glide. With the Phase 4 waveform-matched marks this should stay quiet, but the pitch-glide scenario must run the same cycle-jump check (`cli/checks/pitch_checks` once the cycle-jump measure is ported there, ADR-0019 decision 6) before anyone listens.
- **Unvoiced and consonant frames.** Frames the engine does not shift (ADR-0017 class routing) keep the original samples. The ramp must therefore end at the voiced/unvoiced boundary, not inside a consonant.
- **Correct Pitch.** The macro should produce glides by default: snap the note, then ramp, so the correction does not step. The `pitch_target_mode` ADR (ADR-0015) and this note should be decided together.

## Generalisation: continuous curves for pitch and formant (owner 2026-10-21)
Glides are one case of a more general need: a pitch or formant change whose amount varies through time, for a note or a selected range, instead of one static shift.

| Piece | Proposal |
|---|---|
| Amount type | `AmountCurve`: breakpoints `(time, value)` with a segment shape per segment (`linear`, `smooth` = raised cosine / monotone cubic, `hold`). Value is `Cents` for pitch and for formant (an envelope shift expressed in cents of frequency; one unit for both). Time is a `Seconds` offset from the note's **output start under the `TimeMap`**, so an `onset_shift` moves the curve with the note. `validate`: at least one point, strictly increasing times, finite values, a bounded count; evaluation is a pure function in `core/types` next to `validate`, called by the engine (engines take `EffectiveNote`, so the engine, not the renderer, samples it per frame). |
| Two ways to author it | A ramp (start value, end value, shape) and a drawn curve (breakpoints). A ramp is a curve of two points; one data type. |
| Where it lives | `core/types` (the curve), folded into `EffectiveNote` as an optional curve next to the static offset; the static offset is the constant curve. Tools emit edits that carry a curve. |
| Pitch | The curve adds to centre and keeps drift and modulation. A glide at a note edge is a pre-built curve. |
| Vibrato control (Phase 6 `pitch_modulation`) | Rate, depth and shape of the vibrato are each a curve over the note, so vibrato can fade in, speed up or deepen. This is why the curve type should exist before the vibrato tool does. Re-imposing vibrato on a flattened note is the idea of `psola-vibrato-hiccups.md` recipe 2 (shift the note-level part, then add the vibrato back); the research marks it simulated on one synthetic vowel, so it is a candidate to test, not an established method. |
| Formant | The formant tool takes the same curve. The engine applies it as the ratio of the envelope at the new position to the original, per frame. |
| Engines | Take a per-frame ratio or envelope-shift value. The Phase 4 engines take one number; the interface changes to a per-frame function, and the constant offset becomes the special case. |
| Fold semantics | ADR-0020 defines how a curve composes under `set` (replace), `add` (breakpoint-wise sum on the union of times), `scale` (scales values) and `reset`, and how two curves on one note combine, keeping `effective_state` a pure fold in `seq` order. The limit `k_max_pitch_offset` is checked at the curve's peak. `dirty_regions` covers the curve's time span plus the blend margin. |
| Smoothing | The engine samples the curve at the frame rate and a jump is an error unless the shape is `hold` at a boundary. |
| UI (Phase 10) | A curve editor on the note, with the same drag semantics for pitch and formant. |

This needs an ADR before code (new edit parameter type, `EffectiveNote` change, engine interface change). It is not part of Phase 4 and Phase 4 does not build any of it; its constant-offset path stays the special case.

## Effect on audio
Changes rendered output for notes with a transition edit only. Notes without one are as in Phase 4 (constant offset), bit-identical for unedited material.

## Objective checks (Phase 6)
- Pitch round trip along the ramp: measured `f0(t)` minus source `f0(t)` follows `note_offset * ramp(t)` within the pitch tolerance, at ten points across each transition.
- Zero-length transition gives the Phase 4 result exactly.
- Click detector and cycle-jump metric over the transitions.
- Audition `phase6-expression` includes: +200 cents on one note of a legato pair, with the default glide, a 10 ms glide and a 150 ms glide.

## Open questions for the owner
1. Should the ramp be centred on the note boundary or sit entirely inside the edited note? A centred ramp changes the neighbour's samples, but engines render one note: the neighbour would need its own edit, or `dirty_regions` and the render would have to widen across the boundary. ADR-0020 resolves this before code; the lower-risk default is a ramp entirely inside the edited note.
2. Should the glide time default to the singer's own measured time, or a fixed time the user sets?
3. Does a macro like Correct Pitch get one global transition setting, or per-note override?
