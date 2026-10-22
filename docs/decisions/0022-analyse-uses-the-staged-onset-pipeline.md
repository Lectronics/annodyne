# ADR-0022: analyse() finds spectral onsets with the staged pipeline

- **Date:** 2026-10-22
- **Status:** proposed 2026-10-22 (owner: "Lets use analyse() with the new pipeline"); structure review round 1 answered in this revision; the owner's acceptance pending
- **Phase:** 5
- **Amends:** ADR-0010a (the onset strength scale and the energy-step finding, below)

## Context
Phase 3's `detect_onsets` finds spectral onsets with a SuperFlux activation, an adaptive local-mean threshold and an energy refinement, then adds legato energy steps and pitch changes. Phase 5 split the spectral part into public, individually tested stages (`docs/design/phase5-transient-pipeline.md`, `core/analysis/onsets/`) and added what the owner's onset research recommends: level normalisation, a windowed peak-picking rule and a bench of synthetic signals with known onsets. `OnsetParams::method` selects between the Phase 3 chain (`adaptive_threshold`) and the pipeline (`windowed_peaks`); the energy steps, pitch changes, merge and labels are shared.

**Evidence.** All numbers below are reproducible with `build/eval-release/bin/vocadito_report tests/golden/vocals/zenodo_data <set>` (release build; `--tune` = the 35 tuning tracks, `--holdout` = the 5 held-out tracks). Phase 3 rows: `ANNO_TUNE=method=0`; pipeline rows: no override (the analyse() default). The Phase 3 rows equal the figures recorded in ADR-0010a and `docs/design/phase4-pitch-edit.md` (tuning F20 A1 0.582, note pitch 0.853, APC 0.655; held-out F20 A1 0.552, APC 0.552).

| Set | Method | notes | onset F20 A1 / A2 | F50 A1 / A2 | note pitch | APC target | over / under-seg |
|---|---|---|---|---|---|---|---|
| 35 tuning | Phase 3 (`method=0`) | 1671 | 0.582 / 0.587 | 0.770 / 0.796 | 0.853 | 0.655 | 0.092 / 0.025 |
| 35 tuning | pipeline | 1682 | 0.588 / 0.596 | 0.773 / 0.803 | 0.850 | 0.653 | 0.094 / 0.024 |
| 5 held-out | Phase 3 (`method=0`) | 280 | 0.552 / 0.528 | 0.727 / 0.774 | 0.803 | 0.552 | 0.115 / 0.043 |
| 5 held-out | pipeline | 275 | 0.570 / 0.549 | 0.730 / 0.777 | 0.808 | 0.586 | 0.108 / 0.044 |

On the tuning tracks the two are equal within noise (F20 +0.006 / +0.009, note pitch -0.003, APC -0.002). On the 5 held-out tracks (273 notes, one corpus, annotators agree on 75 % of onsets) the pipeline is a little better (F20 +0.018 / +0.021, APC +0.034). That is a small, uncertain gain, not a demonstrated improvement.

On the owner-supplied research's synthetic families (the only data with every onset known; `tests/support/onset_bench.h`; pooled A, A0, B, D, F, G, +-25 ms, parameters chosen on these signals so optimistic), full `detect_onsets` (energy steps on, as in analyse()): Phase 3 F 0.545 (TP 39 FP 55 FN 10), pipeline F 0.601 (43 / 51 / 6). With the energy steps off (spectral part only): Phase 3 0.804, pipeline 0.887. The pipeline finds more of the fast notes (family G: 11 of 16 against 6) and one soft attack fewer (family B: 7 of 8 against 8). **The 23 false positives in the single vibrato/tremolo note (D) and the 8 in the noisy family (F) come from the energy-step detector in both methods, not from the spectral chain** (with the steps off, both methods have none); on real recordings the steps help (turning them off: tuning F20 0.597 -> 0.547 at an earlier parameter setting), so they stay and the synthetic false positives are an open item. Earlier drafts of this ADR and of the design note attributed those false positives to the Phase 3 spectral chain; that was wrong.

Parameter choice (tuning tracks only): a sweep of log gain {1, 10, 100, 1000} x `delta_activation` {0.02, 0.05, 0.1}, then lag, picker windows, level normalisation and `merge_onset_strength` {1, 1.5, 2, 3}. The plateau is F20 0.595-0.60 for gain 1 and delta 0.05-0.1; **0.07 was rejected because it misses all 8 soft attacks of bench family B** (0 of 8; the real-data difference between 0.05 and 0.07 is F20 0.009 on the tuning tracks and 0.016 on the held-out tracks). `merge_onset_strength` is a trade-off, not a no-op (at 3.0: notes 1661 -> 1551, over-segmentation 0.087 -> 0.057, under-segmentation 0.026 -> 0.038, F20 A2 -0.01); it stays 1.5 (ADR-0010a).

