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
| Golden vocals: onsets +-20 ms, no octave errors | **pending**: owner supplies 3-5 recordings | `tests/golden/vocals/README.md` |
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
- **Golden vocals and the plot check** (above): required to close the phase.
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
