# Design note: Phase 3 — analyse(), the CLI command, golden metrics

- **Phase:** 3
- **Status:** implemented (reviewed in two rounds; see Review log and Implementation notes)
- **Spec sections:** architecture §4.2 (`analyse` orchestrator), §3 (`AnalysisResult`); ROADMAP Phase 3 (`anno-cli analyse`, plot export, golden checks); `docs/AUDIO_VERIFICATION.md` §1
- **Folders / CMake targets:** `src/core/analysis/orchestrator` (layer 1), `src/cli/commands/analyse` (layer 7; nlohmann via ADR-0007), `tests/support/analysis_metrics.h`, `tests/golden/`

## Purpose
Wire the stages into `analyse()` for the `melodic` algorithm, expose it as `anno-cli analyse` with a JSON export and an SVG plot a human can check, and define the objective metrics that gate the phase.

## 1. analyse()
```cpp
struct AnalyseParams {
    Algorithm algorithm = Algorithm::melodic;   // others: not_implemented (Phases 11+)
    TrackId track{"t1"};                        // track_id of every Note
    std::uint32_t version = 1;                  // AnalysisResult::version; the caller bumps it on re-analysis
    PitchParams pitch;
    PYinParams pyin;
    OnsetParams onsets;
    SegmentParams segmentation;
    PitchDecompositionParams decomposition;
    PerceivedPitchParams perceived;
    LoudnessParams loudness;
};

/// Runs, in order: read_mono + k_weight (once), the pYIN estimator (`PYinEstimator{params.pyin}`, not the `detect_pitch` convenience), detect_onsets (with the pitch track),
/// segment_notes, then per NoteSpan: note_grid, measure_loudness, decompose_pitch_curve, then
/// builds the Note with centre, drift, mod, amp and loudness filled and perceived_pitch set to the rough
/// pitch, calls perceived_pitch on that Note and stores its result, and only then is the Note final
/// (it is validated with the whole result; harmonics, noise and envelope stay empty until Phase 4; a Note
/// with empty nested tracks is valid). `source_id = src.id()`; tuning reference stays 440 Hz
/// (estimated in Phase 8). Every per-note stage is guaranteed to succeed for a span `segment_notes`
/// returned (at least 10 pitch frames, voiced); if one fails anyway the whole analysis fails with the note id in
/// the message (a bug, never silently dropped), and a contract test asserts every output note has
/// grid.n_frames > 0, curves of that length, a finite loudness and a perceived pitch. Returns a shared immutable snapshot that passes validate().
/// Errors: not_implemented (algorithm other than melodic), invalid_argument (bad params), or any
/// stage's error (message names the stage). The source is never modified. Pure; thread-safe; not
/// real-time safe. A cancel token / EngineContext (spec §4) is not needed yet: analysis of a typical
/// vocal is seconds; added with the background scheduler in Phase 9.
[[nodiscard]] Result<std::shared_ptr<const AnalysisResult>> analyse(const AudioSource& src, const AnalyseParams& params);
```
The pitch estimator and `detect_onsets` take the `AudioSource` and read their own mono mix; only the orchestrator's loudness path shares one `read_mono` result. The repeated read costs time only and is accepted; `MonoAudio` overloads are a later optimisation.

Determinism: the same source and params give a bit-identical result on the same build (no randomness, `-ffp-contract=off`); tested by hashing the result.

## 2. `anno-cli analyse`
`anno-cli analyse <file.wav> [--json <out.json>] [--plot-dir <dir>] [--track-id <id>]`.
- Reads the WAV (`core/io`), runs `analyse`, prints a one-line summary (notes, duration) to stdout.
Structure: `cli/commands/analyse` (argument handling, orchestration), `cli/analysis_json` (JSON writer, links nlohmann; unit-testable without a process, like `cli/scenario_json`) and `cli/plot` (CSV + SVG, no third-party code) are separate targets; `cli/commands` does not link nlohmann.
- `--json`: notes (id, start_s, attack_end_s, release_start_s, end_s, perceived_pitch_cents, pitch_center_cents, loudness_lufs, confidence, separation_in), the onset list, and the pitch track as `{hop_s, start_s, f0_cents[], voicing[], confidence[]}` (NaN as null). Keys are stable and documented in the command's help; the format is for humans and tests, not a persistence format (Phase 7).
- `--plot-dir`: `pitch.csv` (time_s, f0_cents, voicing, confidence), `notes.csv`, and `plot.svg` (dependency-free: pitch curve as a polyline, notes as rectangles at their perceived pitch, onsets as ticks, a MIDI-name axis). Written by a small `cli/plot` helper with no third-party library.
- Exit codes as the other commands (0 ok, 1 error, 2 usage). Errors: not_found / corrupt_data from the WAV reader, analysis errors.

