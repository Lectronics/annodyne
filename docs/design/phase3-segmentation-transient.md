# Design note: Transient-driven note segmentation

- **Phase:** 3 (follow-up slice, stage 2 of the pitch-target work)
- **Status:** implemented (stage 2, 2026-10-20); measurements in ADR-0010a
- **Spec sections:** implementation guide B3 (note segmentation); architecture spec §4.2 `segment_notes`; ADR-0010
- **Folder / CMake target:** `src/core/analysis/segmentation/` (layer 1), new `detail/merge_runs.{h,cpp}`
- **Related:** `note-pitch-target.md` (what happens to a note's pitch after it is segmented)

## Purpose
Owner review of Vocadito tracks 13 and 28 found notes split at the peaks of a hunting pitch although the waveform shows one transient and a decaying note, notes starting before the transient they belong to, and scoops split in two. Principle from the owner: a transient happens almost every time the pitch changes, except for legato. So a note boundary needs evidence beyond a pitch wiggle.

## Public types
No new public type. `SegmentParams` (struct, caller-owned, value) gains:

```cpp
Seconds min_gap_for_split{0.030};     // unvoiced/silent gap that always separates two notes
double  merge_onset_strength = 1.5;   // OnsetList::strength (activation / threshold, >= 1) a transient needs to keep two runs apart
Cents   merge_max_cents{80.0};  // was drafted as 150; 80 measured best (see ADR-0010a)       // runs whose robust centres differ more are never merged (a missed transient, not a wobble)
Seconds snap_before{0.030};           // today's k_onset_zone_s, now tunable
Seconds snap_after{0.040};            // today's k_snap_after_s, now tunable
```

`segment_notes` signature, `NoteSpan`, `Note` and `OnsetList` are unchanged, but `SegmentParams` is a public type and the documented behaviour changes, so this is recorded in an amendment to ADR-0010 (ADR-0010a, written with the implementation); `validate_params` checks the new fields (positive, finite) with error tests. Defaults are provisional and fixed by measurement (stage 2 of the plan).

## Public functions
`segment_notes(const PitchTrack&, const OnsetList&, const SegmentParams&)` keeps its contract (see `segmentation.h`). Internal addition (detail, not public):

```cpp
/// Merges adjacent runs of `runs` (frame indices on the pitch grid, SourceTime) that nothing
/// separates. Pure; thread-safe; not real-time safe.
std::vector<NoteRun> merge_runs(std::vector<NoteRun> runs, const MergeInput& in);
// MergeInput { smoothed_cents, pitch, onsets, params } bundles the data clump (join rule 1).
```

| | |
|---|---|
| Inputs | HMM runs (pitch-grid frames), smoothed f0 in cents (A4 = 6900), pitch track voicing, onsets (SourceTime seconds) |
| Rule | Two adjacent runs stay apart if (a) an onset of kind `attack`/`consonant` with strength >= `merge_onset_strength` lies between their voiced parts (+- one onset evidence window), or (b) a `pitch_change` onset lies between them (legato step: two stable stretches, existing detector), or (c) an unvoiced gap >= `min_gap_for_split` lies between them. Otherwise they merge, provided the medians of their smoothed f0 differ by <= `merge_max_cents` |
| Returns | the merged run list, order and frame indices preserved |
| Errors | none beyond `segment_notes`' own invalid_argument checks |
| Pure / thread-safe / real-time | yes / yes / no |
| Cost | O(runs + onsets) |

Order inside `segment_notes`: HMM -> **merge_runs** -> absorb short runs (merging first, so a transient-separated short staccato run is never swallowed before it is judged; the merged run's bin is the longer run's, and it spans first..last frame including the unvoiced frames between) -> `make_drafts` -> `place_times` -> attack/release -> statistics -> `drop_unusable`.

Note start placement: `snap_start` already snaps to the nearest onset in `[start - snap_before, start + snap_after]` (0.030 / 0.040 s, previously constants). Nothing new is added here; the two constants become `SegmentParams` fields so stage 2 can tune them, and the n011611 case (plan: track 13) is measured first.

**One onset policy.** The HMM prior no longer raises `new_note_prob` for every onset: only onsets with strength >= `merge_onset_strength` do (so one threshold, one place). `merge_runs` therefore only handles what the HMM cannot see: `pitch_change` (legato) onsets, gaps and the centre guard. `k_onset_evidence_s` moves to a shared `detail/onset_evidence.h`. `OnsetKind::phoneme`, if present, is ignored.

**Trill clusters.** `pitch_change` onsets that belong to a cluster of >= 3 alternating-direction steps, each plateau <= 150 ms, do not separate runs, so a trill stays one note for the pitch-target `ornament` check (note-pitch-target.md §12 B8).

## Effect on audio
None. Analysis only; nothing is rendered or edited. Samples are never read here beyond the existing onset list. No audio-verifier run; the objective checks are the unit tests and the golden metrics. A human re-checks the plots of tracks 13 and 28.

## Split / join decisions
- `merge_runs` is its own kernel (split rule 4: independently testable on synthetic run lists) and does not touch the HMM (rule 3: different reason to change).
- The merge rule is one function; the three separators are small predicates inside it (keep-together rule 1).

## Dependencies
`core/types`, existing `onsets` types (`OnsetList`). No new target links; no layer change.

## Tests (written first; synthetic, known answers)
1. One transient, pitch hunting +-40 cents around a target, decaying level -> one note, pitch = mean +-5 cents.
2. Same pitch twice with a transient between -> two notes (repeated notes).
3. Legato step of 100 cents (stable 80 ms each side) without a transient -> two notes (pitch_change separates); a 200 cent step stays two by the centre guard.
4. Scoop of 150 cents over 120 ms into a plateau with no mid transient -> one note.
5. The same scoop with a transient mid-way -> two notes.
6. Vibrato +-50 cents at 5.5 Hz -> one note (existing test unchanged).
7. An unvoiced gap of 40 ms (and exactly 30 ms) between equal pitches -> two notes.
8. A run pair with centres 300 cents apart and no onset -> stays two (merge_max_cents guard).
9. A trill (6 Hz, 100 cents, flat-topped) stays one note after segmentation.
10. Onset weaker than `merge_onset_strength` between runs -> merged; exactly at the threshold -> kept apart; `merge_runs` on empty and single-run lists; chain of three runs 100 cents apart each (drift 200) -> merges only while each neighbour pair is within the guard (intended, stated).
11. A real `detect_onsets` run on a synthetic hunting note (not a hand-built list).
Existing segmentation tests pass unchanged. Golden: no drop in onset F20/F50 or matched-note pitch; over-segmentation rate (annotated notes covered by more than one of ours) falls; plot-sheet cases fixed.

## Open questions
1. Initial value of `merge_onset_strength` and the transient look-ahead: measured on the tuning tracks, not guessed.
2. Is a `pitch_change` onset sufficient to separate every legato note, or do slow legato steps (> 65 ms transition) need the target-core's level/moving segmentation (note-pitch-target.md §3)? If so, stage 3 may split a merged note; recorded in the review.

## Review log
Structure review 2026-10-19: B7 (transient look-ahead duplicated `snap_start`) -> removed, parameters exposed instead; B8 (S vs `multi`, trills, test 3 and 7 wording) -> trill clusters, tests fixed, `multi` is the target note's fallback; S8 (two onset policies) -> one threshold in the HMM prior; S9 (bin, order, bundle, chain, phoneme) -> stated above; S10 (ADR-0010a, validate_params, README/CMake, golden re-baseline by the implementer with owner sign-off) -> done at implementation; S11 tests added. Re-analysis changes note ids: `remap_after_reanalysis` applies to stored edits.
