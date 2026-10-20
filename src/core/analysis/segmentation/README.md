# core/analysis/segmentation

Note segmentation (layer 1; links `core/types`). Design: `docs/design/phase3-notes.md` §1, ADR-0010;
spec §4.2 `segment_notes`; implementation guide B3.

**Method (Mauch & Dixon 2014 note HMM, fused with onsets).** The f0 curve is smoothed in cents over 150 ms
(a mean over the voiced frames within 120 cents of the frame's own value, so a vibrato is averaged but a
note change is not blurred). HMM states: per pitch bin (3 per semitone, over the track's range) an
attack and a stable state, plus silence. Emissions: a Gaussian (45 cents) of the smoothed f0 around the
bin times the voicing probability; silence emits 1 - voicing. Transitions: attack -> stable 0.65,
attack stays 0.3, attack -> silence 0.05; stable stays (1 - new-note prob - 0.005), stable -> silence
0.005, stable -> attack of a pitch within +-1 semitone gets 80 % of the new-note probability (1e-3 per
frame, 0.98 within +-10 ms of an onset) and any other pitch 20 %; silence stays 0.99. A repeated
same-pitch note therefore needs an onset, and a vibrato cannot split a note without one. Runs shorter
than min_note (60 ms) or 10 pitch frames are absorbed right to left into the following adjacent run (a
glide becomes the attack of the note it reaches) or into a preceding long one, else dropped.
Times: start = the first voiced frame, pulled onto the earliest consonant onset up to 120 ms before it,
else the nearest onset within -30 / +40 ms (never before the previous note's end); end = last voiced
frame + one hop, never past the next start; attack end / release start = where the smoothed f0 first
/ last holds within 40 cents of the note's pitch for 30 ms.

**Limitations / tuning:** every constant above is initial and tuned on synthetic melodies first, then on
the golden vocals at the Phase 3 review. Glides are not notes (they join the note they reach); a
sustained portamento between two notes counts as the second note's attack. The HMM is monophonic.
Memory: frames x (2 bins + 1) ints for the back pointers. Compiled with `-ffp-contract=off`.

## Transient-driven boundaries (ADR-0010a)
Runs from the note HMM are merged (`detail/merge_runs`) unless a strong attack/consonant onset, a non-trill `pitch_change` onset, an unvoiced gap of 30 ms or a median-pitch difference above `merge_max_cents` (80) separates them, so a pitch that wanders around its target is one note. The HMM's new-note prior only reacts to onsets of strength >= `merge_onset_strength`. Limits: slow legato steps without a `pitch_change` onset and notes whose pitch differs by less than 80 cents can be merged; held-out under-segmentation is 8 %.
