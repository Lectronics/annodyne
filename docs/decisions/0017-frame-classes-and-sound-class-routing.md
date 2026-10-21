# ADR-0017: Frame classes breathy and rough, and sound-class routing for pitch edits

- **Date:** 2026-10-20
- **Status:** accepted 2026-10-20 (owner: "your instincts are good here"; thresholds stay provisional, ADR-0018)
- **Phase:** 4

## Context
The owner's research (`docs/design/vocal-resynthesis-research.md`) shows that plosive bursts, sibilants and breaths, breathy phonation and rough phonation (fry, growl) each fail differently under pitch shifting, so each frame is classified and routed. `FrameClass` has five members (`voiced, sibilant, breath, plosive, silence`) and cannot tell breathy or rough voice from a modal vowel. Phase 4's engines must not shift what they cannot shift well.

## Decision
1. **`FrameClass` gains `breathy` and `rough`** (appended, so existing values keep their numbers): `voiced, sibilant, breath, plosive, silence, breathy, rough`. `voiced` means modal phonation.
2. **Noise gain mapping.** `noise_gain(NoiseGains, FrameClass)` returns `gains.voiced` for `breathy` and `rough` (they are voiced phonation; the Phase 6 sibilant/breath tools do not own them). No new `EditParam` now. `render_key` hashes the enum value; JSON names are the enumerator spelling; `validate(NoiseTrack)` accepts both.
3. **Classifier** (`split_harmonic_noise`, Phase 4 stage 2) runs the research's order on each frame and takes the first match, default `voiced`:
   1. plosive burst: positive spectral flux peak (moving median + 3 MAD, minimum gap 30 ms) after a closure energy dip and with a high-frequency rise;
   2. sibilant: unvoiced and spectral centroid above `sibilant_centroid` (default 4 kHz);
   3. breath: unvoiced, otherwise;
   4. rough: two-tracker octave ratio flag (F0 / alternate F0 in 1.74-2.3 or inverse) or SHR above the singer's 90th percentile;
   5. breathy: maximum voiced frequency below 2 kHz or HNR below 10 dB;
   6. voiced.
   Labels are median-filtered over 3-5 frames; minimum segment 20 ms except bursts. Phase 4 builds tests 1-3 and 5 (centroid, ZCR, flux, HNR, MVF from band aperiodicity) and test 4 through the octave ratio from `PitchTrack::alternates`; SHR, CPPS, GNE, H1-H2, jitter and shimmer are Phase 6 (ADR-0018). Until a metric exists its test never matches.
4. **Routing in the pitch-shift engines** (`HarmonicNoiseParams::shifted_classes`, default `{voiced, breathy}`):
   - `plosive`, `sibilant`, `breath`, `silence`, `rough`: original samples, no shift, no formant move. Crossfade 4 ms (default) at the class boundary, aligned to a pitch period when the next segment is voiced.
   - `breathy`: harmonics below the frame's maximum voiced frequency are shifted; noise above it stays pinned to the original envelope (the engine's residual is never shifted).
   - `rough` is left alone until a method for it is chosen (texture-preserving small shifts, or a noise-conditioned neural vocoder in Phase 13). The `creak` flag from the pitch target already stops Correct Pitch from auto-correcting such notes.
5. **Per-frame metric log.** The classifier stores nothing new in `Note` beyond `frame_class`; the metrics used for each decision are returned by the function for tests and for the calibration harness (no cache key impact).
6. **Calibration checks** (research's last table) become objective checks in `tests/audio` at their phases: plosive onset within 2 ms, level within 3 dB, pre-burst energy within 10 dB; sibilant centroid within 5 % and 4-16 kHz flatness within 10 %; breathy HNR within 3 dB, MVF within 500 Hz, CPPS within 1 dB (Phase 6, once CPPS exists); rough octave-error rate under 2 % and jitter/shimmer ratio 0.7-1.3 (Phase 6); modal F0 error under 10 cents. They enter `tests/tolerances.h` as provisional and are never loosened without the owner (AUDIO_VERIFICATION §1).

## Alternatives considered
- Keep five classes and treat breathy/rough as `voiced`: no type change, but pitch shifting would apply a harmonic comb to subharmonic voices and drag breath noise with the pitch.
- Separate flags on `NoiseTrack` instead of enum members: two ways to say the same thing; the enum is what the engines and gain tools already switch on.
- A `voiced_consonant` class: the research handles voiced fricatives with the same harmonic-below-MVF / noise-above rule as breathy phonation, so no class is needed.

## Consequences
Touches `core/types/tracks.h`, `edit.cpp` (`noise_gain`), `render_key.cpp`, JSON, and five tests; analysis fingerprint changes only for notes that gain the new tracks (Phase 4 already re-baselines). Quality of the breathy path is a listening-gate #1 item.

## Spec updates
Architecture spec §3 (`NoiseTrack.frame_class` literal list) is overridden: two more members.

## Amendment 2026-10-21 (Phase 4, engine)
The harmonic-noise engine shifts **all** measured harmonics of a `breathy` frame in Phase 4. The
"harmonics below the maximum voiced frequency only" clause of item 4 needs the MVF per frame, which
the `Note` does not store; it is deferred until the listening gate shows whether it matters (the
noise above the harmonics is the unmoved residual either way). Engine params use a `FrameClassSet`
(canonical set, empty set = shift nothing). The 4 ms fade is one `class_edge_fade` for every class
boundary inside a note; the separate `consonant_fade` of the design note is dropped (a consonant is
an unshifted class, so its boundary is a class boundary).
