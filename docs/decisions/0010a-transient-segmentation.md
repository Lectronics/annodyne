# ADR-0010a: Transient-driven note boundaries (amends ADR-0010)

- **Date:** 2026-10-20
- **Status:** accepted 2026-10-20 (owner approved the transient-segmentation plan)
- **Phase:** 3 (follow-up slice)

## Context
Owner review of Vocadito tracks 13 and 28: one note with one transient and a hunting pitch was split at the pitch peaks. Measured on 35 tuning tracks, 18.6 % of annotated notes held two or more of our notes.

## Decision
`SegmentParams` gains `merge_onset_strength` (1.5), `merge_max_cents` (80), `merge_onset_zone` (40 ms), `min_gap_for_split` (30 ms), `trill_max_plateau` (150 ms), `trill_min_steps` (5), `snap_before` (30 ms) and `snap_after` (40 ms; the last two were constants). Behaviour of `segment_notes` changes:
1. The note HMM raises its new-note probability only at onsets of strength >= `merge_onset_strength`.
2. Adjacent HMM runs are merged (`detail/merge_runs`) unless a strong `attack`/`consonant` onset or a non-trill `pitch_change` onset lies within `merge_onset_zone` of the boundary, the unvoiced gap is >= `min_gap_for_split`, or their median pitches differ by more than `merge_max_cents`. Merging precedes the absorption of short runs.
3. A trill (>= `trill_min_steps` alternating pitch steps of >= 60 cents found in the pitch track, plateaus up to `trill_max_plateau`) is not split: onsets inside it do not separate runs and the centre guard is lifted there, so the pitch-target stage can classify it as an ornament (design note note-pitch-target.md §12 B8). 5 steps was chosen on the tuning tracks (3 merged real fast melodic alternations).
`segment_notes`' signature, `NoteSpan` and `Note` are unchanged. `merge_max_cents` 80 was chosen on the tuning tracks (40-150 tried).

## Measured (tuning / held-out)
Over-segmentation 18.6 % / 18.8 % -> 10.2 % / 8.9 %; under-segmentation 1.2 % / 4.3 % -> 3.5 % / 8.0 %; onset F at +-20 ms (annotator 1) 0.531 / 0.439 -> 0.556 / 0.472; at +-50 ms 0.753 / 0.677 -> 0.776 / 0.715; notes found 2052 -> 1790 (annotated 1923).

## Consequences
Golden guards raised (`tests/tolerances.h`). Track 28 n001330/n001450 (84 cents apart) still end as two notes; the level/moving target stage and, if needed, a higher guard handle it later. Re-analysis changes note ids; stored edits re-map through `remap_after_reanalysis`.

## Amendment 2026-10-20: a ramp is not a note

Owner review of Vocadito track 28 (0.66 s): a roll of about 380 cents in 100 ms into a note was
split from it, because the medians of the two runs differ by more than `merge_max_cents` (80).
`merge_runs` now skips that median rule when the earlier run is a ramp: at most 120 ms long and
travelling at least 150 cents (raw f0) from its start to the start of the next run, toward it.
Transients, gaps and `pitch_change` onsets still separate runs. Measured on the 35 tuning tracks:
onset F20 0.556 -> 0.554, matched-note pitch 0.824 -> 0.811, under-segmentation 3.5 -> 4.4 %; held-out
onset F20 0.476 -> 0.469, under-segmentation 8.5 -> 9.4 %. Accepted as the cost of the owner's rule;
thresholds are constants in merge_runs.cpp. Variants that merged more (no travel requirement, 100
cents) cost more onset F and were rejected.

## Amendment 2026-10-20 (2): legato level steps are onsets

Owner review of track 28 at 0.88 s: two legato notes at nearly the same pitch, separated only by a
level step (about 4 dB over 40 ms) with no spectral change, so SuperFlux found nothing even at
delta 0.5. `detect_onsets` now adds `OnsetKind::attack` onsets from the 20 ms RMS level
(`detail/energy_step`): the mean of the 60 ms after a frame exceeds the mean of the 60 ms before by
`energy_step_db` (3.5), at the local maximum, strength = rise / `energy_step_strength_db` (2.0).
Within `min_gap` of a spectral onset the stronger wins (merge); 30-60 ms away the step is dropped
as a duplicate. Measured (35 tuning / 5 held-out): onset F20 A1 0.554/0.469 -> 0.570/0.515,
matched-note pitch 0.811/0.784 -> 0.820/0.803, under-segmentation 4.4/9.4 % -> 3.0/4.6 %;
over-segmentation 9.4/7.6 % -> 11.3/10.2 %; APC (-10/+15) 0.605/0.569 -> 0.615/0.530. The held-out
APC fall is the cost of more, shorter notes; guard 0.52 still holds.
