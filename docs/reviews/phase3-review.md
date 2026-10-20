# Phase 3 review: Monophonic analysis

- **Status:** OPEN (code complete). Closes when (1) three golden recordings with hand-checked note lists
  and pitch spots are in `tests/golden/vocals/` and `ANNO_REQUIRE_GOLDEN=3 ctest --preset release -L golden`
  passes, and (2) the five note plots are checked in `docs/reviews/phase3-plots.md`.
- **Git tag:** `phase-3` (set at close)

## Built
Design notes: [`phase3-dsp`](../design/phase3-dsp.md), [`phase3-pitch-onsets`](../design/phase3-pitch-onsets.md),
[`phase3-notes`](../design/phase3-notes.md), [`phase3-orchestrator-cli`](../design/phase3-orchestrator-cli.md).
ADRs: 0009 (pocketfft), 0010 (analysis signatures).

- **`core/dsp`:** `RealFft` (pocketfft, private dependency), windows, `stft`/`istft`, `resample`, `k_weight`
  (BS.1770), `interp_curve`, `MonoAudio`/`read_mono`.
- **`core/analysis/pitch`:** `PitchEstimator` interface, `PYinEstimator` (YIN by FFT autocorrelation,
  Beta-prior candidates, 10-cent HMM, alternates, lag-centre correction), `detect_pitch`.
- **`core/analysis/onsets`:** SuperFlux with energy refinement, offset rejection, pitch-change onsets.
- **`core/analysis/segmentation`:** note HMM fused with onsets, glide absorption, boundary times, `NoteSpan`.
- **`core/analysis/perceived_pitch`, `loudness`:** stability-weighted perceived pitch, centre/drift/mod
  decomposition, per-note LUFS and amp curve.
- **`core/analysis/orchestrator`:** `analyse()` for `melodic`.
- **`core/types`:** `NoteSpan`, `note_grid`, `idx`, `is_voiced`.
- **CLI:** `anno-cli analyse` (JSON, CSV, SVG plots).
- **Tests:** 420 CTest tests locally in debug-ASan/UBSan (release 418 before the review fixes; TSan clean).

## Gates
| Gate | Result | Evidence |
|---|---|---|
| Structure review of design notes | Round 1 CHANGES REQUIRED (7 blocking), round 2 APPROVED WITH FIXES; fixed before coding | design notes, Review logs |
| Structure review of the implementation | APPROVED WITH FIXES (1 blocking for the close = golden data, 10 should-fix, all fixed) | review on branch `phase-3` |
| Build, tests, ASan/UBSan, TSan, release | green locally (see CI row) | local runs |
| clang-tidy, clang-format | clean (pinned 23.1.3) | local runs, CI |
| CI (Linux, macOS, Windows, tidy) | see the closing commit | GitHub Actions |
| ROADMAP synthetic accuracy | **met** (numbers below) | `pitch_test`, `onsets_test`, `segmentation_test` |
| Golden vocals (Vocadito, 5 held-out tracks) | gate runs and passes as a regression guard (voicing recall 0.92, pitch accuracy 0.86, octave rate 0.054, onset F 0.39 at +-20 ms / 0.65 at +-50 ms, matched-note pitch 0.72). **The ROADMAP wording (F >= 0.9, no octave errors) is NOT met and cannot be on this data** (annotators agree at 0.75); replacement gate needs the owner's ADR | `docs/design/phase3-activity.md`, `tests/golden/vocals/README.md` |
| Human check of 5 note plots | **pending** | `docs/reviews/phase3-plots.md` |
| Audio checks | no rendered audio changes; `-L audio` still passes (null, undo-null, untouched, click, sanity, determinism, length) | local runs |

## Deviations from the specs
ADR-0010: `segment_notes` returns `NoteSpan`s; `decompose_pitch_curve`, `measure_loudness`, `detect_onsets`
and `perceived_pitch` take the pieces they need; one note-grid rule; loudness integrated per note.
ADR-0009: pocketfft as the FFT. Recorded in the notes: the octave penalty was dropped; `voicing_prob` is
the observed voiced mass forced to the path's side of 0.5; onsets are modelled as strong evidence
(`onset_new_note_prob`) instead of a multiplier.

## Objective numbers (synthetic ground truth)
- Stationary harmonic tones 80-1200 Hz at 44.1/48 kHz: **max f0 error 0.92 cents** (gate 3 cents).
- Vibrato (5.5 Hz, +-50 cents), one-octave-per-second glide, harmonic tone in noise (20 dB SNR), 22.05-48 kHz,
  mono and stereo: 10-cent accuracy >= 95 %, 0 octave errors (a weak fundamental 26 dB down is not an octave
  error, and the octave is in `alternates`).
