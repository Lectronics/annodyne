# Listening sheet: phase 5 — timing (listening gate)

- **Listener:** the project owner
- **Date:** 2026-10-23 (several rounds in one session)
- **Playback:** not recorded
- **Audition packages:** `audition/phase5-layout/` (four synthetic scenarios, PSOLA), `audition/phase5-real/` (four `vocadito_2` scenarios, PSOLA), `audition/phase5-real-rb/` (the same four, Rubber Band R3), each with `excerpt_*.wav` around the edit. Scenarios: `tests/audio/scenarios/phase5/`.

**How this sheet was filled.** The owner listened and reported in words and with times; no 1-5 ratings. What follows is their statements, in order, as given in the session; nothing here is a claim about how the audio sounds.

| Round | Material | Statement (owner) |
|---|---|---|
| 1 | synthetic `layout_a_*` (PSOLA) | Vibrato depth seems to increase and its rate decreases on a stretched note; "this type of artifact is NOT good in this quantity". No more than 0.5 Hz shift in vibrato from any edit or chain of edits (unless the edit is a vibrato control). A shortened note should keep the original vibrato rate and depth. `layout_a_later` did not behave as expected: only the edited note should change where possible; where the gap is not long enough "the file on both sides should be stretched or squished"; reverting to flex is acceptable for a monophonic vocal. |
| 2 | real `vocadito_2` (PSOLA) | Artifacts remain at 31.61-32.888 s in `longer_late`; hitch-like artifacts at 3.06 s and clicks near 4.425, 4.696, 4.99 s in `later`, "a gain change of 1.5 to 2 dB during the crossfades"; not employing flex all the time is "really hurting us"; the layout should decide where to place markers and use flex as the default. Sound is "a little bit similar to the clicks in your clicks_pipeline files". |
| 3 | `later` v2 (PSOLA) | Cycles not lining up; look at 3.23598 s, 3.27 s (smooth junction, no zero crossings) and 6.6670833-6.6704166 s: unexpected aperiodicity compared with the original, "99% sure it is the exact spot". |
| 4 | `later` v2 after the glide fix (PSOLA) | The issue still exists: a clean sine wave grafted into the vibrato, with subtle aperiodicity on either side for about the same duration. Direct splicing did not give better results; suggests re-synthesis with a periodicity check and vowel/consonant-aware blending. |
| 5 | `real_v2_*_rb` (Rubber Band R3) | "Rubber band sounds phenomenal." |
| 6 | `real_v2_*_rb` after the edge-blend change | "That really does sound good now!" (earlier: "subtle hitches on some of the audio where the note blends back in, but it is not like record skipping or digital buffering"). |

**Decision recorded (owner):** Rubber Band R3 is the default stretch engine (ADR-0026 amendment 1); the project reuses known, tested third-party code where it exists (CLAUDE.md rule 9). The pitch-edit engine decision (SPTK/REAPER or Rubber Band) waits for Phase 6 or 7.
