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

/// Runs, in order: read_mono + k_weight (once), detect_pitch, detect_onsets (with the pitch track),
/// segment_notes, then per NoteSpan: note_grid, measure_loudness, decompose_pitch_curve,
/// perceived_pitch, and builds the Note (harmonics, noise and envelope stay empty until Phase 4; a Note
/// with empty nested tracks is valid). `source_id = src.id()`; tuning reference stays 440 Hz
/// (estimated in Phase 8). Every per-note stage is guaranteed to succeed for a span `segment_notes`
/// returned (min 12 frames, voiced); if one fails anyway the whole analysis fails with the note id in
/// the message (a bug, never silently dropped), and a contract test asserts every output note has
/// grid.n_frames > 0, curves of that length, a finite loudness and a perceived pitch. Returns a shared immutable snapshot that passes validate().
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
Structure: `cli/commands/analyse` (argument handling, orchestration), `cli/analysis_json` (JSON writer, links nlohmann; unit-testable without a process, like `cli/scenario_json`) and `cli/plot` (CSV + SVG, no third-party code) are separate targets; `cli/commands` does not link nlohmann.
- `--json`: notes (id, start_s, attack_end_s, release_start_s, end_s, perceived_pitch_cents, pitch_center_cents, loudness_lufs, confidence, separation_in), the onset list, and the pitch track as `{hop_s, start_s, f0_cents[], voicing[], confidence[]}` (NaN as null). Keys are stable and documented in the command's help; the format is for humans and tests, not a persistence format (Phase 7).
- `--plot-dir`: `pitch.csv` (time_s, f0_cents, voicing, confidence), `notes.csv`, and `plot.svg` (dependency-free: pitch curve as a polyline, notes as rectangles at their perceived pitch, onsets as ticks, a MIDI-name axis). Written by a small `cli/plot` helper with no third-party library.
- Exit codes as the other commands (0 ok, 1 error, 2 usage). Errors: not_found / corrupt_data from the WAV reader, analysis errors.

## 3. Metrics (tests/support/analysis_metrics.h, mir_eval definitions)
`raw_pitch_accuracy(est, ref, tolerance)` and `raw_chroma_accuracy` at 50 and 10 cents (voiced reference frames only), `voicing_recall` / `voicing_false_alarm`, `octave_errors` (count of voiced frames where the estimate is within 50 cents of ±1200 of the reference), `note_onset_f(est, ref, window)` (precision/recall/F at ±20 ms, one-to-one matching), `note_pitch_error`.

## 4. Golden recordings (tests/golden/vocals/)
3-5 short vocal WAVs supplied by the owner, with a `README.md` (source, licence, length) and, per recording `<name>.wav`, two hand-checked files: `<name>.notes.json` = `{"notes": [{"start_s": 1.20, "end_s": 1.85, "midi": 64}, ...]}` (pitch within ±0.5 semitone) and `<name>.pitch.json` = `{"spots": [{"time_s": 1.5, "midi": 64.0}, ...]}` (at least 10 voiced spot checks per file, used only for the octave check: the estimate within 50 cents of the spot, never ±1200 off). Golden tests (label `golden`) require onset F ≥ 0.9 at ±20 ms, zero octave errors at the spots, and every golden file analyses without error. **The gate is not vacuous:** when `tests/golden/vocals/` holds fewer than 3 recordings the golden tests `SKIP` (CI stays green while the files are missing), and the Phase 3 close checklist plus a `golden_inventory` test run with `ANNO_REQUIRE_GOLDEN=3` fail unless ≥ 3 recordings with both files exist. The owner hand-checks the expected lists (listed in the review) and the five SVG plots (`docs/reviews/phase3-plots.md`).

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
