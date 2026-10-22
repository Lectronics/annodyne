# Phase 4 review: First audible edit, pitch

- **Date closed:** 2026-10-22
- **Git tag:** `phase-4` (not set: tagging waits for the owner, as for earlier phases)
- **Listening gate #1:** held; see [`phase4-listening.md`](phase4-listening.md). The owner reported in words, not ratings; PSOLA chosen (ADR-0021).

## Built
Design note [`phase4-pitch-edit`](../design/phase4-pitch-edit.md) (three structure-review rounds, Stage 9 reviews, Stage 9b-9d results). ADRs: 0016 (pitch edit), 0017 (frame classes and routing), 0018 (research adoption), 0019 (SPTK marks, proposed, Phase 6), 0021 (PSOLA is the pitch engine).

- **`core/analysis/partials`** f0-guided harmonic tracking (sieve, Blackman-Harris, QIFFT, amplitude/phase by projection); **`harmonic_noise`** residual and sound classes (voiced, breathy, rough, plosive, sibilant, breath, silence) with the research's metrics; **`envelope`** True Envelope on harmonic peaks. `analyse()` fills them per `Note`.
- **`core/synthesis/harmonic_noise`** `HarmonicNoiseEngine` (x - H_original + H_shifted, envelope re-read, class routing, equal-power fades). **`core/synthesis/psola`** `PsolaEngine` (TD-PSOLA; analysis marks placed by waveform match with fractional samples; period-synchronous separation so only the periodic part is re-spaced; DC removal; margin rule). Shared helpers in `synthesis/engine/engine_support`.
- **`core/tuning`** `target_pitch` (`ChromaticGrid`); **`tools/main_tool`** `pitch_drag_edit`.
- **CLI:** `--engine passthrough|harmonic_noise|psola`; analysed scenarios with `pitch_drag` edits; the audition report gains pitch round trip, vibrato and first-formant rows (`cli/checks/pitch_checks`); `scripts/phase4_audition.py`, `scripts/phase4_cycle_jump.py`.
- **Edits:** `k_max_pitch_offset` 2400 cents; `effective_state` clamp.
- **CI/tooling:** clang-tidy fixes in Phase 3/4 files; `anno_add_test(... TIMEOUT)`; golden test 1500 s (508 s under TSan).

## Gates
| Gate | Result | Evidence |
|---|---|---|
| Structure review of design notes | Phase 4 note: 3 rounds, blocking items fixed before coding; Stage 9: CHANGES REQUIRED (B1-B3, S1-S9), blocking fixed, S1 S3 S5 S8 S9 open (below) | design note review logs |
| Build, all tests, ASan/UBSan, TSan, release | **696/696** debug-asan (after the Phase 5 stages; 660/660 at the last Phase 4 commit in debug-asan, release and TSan) | local runs 2026-10-22 |
| clang-tidy, clang-format, include layers | clean on the changed files; full tidy over the CI file set fixed 7 older findings; `check_includes` 164 files, 0 violations | local runs |
| CI (Linux, macOS, Windows, tidy, TSan) | to run on push; only macOS reproduced locally | GitHub Actions |
| Objective audio checks | `ctest -L audio` 14/14. Audition package `phase4-pitch`: 13 scenarios x 2 engines = 24 pairs; **all 144 structural rows pass** (null, untouched, click, sanity, determinism, length). 11 pairs have FAIL rows, all pitch round trip, vibrato or first formant (below) | audio-verifier report 2026-10-22 |
| Human listening | held in words; no per-scenario ratings | `phase4-listening.md` |
| Structure audit (Phases 1, 5, 9, 12 only) | not due | |

## Deviations from the specs
ADR-0016 (envelope re-read instead of spec 5.3, 2400-cent limit, engine scope, margin rule), ADR-0017 (`FrameClass` gains breathy and rough), ADR-0018 (research allocated to phases), ADR-0021 (engine choice). Spec text in `docs/AUDIO_VERIFICATION.md` section 2 uses `center_offset_cents`, the parser's name is `center_offset`: the owner's to edit (the spec is read-only for Claude).

## Objective numbers
- **Pitch round trip** (limit 3 cents): PSOLA worst 1.16 cents (`vocadito2_note2_up200`), synthetic vowels within 0.1 cents. harmonic_noise worst 7.92 cents on the real voice at +700 (+692.08 measured), vowels within 0.4 cents. -2400 is not measurable (55 Hz is below the tracker).
- **Vibrato:** PSOLA synthetic vowel +700: 5.52 -> 5.93 Hz (pinned bound +-0.5, widened from 0.4 with the owner's approval); other shifts 5.52-5.56. Real voice `vocadito2_note2`: PSOLA rate FAIL rows (10.00 -> 7.1-7.7 Hz of a jittery pitch_mod), within about 1 Hz on real tracks.
- **First formant (single parabolic peak):** PSOLA synthetic vowel -5.1 % (down200), +8.7 % (down700), +8.4 % (up700), -15.8 % (sibilant vowel down200); real voice +12.7 % and "not measurable". The measure is not validated (sparse harmonics): Phase 6.
- **Hiccups** (cycle-to-cycle jump, source's maximum vs render's): 1.14 vs 0.03 before the mark change, at or below the source on 11 of 12 scenarios after (owner: "pretty good").
- **Noise bed above 8 kHz** (render vs source, 1 cycle): -0.4 to -0.8 dB on four of five scenarios, -3.1 dB on one (probably harmonics); plain PSOLA -2.0 to -5.1 dB.

## Open issues carried forward
| Issue | Phase |
|---|---|
| First formant moves with the shift (and the measure itself is unvalidated) | 6 (`formant` tool) |
| Vibrato rate/extent off slightly on a few edits; the measure jitters on ~6 vibrato cycles (a longer synthetic note would separate them) | 6 |
| PSOLA limited to 700 cents; harmonic_noise beyond, with audible distortion | 6 (ADR-0019 marks, formant work) |
| Voice level changes with the shift (about -3 dB at +700 cents, hypothesis: grain overlap) | 6 |
| Continuous pitch/formant curves and glides at note edges | 6 (`pitch-glide-transitions.md`, ADR-0020 provisional) |
| ADR-0019 SPTK/REAPER marks: A/B trial, needs rough/breathy labels | 6 |
| Stage 9 review items S1 (ADR for the pitch/formant measures), S3 (a note-contained sibilant scenario), S5 (DC removal test that content below f0/2 survives), S8 (a report-only row status: real-voice FAIL rows make the CLI exit non-zero), S9 (more `pitch_checks` unit tests), undo-null and note-edge click checks for the pitch engines | 5-6 |
| `expect` in scenarios is echoed, not checked against the edits | 5 |
| Pitch-correction research file and licence decision (ADR-0014), plot checks, accepting the -10/+15 tolerance (Phase 6 gate) | owner |
| AUDIO_VERIFICATION section 2 example name `center_offset_cents` | owner |

## Split/join and cleanup done this phase
`FrameClassSet` moved to `core/types`; the engines share `engine_support` (`only_pitch_edited`, `zone_shift`, `hex64`, `run_weights`) and one test fixture (`tests/support/engine_fixture.h`); `render_note` of PSOLA split into `respace_zone`/`synthesise_run`/`average_cycles`/`cycle_energies`; `normalise_level`, the audition report rows got an enum kind. No structure audit is due until Phase 5 closes.
