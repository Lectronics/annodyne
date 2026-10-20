# ADR-0010a: Transient-driven note boundaries (amends ADR-0010)

- **Date:** 2026-10-20
- **Status:** accepted 2026-10-20 (owner approved the transient-segmentation plan)
- **Phase:** 3 (follow-up slice)

## Context
Owner review of Vocadito tracks 13 and 28: one note with one transient and a hunting pitch was split at the pitch peaks. Measured on 35 tuning tracks, 18.6 % of annotated notes held two or more of our notes.

## Decision
`SegmentParams` gains `merge_onset_strength` (1.5), `merge_max_cents` (80), `merge_onset_zone` (40 ms), `min_gap_for_split` (30 ms), `trill_max_plateau` (150 ms), `trill_min_steps` (3), `snap_before` (30 ms) and `snap_after` (40 ms; the last two were constants). Behaviour of `segment_notes` changes:
1. The note HMM raises its new-note probability only at onsets of strength >= `merge_onset_strength`.
2. Adjacent HMM runs are merged (`detail/merge_runs`) unless a strong `attack`/`consonant` onset or a non-trill `pitch_change` onset lies within `merge_onset_zone` of the boundary, the unvoiced gap is >= `min_gap_for_split`, or their median pitches differ by more than `merge_max_cents`. Merging precedes the absorption of short runs.
3. `pitch_change` onsets of a trill cluster do not split.
`segment_notes`' signature, `NoteSpan` and `Note` are unchanged. `merge_max_cents` 80 was chosen on the tuning tracks (40-150 tried).

## Measured (tuning / held-out)
Over-segmentation 18.6 % / 18.8 % -> 10.2 % / 8.9 %; under-segmentation 1.2 % / 4.3 % -> 3.5 % / 8.0 %; onset F at +-20 ms (annotator 1) 0.531 / 0.439 -> 0.556 / 0.472; at +-50 ms 0.753 / 0.677 -> 0.776 / 0.715; notes found 2052 -> 1790 (annotated 1923).

## Consequences
Golden guards raised (`tests/tolerances.h`). Track 28 n001330/n001450 (84 cents apart) still end as two notes; the level/moving target stage and, if needed, a higher guard handle it later. Re-analysis changes note ids; stored edits re-map through `remap_after_reanalysis`.
