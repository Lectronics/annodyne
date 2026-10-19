# Design note: Phase 3 — analyse(), the CLI command, golden metrics

- **Phase:** 3
- **Status:** draft
- **Spec sections:** architecture §4.2 (`analyse` orchestrator), §3 (`AnalysisResult`); ROADMAP Phase 3 (`anno-cli analyse`, plot export, golden checks); `docs/AUDIO_VERIFICATION.md` §1
- **Folders / CMake targets:** `src/core/analysis/orchestrator` (layer 1), `src/cli/commands/analyse` (layer 7; nlohmann via ADR-0007), `tests/support/analysis_metrics.h`, `tests/golden/`

## Purpose
Wire the stages into `analyse()` for the `melodic` algorithm, expose it as `anno-cli analyse` with a JSON export and an SVG plot a human can check, and define the objective metrics that gate the phase.

## 1. analyse()
```cpp
struct AnalyseParams {
    Algorithm algorithm = Algorithm::melodic;   // others: not_implemented (Phases 11+)
    TrackId track{"t1"};
    std::uint32_t version = 1;                  // AnalysisResult::version; the caller bumps it on re-analysis
    PitchParams pitch;
    OnsetParams onsets;
    SegmentParams segmentation;                 // .track is overwritten with `track`
    PitchDecompositionParams decomposition;
    PerceivedPitchParams perceived;
    LoudnessParams loudness;
};

/// Runs, in order: mono mix + K-weighting (once), detect_pitch, detect_onsets, segment_notes, then per
/// note decompose_pitch_curve, measure_loudness, perceived_pitch, and fills a Note (harmonics, noise and
/// envelope stay empty until Phase 4; a Note with empty nested tracks is valid). Tuning reference stays
/// 440 Hz (estimated in Phase 8). Returns a shared immutable snapshot that passes validate().
/// Errors: not_implemented (algorithm other than melodic), invalid_argument (bad params), or any
/// stage's error (message names the stage). The source is never modified. Pure; thread-safe; not
/// real-time safe. A cancel token / EngineContext (spec §4) is not needed yet: analysis of a typical
/// vocal is seconds; added with the background scheduler in Phase 9.
[[nodiscard]] Result<std::shared_ptr<const AnalysisResult>> analyse(const AudioSource& src, const AnalyseParams& params);
```
Determinism: the same source and params give a bit-identical result on the same build (no randomness, `-ffp-contract=off`); tested by hashing the result.

## 2. `anno-cli analyse`
`anno-cli analyse <file.wav> [--json <out.json>] [--plot-dir <dir>] [--track-id <id>]`.
- Reads the WAV (`core/io`), runs `analyse`, prints a one-line summary (notes, duration) to stdout.
- `--json`: notes (id, start_s, attack_end_s, release_start_s, end_s, perceived_pitch_cents, pitch_center_cents, loudness_lufs, confidence, separation_in), the onset list, and the pitch track as `{hop_s, start_s, f0_cents[], voicing[], confidence[]}` (NaN as null). Keys are stable and documented in the command's help; the format is for humans and tests, not a persistence format (Phase 7).
- `--plot-dir`: `pitch.csv` (time_s, f0_cents, voicing, confidence), `notes.csv`, and `plot.svg` (dependency-free: pitch curve as a polyline, notes as rectangles at their perceived pitch, onsets as ticks, a MIDI-name axis). Written by a small `cli/plot` helper with no third-party library.
- Exit codes as the other commands (0 ok, 1 error, 2 usage). Errors: not_found / corrupt_data from the WAV reader, analysis errors.

## 3. Metrics (tests/support/analysis_metrics.h, mir_eval definitions)
`raw_pitch_accuracy(est, ref, tolerance)` and `raw_chroma_accuracy` at 50 and 10 cents (voiced reference frames only), `voicing_recall` / `voicing_false_alarm`, `octave_errors` (count of voiced frames where the estimate is within 50 cents of ±1200 of the reference), `note_onset_f(est, ref, window)` (precision/recall/F at ±20 ms, one-to-one matching), `note_pitch_error`.

## 4. Golden recordings (tests/golden/vocals/)
3-5 short vocal WAVs supplied by the owner, with a `README.md` (source, licence, length) and one `<name>.notes.json` each: the hand-checked note list (start_s, end_s, midi pitch ±0.5 st). Golden tests (label `golden`) require onset F ≥ 0.9 at ±20 ms, zero octave errors in voiced regions against a hand-checked pitch spot list (octave check only), and every golden file analyses without error. The owner hand-checks the expected lists (listed in the review) and the five SVG plots (`docs/reviews/phase3-plots.md`).

## Effect on audio
None: analysis does not render. No audio-verifier run is needed; the audio checks (`-L audio`) still run in the gate.

## Split / join decisions
- `analyse` is an orchestrator only (no maths): a readable list of steps — split function rule 2.
- JSON/plot code lives in the CLI layer; `core/` never sees JSON (ADR-0007) or SVG.
- The SVG writer is a helper in `cli/plot`, not a library dependency: ~100 lines, one caller.

## Dependencies
`core/analysis/{pitch,onsets,segmentation,perceived_pitch,loudness}`, `core/dsp`, `core/types`; CLI: `core/io`, nlohmann.

## Tests
- Unit: `analyse` on a synthetic three-note melody (notes, times, pitches, loudness, validate, determinism hash); not_implemented for other algorithms; invalid params; the source buffer is bit-identical after analysis.
- CLI process tests (like the render/audition ones): `analyse` writes valid JSON with the documented keys, CSVs and an SVG that contains one `<rect>` per note; exit codes.
- Metric tests: hand-built reference/estimate pairs with known RPA/F values.
- Golden: as in §4 (skipped with a clear message when no WAV is present, so CI stays green until the files are added).

## Open questions
1. JSON key names are fixed here; say if you prefer a different layout.

## Review log
*(structure-reviewer findings go here)*