## 3. Metrics (tests/support/analysis_metrics.h, mir_eval definitions)
`raw_pitch_accuracy(est, ref, tolerance)` and `raw_chroma_accuracy` at 50 and 10 cents (voiced reference frames only), `voicing_recall` / `voicing_false_alarm`, `octave_errors` (count of voiced frames where the estimate is within 50 cents of ±1200 of the reference), `note_onset_f(est, ref, window)` (precision/recall/F at ±20 ms, one-to-one matching), `note_pitch_error`.

## 4. Golden recordings (tests/golden/vocals/): Vocadito
**Revised after the owner supplied the corpus** (replaces the earlier hand-made `notes.json` / `pitch.json` format; the owner chose Vocadito, Zenodo record 5578259, CC BY 4.0, listed in `zenodo_data/LISCENSE.txt`). Facts measured on the supplied files:

| Property | Value |
|---|---|
| Tracks | 40 solo vocals, 8.7-38.7 s (817 s total), 16-bit mono 44.1 kHz WAV, 29 singers, several languages |
| Layout | `Audio/vocadito_<n>.wav`; `Annotations/F0/vocadito_<n>_f0.csv`; `Annotations/Notes/vocadito_<n>_notesA1.csv` and `_notesA2.csv` (two independent annotators); `Annotations/Lyrics/` (unused); `vocadito_metadata.csv` (track, singer, average pitch as MIDI-ish number, language) |
| F0 CSV | no header; `time_s,f0_hz` on an even grid of 1/172.27 s (5.805 ms); **0.0 Hz = unvoiced**. 45-81 % of frames are voiced. Dense, so it replaces the sparse "pitch spots" |
| Notes CSV | no header; `onset_s,pitch_hz,duration_s`; pitch in **Hz** (MIDI = 69 + 12 log2(Hz/440)); durations, not end times |
| Annotator agreement | the two annotators disagree on segmentation: A1 has 2237 notes, A2 1876; **onset F-measure between A1 and A2 at +-20 ms is 0.75 on average** (0.48-0.88 per track) |

Consequences for the plan:
1. **Read the native CSVs; no JSON conversion.** `tests/support/vocadito.h` (test-only) parses the three CSVs; the old `<name>.notes.json` / `<name>.pitch.json` formats and their README text are removed. One source of truth, and the full corpus works without a conversion step.
2. **Repository size.** The full corpus is 73 MB and stays out of git (`.gitignore`: `tests/golden/vocals/zenodo_data/`). A **committed subset of 5 tracks** (about 10 MB; chosen to cover male/female, range, language, and both high and low annotator agreement) lives in `tests/golden/vocals/vocadito/` in the same native layout with `LICENSE.md` (attribution). CI and the Phase 3 gate use the subset. The full corpus is used locally for tuning and a report-only run (`ANNO_VOCADITO_DIR=<path>`), so constants are tuned on 35 tracks that the gate does not use.
3. **Pitch metric from the dense f0**: raw pitch accuracy at 50 cents, raw chroma accuracy, voicing recall / false alarm and the **octave-error count**, all against the annotated f0 (voiced reference frames), using the metrics already in `tests/support/analysis_metrics.h`. Gate: zero octave errors is not realistic on 817 s of real singing (annotator f0 is itself corrected pYIN output), so the gate is an octave-error **rate** (threshold in the open question below).
4. **Onset metric against two imperfect references.** Score the estimate against A1 and A2 separately (one-to-one matching, +-20 ms) and gate on the better of the two per track, plus report both. The ROADMAP figure of F >= 0.9 cannot be met against references that agree at 0.75.
5. **Note pitch**: matched notes' perceived pitch within 50 cents of the annotated pitch (as before), now from Hz.
6. **Current analyser, first measurement (all 40 tracks, release build, nothing tuned on this data yet)**: 1353 notes found vs 2237 / 1876 annotated; onset F at +-20 ms 0.28 (A1) / 0.30 (A2), at +-50 ms 0.50 / 0.53; precision 0.37, recall 0.23 at +-20 ms. 88 % of matched notes are within 50 cents; 0.2 % are an octave off. So real singing needs tuning (mostly under-segmentation of legato and melismatic phrases), which is what the golden slice of Phase 3 is for.

