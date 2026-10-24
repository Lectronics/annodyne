# ADR-0032: a pitch correction applies to the note's settled core; unpitched frames and note edges stay as sung

- **Status:** accepted (owner's listening, 2026-10-23; ADR-0031 items 4 and 6). Supersedes the neighbour-glide design of ADR-0030 for the Rubber Band engine.
- **Phase:** 6

## Context
The owner listened to the glide renders (40, 80, 150 ms): they all sounded the same. The remaining artifacts were "sudden dips in gain" in the middle of open vowels, not clicks, and the owner's reading was that each note was being jumped to its corrected pitch instead of keeping its natural start and end: "the rolls and oddities ... should count as part of the note, but not all of that note should be corrected. Only the portion that is at the target at least initially." Measured on `macro_correct_pitch_chromatic`: 19 four-millisecond windows were more than 6 dB under the source (93 under 3 dB), 11 of 18 dip events within 20 ms of a note boundary, where the corrected zone met the base audio with a differently-shifted signal.

## Decision
1. **The offset is applied to the core of the note** (`Note::pitch_target.core`; the whole note when no core is set), as one pitch curve: `offset(t) = own offset x raised cosine up over the first `glide_time` of the core x raised cosine down over its last `glide_time` x class weight(t)`. `glide_time` defaults to 40 ms (owner: 30-40). Outside the core (an approach, a scoop, a release) the audio is not shifted. Touching notes need no coordination: both curves are zero at their shared edge.
2. **Unpitched classes are not shifted** (ADR-0031 item 4): frames whose class is not in `shifted_classes` (default voiced and breathy; sibilant, breath, plosive, silence are left alone) get weight 0, interpolated between frame centres.
3. **Where the offset is effectively zero the original samples are kept**: the shifted audio fades in over 2-10 % of the full offset. Reason: the shifter smears an attack even at a pitch ratio of 1, and a note's start is often one.
4. All of this is the live shifter path (a time-varying pitch scale, ADR-0027 amendment 1). `core_only = false` restores the constant offset by the offline stretcher over the whole zone.
5. The touching-notes context of ADR-0030 (`TouchingNotes`, `render_note_in_context`, cache key 8) is removed again (key version 9); nothing used it any more.
6. **The share of a note that is corrected** (a long press on a note in a pitch tool shows a slider for how much of it is corrected, owner) is a GUI feature; the core window is its first form and `glide_time`/`core_only` are the engine's controls. A per-note amount edit is left to the phase that builds the tools' UI.

## Consequences
- `macro_correct_pitch_chromatic` (Rubber Band engine, ADR-0029 default): windows more than 6 dB under the source 19 -> 8 (PSOLA 4), more than 3 dB 93 -> 48 (PSOLA 37), cycle-similarity dips 147 -> 116, the 6.60-6.65 s window 4 -> 2, the 4.31-4.36 s window stays 0; click detector 0 new. The remaining six dips are inside notes (4-14 ms, -5 to -11 dB, voiced and breathy frames); a likely cause is the partial-offset crossfade between the shifted and the original audio when they are a fraction of a cycle out of phase. Not fixed.
- Whole-note corrections ("whole" targets with no separate core) ramp in and out inside the note over `glide_time`, so the first and last 40 ms of such a note are only partly corrected.
- Not listened to; the owner decides.