## Decision
1. **`analyse()` uses `SpectralMethod::windowed_peaks`.** `AnalyseParams::onsets` defaults to `analysis_onset_params()`: level normalisation (P0, RMS of the active frames to -23 dBFS), `log_gain` 1, lag 15 ms, windowed peak picking with `delta_activation` 0.05, the research's pre/post windows (30/30/100/70 ms), `min_gap` 30 ms, then the existing localisation. Energy steps, pitch changes, the merge and the voicing labels are unchanged.
2. **`OnsetParams{}` stays the Phase 3 chain** (`adaptive_threshold`) and is what `detect_onsets` gets when no parameters are named; `params.onsets = OnsetParams{}` (or `ANNO_TUNE=method=0` in the report tool) selects it for `analyse()`. **That is the rollback**: one line, no code removed.
3. **Strength.** Both methods report the activation over its threshold (>= 1 for a picked peak). The scales are not the same quantity (adaptive local mean + spread against local mean + `delta_activation`), so ADR-0010a's `merge_onset_strength` 1.5 and the energy-step strength constants were tuned for the Phase 3 scale. The sweep above kept 1.5; this ADR amends ADR-0010a only to record that the value was re-checked on the pipeline, not re-derived.
4. **Versioning.** There is no params fingerprint on `AnalysisResult` today (`AnalyseParams::version` is the caller's counter), so a stored analysis made with the Phase 3 chain is **not** detected as stale. This is a known gap, not solved here: callers that persist analyses (Phase 7 `project`) must bump `AnalyseParams::version` when onset parameters change, and Phase 7 should add the parameter fingerprint of ADR-0010. Re-analysing changes note boundaries slightly (1671 -> 1682 notes on the tuning set), so stored edits re-map through `remap_after_reanalysis` (src/core/edits/remap.h; tested in `remap_test` and `remap_contract_test` on synthetic note changes; **not exercised on these tracks**).
5. **Golden guards.** `tests/tolerances.h` is the owner's. The measured held-out values now sit well above its floors (onset F20 floor 0.43 against 0.570/0.549; APC floor 0.55 against 0.586; note pitch floor 0.78 against 0.808), so the guards no longer catch a regression of this size. Raising them is the owner's call and is not done here; the file's comment still quotes F20 0.519 for the held-out set (stale since the c4dm tracker: 0.552 at Phase 3).
6. **Revert criteria.** Revert to the Phase 3 chain if (a) the owner hears worse segmentation on real recordings in the next stage, (b) a second corpus shows held-out onset F20 or APC below the Phase 3 values, or (c) the legato/fast-note work shows the Phase 3 chain is a better base. Each is a parameter change (decision 2).
7. **Known limits.** Legato pitch changes at constant level (bench C: 3 of 8) and fast notes (G: 11 of 16, about 23 ms early from the localiser) remain weak; the energy-step detector false-fires on synthetic tremolo and noise. Both are the next stage. Whitening stays off (implemented, form unchecked against Bock 2012).

## Alternatives considered
- Keep the Phase 3 chain: no change; but the staged code is the base for the open work and is not worse on the data we have.
- A larger real-data win by tuning further: rejected, the plateau is flat and the tuning set is also what the sweep was scored on.
- Retune the Phase 3 chain's constants instead (gain 1, other delta): not run; the windowed picker is the research's rule and testable on its own.
- Remove the energy-step detector: rejected, it helps on real recordings (see Evidence).

## Consequences
- `analyse()` output changes (see decision 4); `anno-cli analyse` and the golden numbers move accordingly.
- Both methods stay in the code and under test (`onsets_test` and the bit-exact gate for Phase 3; `onset_stages_test` and the bench for the pipeline), so the choice is a parameter, not a fork.
- The engine tests (`tests/support/engine_fixture.h`) are pinned to the Phase 3 chain: the pipeline places their synthetic note's onset 0.2 ms later, which moves the note's frame grid by one frame and changes a vibrato-extent measure by about 1 % (recorded as an open item in `docs/design/phase4-pitch-edit.md`).
- The next stage works from this baseline; the owner's listening on real recordings is the final check.