7. **Diagnosis and experiments on all 40 tracks (throwaway code, reverted; nothing tuned yet).** The notes are lost mostly in the pitch tracker's voicing decision, not in the STFT (which feeds only the onset detector; pitch is time-domain YIN with a 40 ms window). With `beta_mean = 0.1` only 60 % of annotated-voiced frames are called voiced (and the pitch is right when it is: 59.6 % accuracy at 50 cents).

   | Setting | voicing recall | pitch acc. (50 c) | false voiced | notes found | onset F (+-20 ms, A1) |
   |---|---|---|---|---|---|
   | current (`beta_mean` 0.1) | 0.60 | 0.60 | 0.9 % | 1353 | 0.28 |
   | + zero-phase band-pass 60 Hz-2 kHz | 0.68 | 0.68 | 1.4 % | 1587 | 0.32 |
   | + band-pass 60 Hz-1 kHz | 0.73 | 0.72 | 1.7 % | 1686 | 0.34 |
   | `beta_mean` 0.3 | 0.87 | 0.84 | 4.8 % | 2120 | 0.39 |
   | `beta_mean` 0.3 + band-pass 60 Hz-1 kHz | 0.92 | 0.88 | 7.5 % | 2273 | 0.40 |

   The high-pass (60-80 Hz) alone changes nothing; the low-pass is the part that helps. A pitch-only band-pass in the front end is therefore a real, modest gain, to be derived from `PitchParams` (low-pass relative to `max_f0`, not a fixed 1 kHz, which would hurt sopranos). Onset F stalls near 0.4 because of timing: our starts are a median 10 ms before the annotators' (IQR -31..+10 ms), and `attack_end` is no better (median +16 ms, IQR -4..+32); annotators themselves disagree by more than 20 ms on 25 % of onsets, so +-20 ms against one annotator is a noisy target.
8. **Consonants (owner discussion):** a note starts at its consonant (`preroll`), but its pitch comes only from the voiced/stable part (`perceived_pitch` over `[attack_end, release_start]`), so the consonant is already assigned the vowel's pitch afterwards. **Open decision for later (Phase 4):** whether the consonant itself is pitch-shifted by a correction or left unshifted (unvoiced consonants have no pitch to shift).

**The gate is still not vacuous:** `ANNO_REQUIRE_GOLDEN=<n>` fails the inventory test unless n complete tracks (wav + f0 + both note files) exist; the Phase 3 close uses 3 of the 5 committed tracks or more. The five-plot human check (`docs/reviews/phase3-plots.md`) uses the committed tracks.

**Open (owner decision, ROADMAP gate wording):** what replaces "onset F >= 0.9 at +-20 ms, no octave errors"? Proposal in the review.

## Effect on audio
None: analysis does not render. No audio-verifier run is needed; the audio checks (`-L audio`) still run in the gate.

## Split / join decisions
- `analyse` is an orchestrator only (no maths): a readable list of steps — split function rule 2.
- JSON/plot code lives in the CLI layer; `core/` never sees JSON (ADR-0007) or SVG.
- The SVG writer is a helper in `cli/plot`, not a library dependency: ~100 lines, one caller.

## Dependencies
`core/analysis/{pitch,onsets,segmentation,perceived_pitch,loudness}`, `core/dsp`, `core/types`; CLI: `core/io`, nlohmann.

## Tests
- Unit: `analyse` on a synthetic three-note melody (notes, times, pitches, loudness, validate, determinism hash); a melody with a 70 ms note (short-note path); not_implemented for other algorithms; invalid params; empty source (empty result, valid); the source buffer is bit-identical after analysis; **contract: every note has `grid.n_frames > 0`, curves of that length, finite loudness and perceived pitch**; mono and stereo, 22.05-96 kHz sources.
- CLI process tests (like the render/audition ones): `analyse` writes valid JSON with the documented keys, CSVs and an SVG that contains one `<rect>` per note; exit codes.
- Metric tests: hand-built reference/estimate pairs with known RPA/F values.
- Golden: as in §4 (skipped with a clear message when no WAV is present, so CI stays green until the files are added).

## Open questions
1. JSON key names are fixed here; say if you prefer a different layout.

## Review log
*(structure-reviewer findings go here)*

## Implementation notes
- CLI targets: `cli/analysis_json` (nlohmann, private), `cli/plot` (no third-party code), `cli/commands/analyse`. The JSON writer builds `Json(nullptr)` / `Json(v)` with parentheses: `Json{v}` makes a one-element array (an nlohmann initializer-list pitfall found by the test).
- `anno-cli analyse <file.wav> [--json f] [--plot-dir d] [--pitch-track] [--track-id id]`; exit codes 0 / 1 / 2 as the other commands (process tests `anno_cli.analyse_*`).
- The golden test (`tests/golden/golden_vocals_test.cpp`) prints a warning and passes when no recordings are present; `ANNO_REQUIRE_GOLDEN=<n>` makes the inventory test fail unless n complete recordings (wav + f0 + both note files) exist. The Phase 3 close requires 3.
- `analyse()` result is a `shared_ptr<const AnalysisResult>`; it is deterministic (hash test), leaves the source untouched, and every note passes the completeness contract test (grid frames, curve lengths, finite loudness and pitch).