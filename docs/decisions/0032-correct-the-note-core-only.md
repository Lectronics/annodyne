# ADR-0032: correcting only the note core (withdrawn experiment)

- **Status:** withdrawn 2026-10-23. The owner listened to the result and found it "actually worse than before". The code is not in `phase-6`; it is kept on the local branch `phase-6-core-only` (commit c9c2e4a) so it can be revived or mined.
- **Phase:** 6
- **Related:** ADR-0027, ADR-0030 (the behaviour that stays), ADR-0031 items 4 and 6

## What was tried
After the owner heard the 40, 80 and 150 ms glides as identical and described gain dips in open vowels as hard jumps to the next pitch, the Rubber Band engine was changed to (1) apply the offset only over the note's settled core (`pitch_target.core`, else the whole note), ramping in and out over 40 ms inside it; (2) give frames of a class that is not shifted (sibilant, breath, plosive, silence) weight 0 in the same curve; (3) keep the original samples where the offset is effectively zero (the shifted audio fades in over 2-10 % of the full offset); all through the live shifter, with the touching-notes context of ADR-0030 removed (cache key 9).

## What was measured (full-song Correct Pitch, `vocadito_2`, 4 ms windows against the source)
Windows more than 6 dB under the source: 19 -> 8 (PSOLA 4); more than 3 dB under: 93 -> 48 (PSOLA 37); cycle-similarity dips 147 -> 116; the click detector passed (0 new). The measures improved; the owner's ear preferred the earlier render. The measures therefore do not capture what the owner hears, and a likely cost is the move of every pitch edit from the offline stretcher to the live shifter (short window), which the measures do not rate.

## Decision
Back out: `phase-6` returns to the ADR-0030 behaviour (offline constant offset for the zone, a glide between touching notes). The core-window idea, the class weights and the keep-original blend are not discarded as ideas; any of them can come back as an option selected per scenario once there is a way to hear it against the current default without losing the offline path.

## Consequences
- The cache key version is 8 again.
- No default changes; the owner's A/B packages are re-rendered from the restored behaviour.
