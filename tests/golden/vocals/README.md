# tests/golden/vocals

Real solo-vocal recordings with human annotations, for the Phase 3 gate. Design: `docs/design/phase3-orchestrator-cli.md` §4.

## Corpus: Vocadito
40 solo vocals with f0, note and lyric annotations by two annotators. Zenodo record 5578259
(https://zenodo.org/records/5578259), licence **CC BY 4.0** (https://creativecommons.org/licenses/by/4.0/;
confirmed by the owner; the supplier's `zenodo_data/LISCENSE.txt` says only "CC 4.0").

**Attribution (required by CC BY):** R. M. Bittner, K. Pasalo, J. J. Bosch, G. Meseguer-Brocal, D. Rubinstein,
"vocadito: A dataset of solo vocals with f0, note, and lyric annotations", arXiv:2110.05580, 2021; Zenodo
record 5578259. The committed tracks are unmodified copies; changes, if any are ever made, must be stated here.
The same notice is in the root `THIRD_PARTY.md`. (Citation text written from memory: verify it against the
Zenodo record.)

| Where | What |
|---|---|
| `zenodo_data/` | the full 73 MB corpus as supplied. **Not committed** (`.gitignore`); for local tuning via `ANNO_VOCADITO_DIR` |
| `vocadito/` | the committed subset in the same layout: tracks **2, 4, 13, 28, 37** (11 MB). They were kept out of all tuning (`vocadito_report --tune` uses the other 35) and chosen to vary singer, range (78-455 Hz), language (Spanish, Catalan, French x2, Hawaiian+English) and annotator agreement (0.60-0.83) |

## File formats (native; the tests read them directly)
| File | Content |
|---|---|
| `Audio/vocadito_<n>.wav` | 16-bit mono 44.1 kHz |
| `Annotations/F0/vocadito_<n>_f0.csv` | no header; `time_s,f0_hz`, even grid of 1/172.27 s; **0.0 = unvoiced** |
| `Annotations/Notes/vocadito_<n>_notesA1.csv`, `_notesA2.csv` | no header; `onset_s,pitch_hz,duration_s`; two independent annotators who agree on only 75 % of onsets at +-20 ms |
| `vocadito_metadata.csv` | `track_id,singer_id,average_pitch,language` |

Metrics and gate: see the design note §4 (onsets scored against each annotator, better one gates; f0 from the
dense track; `ANNO_REQUIRE_GOLDEN=<n>` makes the inventory test fail unless n complete tracks exist).

Plots for the human check: `anno-cli analyse <wav> --plot-dir <dir>` (see `docs/reviews/phase3-plots.md`).

## Gate and tuning tool
`golden_vocals_test` (label `golden`) analyses the committed tracks (in parallel) and checks the aggregate
against a **regression guard** in `tests/tolerances.h` (voicing recall, pitch accuracy, octave-error rate,
note-start F-measure at +-20 / +-50 ms against the better annotator, matched-note pitch); the numbers were
measured on these held-out tracks and rounded down with a margin. It is a guard against regressions, not a
quality claim, and the ROADMAP wording ("F >= 0.9, no octave errors") is superseded only once the owner
signs the replacement in an ADR. `ANNO_REQUIRE_GOLDEN=3 ctest --preset release -L golden` also fails if
fewer than three complete tracks are committed.

`build/release/bin/vocadito_report <corpus dir> [first-last] [--tune|--holdout] [-v]` prints the same scores
for the full corpus; `ANNO_TUNE="beta=0.3,lp=1,preroll=0.03"` overrides parameters for sweeps (not a test,
never run by ctest or CI).