- Onsets: tone bursts within 5 ms; a burst 40 dB below a loud one is found; no false onsets on a +-200 cents
  vibrato with 25 partials (plain flux: 5); a legato 200-cent step is one `pitch_change` onset within 15 ms.
- Notes: onsets within 20 ms, offsets within 30 ms, pitch within 10 cents on synthetic melodies; repeated
  same-pitch notes split only at an onset; a 60 ms-or-shorter blip is dropped; 150 random messy tracks
  never produce a note the description stages reject.
- Loudness: a mono sine reads 20 log10(a) - 3.01 LUFS; a 180 ms and a 2 s note of one level agree to 0.15 LU.
- Speed: release analysis runs far faster than real time (the 5 slowest debug-ASan analyse cases take
  about 3 s for 2-3 s of audio).

## Open issues carried forward
- **Golden gate wording** (owner decision + ADR) and **the plot check** (above): required to close the phase.
- ADR-0011 added the internal activity stage and the pitch front end after the first real-vocal measurements (`phase3-activity.md`).
- **All HMM, onset and weighting constants are initial values tuned on synthetic signals** (bin size, sigma,
  transition probabilities, `delta`, `min_activation`, `log_lambda`, stability scale 600 cents/s, trims, the
  2.5 Hz drift cutoff). Tune on the golden set and record the final values in the module READMEs.
- Glides join the note they reach (no portamento notes); sibilant/breath frames are not excluded from the
  loudness until the noise track exists (Phase 4).
- `pitch` and `onsets` each read their own mono mix; no cancel token or progress until Phase 9.
- Carried from earlier phases: click-detector thresholds and crossfade shape (Phase 4), order of stretch and
  pitch correction (owner), the two read-only specs still describing neighbour stretching.
- Next: Phase 4 (first audible edit, pitch). Needs a human listening session.

## Split/join and cleanup done this phase
After the implementation review: `NoteSpan`/`note_grid` moved to `core/types`; one `is_voiced`; internal headers in
`detail/`; `PYinEstimator::estimate`, `segment_notes` and `onsets` split into named steps; `YinFrame::analyse`
returns a struct; `-ffp-contract=off` also on `core/types`.


## Addendum 2026-10-20: transient segmentation and pitch target

Built (design: `phase3-segmentation-transient.md`, `note-pitch-target.md` sections 11-18; ADR-0010a, ADR-0012, ADR-0013): transient-driven note boundaries; `PitchTarget` replacing `Note::perceived_pitch` (kinds whole/settled/glide/multi/ornament/unpitched, approach/release labels, flags short_note/creak/ambiguous_intent/register_break/run/consistent_off_grid/expressive_peak); `core/tuning` style presets (pop validated, others provisional); plot and JSON export of the target.

Verification: debug-asan 509/509 (with `ANNO_REQUIRE_GOLDEN=3`), debug-tsan 503/503 before the audit fixes, release green; clang-format and clang-tidy clean on the changed files. Structure audit (CHANGES REQUIRED): B1-B4 fixed (default target validates for any centre; `annotate_take` returns `Result` and parameters are validated per group; missing segmentation tests added), S1, S2, S3, S9, S10 handled in the ADR/design text. No audio is rendered; no audio-verifier run.

Held-out 5 (annotator 1): onset F +-20 ms 0.472, +-50 ms 0.715; over-segmentation 18.8 % -> 8.9 %, under-segmentation 4.3 % -> 8.5 %; matched-note pitch within 50 cents 0.740 -> 0.787; within -10/+15 cents 0.520 -> 0.569 (plain median of the note: 0.631, see below).

**Owner decisions needed**
1. The pitch target does not beat the plain median against the annotators (0.569 vs 0.631) because it excludes scoops and falls by design while the annotators' pitch includes them. Accept (the guard stays a regression guard at 0.52), or change what "correct" means for the metric?
2. `glissando_g` 0.32 (accepted 2026-10-20), default correction tolerance -10/+15 (Phase 4 gate), style table values other than pop (provisional).
3. Citations in the pitch-target note are the owner's research and unverified.

**Carried from the audit (not done)**: direct tests of the detail kernels (`frame_reliability`, `find_ornament`, `split_curve`, `label_segments`); named constants for remaining magic numbers (confidence caps, 100 cents per semitone, the step threshold shared between `merge_runs` and `SegmentingParams`); splitting `estimate_pitch_target` (about 85 lines) and computing the vibrato period once per note; `perceived_pitch(Note)` has no production caller (decide at close); `weighted_median` returns a bare double with NaN for "no weight"; the trill `analyse` test allows up to 2 notes; a slow-legato-pair integration test; tuning reference single source at Phase 8.


## Addendum 2026-10-20 (2): segmentation fixes, voicing review, vendored c4dm/pyin

**Segmentation (owner review of track 28)**: a short ramp into the next pitch stays in that note; legato level steps (about 4 dB) are onsets (`core/analysis/onsets/detail/energy_step`); `Note::attack_end` is the consonant | vowel boundary (start of voicing); ADR-0010a amendments 1-3. Consonants stay grouped with the pitch for now (owner).

**Voicing review** (`docs/design/phase3-voicing-review.md`, outside review of commit `1a0882c`): its probes were ported to C++ (`tests/support/voicing_probes.h`, `tests/unit/voicing_probes_test.cpp`, tolerances justified in `tests/tolerances.h`). On the annodyne pYIN four rows failed (tail below -52 dB, lone hum, lone -80 dBFS tone, voiced 'z'); several claims did not reproduce (`beta_mean` was already 0.3; the activity gate removes hum between notes; breathy singing was voiced to 0 dB HNR). A narrower design (`voicing.md`: drop gate, level weight, hum classifier) was drafted and reviewed, then parked.

**Pitch tracker switched to the vendored c4dm/pyin** (owner decision; ADR-0014, `docs/design/c4dm-pyin-integration.md`; GPL-2.0-or-later, owner-approved). All 17 probe rows pass. Vocadito (35 tuning / 5 held-out), annodyne pYIN -> c4dm: voicing recall 0.959 / 0.967 -> 0.989 / 0.997; pitch within 50 cents 0.943 / 0.948 -> 0.982 / 0.995; octave and harmonic errors 0.010 / 0.013 -> 0.002 / 0.000; matched-note pitch 0.820 / 0.803 -> 0.878 / 0.818; target within -10/+15 cents 0.615 / 0.530 -> 0.694 / 0.578; onset F +-20 ms (annotator 1) 0.570 / 0.515 -> 0.541 / 0.519, +-50 ms 0.775 / 0.710 -> 0.729 / 0.694 (the segmentation was tuned on the old tracker and is not re-tuned). A sub-octave guard (not in the plugin, off-switchable) removes the plugin's f0 / 3 errors on track 34 (51.8 % -> 3 %). Golden guards raised: recall 0.95, RPA 0.95, octave rate 0.01, matched-note pitch 0.78, -10/+15 share 0.55.

Verification: release 544+ tests, debug-asan green (it caught an out-of-range read in the probe generator, fixed), layer fixtures for the new allowlist entry, clang-format and clang-tidy clean on the changed files; structure review of the integration (CHANGES REQUIRED, 2 blocking, resolved: design note review log). No audio is rendered; no audio-verifier run.

**Still open**
- Owner: the project licence decision that follows from ADR-0014 (and JUCE / ARA SDK compatibility); the plot check of tracks 13 and 28 and a trill/glide example; whether the target-vs-median -10/+15 result is accepted (now 0.578 vs 0.667 held-out); non-pop style presets; unverified citations.
- Residual octave errors (0.2 % tuning, tracks 34 and 40); onset F far from 0.9 (annotators agree at 0.75); the plugin's absolute low-amplitude rule (take-relative reference); the 61.7 Hz floor; segmentation not re-tuned for the new tracker.
- Carried audit items of the previous addendum; transient/consonant analysis to be replaced by a more accurate method (owner).

## Addendum 2026-10-20 (3): weak-voicing fix, voicing review re-check, fuzzy thresholds

**Track 28 (owner)**: the roll up from 11.958 s that settles at 12.083 s is a note of its own. The note HMM read the tracker's weakly voiced frames (voicing 0.5) as silence; `SegmentParams::voiced_floor_prob` 0.55 fixes it (ADR-0010a amendment 4). Notes 1544 -> 1671 of 1923; onset F20 A1 0.541 / 0.519 -> 0.582 / 0.552; matched-note pitch 0.878 / 0.818 -> 0.853 / 0.803; target within -10/+15 cents 0.694 / 0.578 -> 0.655 / 0.552 (guard 0.55; margin thin, 0.6 would break it). Track 13: owner reads it as mostly fine; unchanged.

**Voicing review re-check on the c4dm tracker**: the 17 ported probe rows all still pass after the change; voicing recall (0.989 / 0.997), pitch within 50 cents (0.982 / 0.995) and octave rate (0.002 / 0.000) are identical to before, because the change is in the segmentation, not the tracker. The four rows that failed on the annodyne pYIN stay fixed. The review's remaining proposals (drop gate, level weight, hum classifier, note HMM) are not needed for the probes and are not adopted.

**Fuzzy thresholds (owner)**: the fixed thresholds of Phase 3 are provisional. With more analysis tools (transient and consonant tags, key, tuning reference, tempo) they can become graded memberships that change locally with the tags of the current waveform and globally per performance. Recorded in the ROADMAP (Phase 3 carried items and Phase 8). Nothing is built.

