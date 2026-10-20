# Phase 3 review: what is and isn't a pitch

Branch `phase-3`, commit `1a0882c`. Reviewer: outside review against theory (YIN, pYIN, Tony) and against the reference implementations: c4dm/pyin (the Vamp plugin that Tony uses) and librosa `pyin`.

Scope: the voiced/unvoiced decision and how it feeds note segmentation. Transient-based segmentation is out of scope; it's planned for a later phase.

## 1. Summary

The pitch tracker finds the *right* pitch when there is one. The problem is the *whether*: the voicing decision currently depends on one quantity, how periodic the frame is (YIN CMND depth). It ignores two others that every working tracker uses, how **loud** the frame is and whether the signal is **noisy but still sung**. The result fails in both directions:

| Fails as | Cause | Seen in probe |
|---|---|---|
| **False pitch** | Quiet periodic sounds pass: mains hum, reverb tails, very quiet tones | Hum at −65 dBFS → a note with confidence 1.00; a tone at −80 dBFS → a note with confidence 1.00; a reverb tail at −52 dB still voiced |
| **Missed pitch** | Breathy singing has shallow YIN dips, and a 0.10 threshold prior discards them | Breathy E4 at HNR 15 dB → 0% voiced, no note |

Both have the same root: the observation model is a strict subset of the reference pYIN's. Three ingredients are missing: the low-amplitude penalty, `yinTrust`, and paired voiced/unvoiced states. The threshold prior is also stricter than the reference default. The constants were tuned on clean synthetic tones only (`docs/reviews/phase3-review.md`), which is exactly where none of this shows up.

**Recommendation:** don't close Phase 3 yet. Fixes P1–P4 below are small, local to `analysis/pitch` and `segmentation`, and well understood. Do them first, and turn the probes in §4 into regression tests.

## 2. What the code does vs. the references

### 2.1 Observation model (`pitch/detail/pyin_observe.cpp`)

| | annodyne | c4dm/pyin (Vamp, used by Tony) | librosa `pyin` |
|---|---|---|---|
| Threshold prior | Beta, **mean 0.10** | Beta, **mean 0.15** (`threshDistr` index 2, the default) | Beta(2, 18) → mean 0.10, **plus** Boltzmann prior over troughs |
| No-trough fallback | global min gets 0.01 × weight | global min gets 0.01 × weight | `no_trough_prob` 0.01 |
| Low-amplitude penalty | **none** | if RMS < `lowAmp` (0.1): candidate mass × (rms + 0.01·lowAmp)/(1.01·lowAmp) | none, but the default `fill_na` plus the dB floor in `frame` handles silence |
| yinTrust | **none** | 0.5: voiced obs = 0.5 × mass | n/a (voicing prob ≈ mass directly) |

Note on the beta prior: librosa's mean 0.10 is paired with the Boltzmann term, which favours the *first* trough, and with `resolution`/`max_transition_rate` choices that are looser than annodyne's. Copying only librosa's beta and dropping the rest is the strictest of the three combinations.

### 2.2 Pitch HMM (`pitch/detail/pitch_hmm.cpp`)

| | annodyne | c4dm/pyin `MonoPitchHMM` |
|---|---|---|
| Unvoiced states | **one** shared unvoiced state | one unvoiced copy **per pitch bin** (2 × nPitch states) |
| Voicing switch | voiced ↔ unvoiced 0.01 | 0.01 (selfTrans 0.99) |
| Pitch transitions | triangular, `max_jump_cents` 80, plus a jump at 1e-4 | triangular, width 11 bins at 5 bins/semitone (≈ ±220 ¢) |
| Range | 50–1600 Hz | 61.7 Hz upward, 69 semitones (≈ 61–3300 Hz) |

The single unvoiced state discards pitch memory. When a breathy note loses voicing for 20 ms in the middle, the reference stays on "unvoiced at A3" and comes back to A3 cheaply. annodyne returns via an unvoiced → voiced transition that is equally cheap to every bin, which also makes *any* periodic blip after a gap an equally good place to resume. Annodyne's 80 ¢ smooth reach also forces a scoop of more than 80 ¢ per hop through the 1e-4 jump. It survives in the probe, but it's the wrong cost for singing.

### 2.3 Voicing output (`pyin.cpp fill_track`, `types/tracks.h`)

`voicing_prob` is pushed to whichever side of 0.5 the Viterbi path chose, then `is_voiced` thresholds at 0.5. Downstream code (note HMM silence emission, note confidence) receives what is effectively a binary signal labelled as a probability. That causes the cliff behaviour in §3 (100% → 0% between HNR 20 and 15 dB).

The reference keeps the forward-backward or emission mass as a soft value. The Tony note HMM blends it with a prior (`priorPitchedProb` 0.7, `priorWeight` 0.5) before using it.

### 2.4 Note HMM (`segmentation/detail/note_hmm.cpp`)

| | annodyne | Tony `MonoNoteParameters` |
|---|---|---|
| Attack pitch σ | **45 ¢** (same as stable) | **5 semitones** (`sigmaYinPitchAttack`) |
| Stable pitch σ | 45 ¢ | 0.8 semitone, plus note-centre σ² 0.7 |
| Attack self | 0.3 | 0.9 |
| Stable self | 0.995 | 0.99 |
| Silence self | 0.99 | 0.9999 |
| Pitch evidence weight | full | `yinTrust` 0.1 (mass^0.1) |
| Voicing evidence | 1 − voicing (binary, see 2.3) | pIsPitched blended with prior 0.7 |
| Level gate | none | a frame counts as voiced only if level[i]/level[i+2] > 0.7 (onset sensitivity) |
| Min note | 60 ms, absorbed afterwards by heuristics | 100 ms (`pruneThresh`) |

The 45 ¢ attack σ is the big one. In Tony the attack state exists to soak up scoops, transients and consonant pitch junk, which is why its σ is very wide. With σ equal to stable, annodyne's attack can't do that job, and `segmentation.cpp` compensates after the fact: 150 ms ±120 ¢ smoothing, absorbing runs shorter than 60 ms, onset snapping. That works on the probes, but it's a second, hand-tuned segmenter layered over the HMM.

### 2.5 Note confidence (`segmentation.cpp`)

Confidence = mean voicing × share of frames within 60 ¢. It doesn't use level. A −80 dBFS tone and a −14 dBFS tone both score 1.00. A downstream UI that dims low-confidence notes, or a "strip silence" that trusts confidence, will keep hum and tails.

### 2.6 Things that are right

- CMND and NCCF via FFT autocorrelation; silent frames return CMND 1. Correct.
- Parabolic refinement on NCCF, octave candidates at 1e-3 of the top mass. Matches the reference intent.
- SuperFlux onsets with energy refinement; consonant tagging (spectral onset followed by unvoiced 20 ms later). Sensible.
- Perceived pitch (trim 40/60 ms, rate-weighted, whole vibrato cycles, 2.5 Hz drift LPF). Consistent with the stability-weighting literature, and the scoop probe gives A3 −4 ¢.
- Inhale, sibilant and chord bleed are all correctly unvoiced. Bleed fails CMND because no single period exists, which is the right reason.

## 3. Evidence

All signals are synthetic, deterministic (seed 1234), 44.1 kHz. They were analysed with `anno-cli analyse --pitch-track --json` (clang release build) and with a small driver linking `PYinEstimator` directly to vary `beta_mean`.

### 3.1 Probe scenes

| Probe | Region | Expected | Got |
|---|---|---|---|
| p1 reverb tail | A3 note | voiced note | ✔ |
| | tail −12…−32 dB | either | voiced |
| | tail below −52 dB | unvoiced | **32% voiced, and a separate 1.3 s note** |
| p2 hum | E4, D4 | notes | ✔ |
| | 50 Hz hum at −55 dBFS between notes | unvoiced | **voiced; notes "G1+35c" conf 0.98; 5 notes total instead of 2** |
| p3 breath | inhale −30 dB | unvoiced | ✔ |
| | C4 | note | ✔ |
| | sibilant | unvoiced | ✔ |
| | voiced "z" 150 ms | voiced, not a note | 0% voiced (acceptable for now) |
| | breathy E4 (HNR ≈ 3 dB) | voiced note | **0% voiced, no note** (voicing median 0.40, max 0.50) |
| p4 bleed | A minor chord at −48 dBFS | unvoiced | ✔ |
| p5 contours | A3 after 250 ¢ scoop | note at A3 | ✔ perceived A3 −4 ¢ |
| | fall-off | voiced | 48% |
| | 70 ms grace note | either | became its own 60 ms note |

### 3.2 Sweeps

**Breath noise:** E4 at −30 dBFS RMS, band noise 200–8000 Hz. Voiced share over the note body:

| HNR (dB) | 30 | 20 | 15 | 12 | 10 | 8 | 6 | 3 |
|---|---|---|---|---|---|---|---|---|
| annodyne default (β mean 0.10) | 100% | 100% | **0%** | 0% | 0% | 0% | 0% | 0% |

Varying only the threshold prior:

| HNR | β 0.10 | β 0.15 | β 0.20 | β 0.30 |
|---|---|---|---|---|
| 15 dB | 0% | 100% | 100% | 100% |
| 10 dB | 0% | 0% | 0% | 100% |
| 6 dB | 0% | 0% | 0% | 0% |

The same signals low-passed at 1.5 kHz before YIN:

| HNR | β 0.10 | β 0.15 |
|---|---|---|
| 15 dB | 100% | 100% |
| 10 dB | 100% | 100% |
| 6 dB | 0% | 100% |
| 3 dB | 0% | 0% |

Breath noise is broadband and most of a voice's periodic energy sits below 1.5 kHz, so a low-pass front-end buys about 5 dB of HNR. Combined with the reference β 0.15, it buys about 9 dB. This is the low-pass prefiltering that the YIN paper itself recommends (de Cheveigné & Kawahara 2002). It doesn't need a looser threshold that would also admit more junk.

**Level:** an A3 tone alone, no noise:

| Tone RMS | −20 | −40 | −60 | −80 dBFS |
|---|---|---|---|---|
| voiced / confidence | 100% / 1.00 | 100% / 1.00 | 100% / 1.00 | 100% / 1.00 |

**Hum:** 50 Hz + 4 harmonics over pink noise at −75 dBFS:

| Hum RMS | −45 | −55 | −65 | −75 | −85 dBFS |
|---|---|---|---|---|---|
| voiced | 100% | 100% | 100% | 0% | 0% |
| notes | 1 (1.00) | 1 (1.00) | 1 (1.00) | 0 | 0 |

The threshold prior has no effect on hum: it's 100% voiced at −65 dBFS for every β. Hum is perfectly periodic, so no periodicity threshold will ever reject it. Only level, context and range will.

## 4. Recommendations

In priority order. P1–P4 should land before Phase 3 closes. P5–P7 can go into the Phase 3 follow-up or into the transient phase.

### P1: level-aware voicing (fixes hum, tails, quiet junk)

1. **Estimate a noise floor per take.** Use a low percentile (e.g. 10th) of short-time RMS in dB over frames that are either unvoiced or have CMND min > 0.5. Store it on the analysis result. `AnalysisResult` is a core contract, so adding a field needs an ADR.
2. **Penalise candidate mass by relative level, not absolute level.** The reference uses absolute RMS < 0.1, which is wrong for a DAW tool where takes arrive at −30 dBFS. Use frame level relative to both the floor and the take's loud reference (95th percentile of voiced frame level):
   - `margin = level_db - noise_floor_db`. Full weight at ≥ 20 dB above the floor; ramp down to 0.01 at ≤ 6 dB.
   - Also `drop = ref_db - level_db`. Full weight within 40 dB of the reference; ramp to 0.01 at 60 dB down.
   - Multiply each candidate's mass by `min(w_margin, w_drop)` before the HMM, exactly where pYIN applies `lowAmp`. Put all the numbers in `PYinParams` with these defaults.
3. **Two passes are needed.** The floor and reference need a first pass. Run YIN observation (cheap) → compute stats → apply weights → Viterbi. No second YIN is required.
4. **Put level into note confidence:** `confidence *= w_level(note median level)`. A note entirely within 10 dB of the floor should never read as confident.

### P2: bring the pitch HMM up to the reference

1. **Add `yin_trust` (default 0.5)** to `PYinParams`. Voiced emission = `yin_trust × mass`; unvoiced = `(1 − yin_trust × Σmass) / n_bins` spread per bin.
2. **Use one unvoiced state per pitch bin** (2 × nBins states), with the same smooth pitch transition applied within the unvoiced copies and a 0.01 switch to the same bin's voiced copy. Viterbi cost doubles. At 10 ¢ bins over 50–1600 Hz that's ~1200 states. Fine offline; if it matters, use the reference's 20 ¢ bins with parabolic refinement after.
3. **Widen the smooth pitch reach** to about ±2 semitones per 5 ms hop (librosa's `max_transition_rate` 35.92 oct/s ≈ 2.2 semitones per 5 ms). Then scoops and fast runs don't go through the jump term. Keep the jump for octave errors.

### P3: breathy-voice recovery

1. **Low-pass prefilter for YIN**, applied in the resampled analysis signal only, never in the audio path. Cut-off ≈ max(1.5 kHz, 4 × the second pass's upper range). Use NCCF on the *unfiltered* signal for the parabolic lag refinement, so precision doesn't suffer.
2. **Raise `beta_mean` to 0.15** (the reference default) once P1 is in. Without P1, the looser threshold admits more quiet junk; with P1, level handles that.
3. Re-run the HNR sweep. Target: 100% at HNR ≥ 10 dB, ≥ 80% at 6 dB, and no regression on inhale/sibilant/bleed.

### P4: soft voicing all the way down

1. Stop forcing `voicing_prob` to the side of 0.5 in `fill_track`. Publish the posterior from forward-backward (or the normalised Viterbi-path emission ratio if forward-backward is too costly) as `voicing_prob`, and keep `is_voiced` as the path decision in a separate bit or field.
2. The note HMM uses the soft value, blended with a prior as Tony does: `p = prior_weight × 0.7 + (1 − prior_weight) × voicing_prob`, with prior_weight 0.5.

### P5: note HMM closer to Tony

1. **Attack σ about 5 semitones** (`note_hmm.cpp`), stable σ ≈ 80 ¢. Raise attack self to ~0.9 and lower silence self to 0.9999.
2. Weight pitch evidence by `mass^0.1` (Tony's `yinTrust` for notes).
3. **Raise the minimum note to ~100 ms**, and tag 40–100 ms runs as `grace_candidate` instead of a note or nothing. This ties into `PitchTargetKind::ornament` in `note-pitch-target.md`.
4. **Add a level-ratio gate at note starts:** ignore a candidate start if level hasn't risen (Tony: level[i]/level[i+2] > 0.7). This kills tail-restarts.
5. Once these are in, **delete the after-the-fact heuristics they replace**: the 150 ms smoothing pass, run absorption. Keep onset snapping. Don't keep two segmenters.

### P6: non-vocal pitched sources (hum, bleed, tails)

Run these as an annotation pass after the note HMM. Each rejects a note, or marks it `kind = unpitched` with a reason, rather than altering the pitch track:

- **Mains hum:** median pitch within ±30 ¢ of 50/60 Hz or their first 3 harmonics, pitch std < 5 ¢ over > 500 ms, and level < floor + 25 dB → reject. Optional: notch out the measured hum fundamental before YIN.
- **Reverb or decay tail:** note starts with no onset, immediately follows a note at the same pitch (±50 ¢), and has monotonically falling level with a decay slope steeper than −20 dB/s → merge into the previous note's release, or mark as tail.
- **Singer range:** after pass 1, set range = [5th percentile − 4 semitones, 95th percentile + 4 semitones] of confident voiced pitch, and re-run Viterbi (not YIN) with bins outside the range down-weighted (not removed). This catches hum (50 Hz is below nearly every singer's range), fan noise and pitched bleed.

### P7: tests and validation

1. **Turn the probes into Catch2 tests** under `tests/unit/analysis/voicing/` (and the scene-level ones under `tests/audio/`), generated in C++ with the same signals as `make_probes.py`. Every row of §3.1 and the sweeps becomes an assertion with a target from P1–P5. They must fail today. Mark the expected-failure rows clearly until the fix lands.
2. **Get real vocals into `tests/golden/vocals/`.** Vocadito (40 solo vocal tracks with f0 and note annotations, CC BY 4.0) is the closest match to this use case. Pick three clips: one clean, one breathy, one with room noise and bleed. Score with mir_eval-style voicing recall and false alarm, and raw pitch accuracy at 50 ¢.
3. **Keep a neural cross-check on the roadmap** (RMVPE or PESTO, Phase 13). It's useful as a second opinion on voicing, especially for breathy and noisy material where any autocorrelation method runs out of road around HNR 5 dB.

## 5. Other notes

- **The GCC 13 release build fails** with `-Werror=free-nonheap-object` in `src/core/edits/remap.cpp`. This is a known GCC 13 false positive with inlined `std::vector` moves. Clang builds cleanly. Either suppress that one warning for GCC 13 in the CMake warnings module, with a comment explaining it's a known GCC 13 false positive, or restructure the function. Don't drop `-Werror`.
- **`docs/design/note-pitch-target.md` uses `annodyne::analysis`**, but the codebase namespace is `anno::`. Fix it before handing the note to Claude Code.
- **CLI:** `--pitch-track` output was essential for this review. Keep it, and add `noise_floor_db` and per-frame `level_db` when P1 lands.

## Sources

- de Cheveigné & Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 2002.
- Mauch & Dixon, "pYIN: A fundamental frequency estimator using probabilistic threshold distributions", ICASSP 2014.
- Mauch et al., "Computer-aided melody note transcription using the Tony software: accuracy and efficiency", TENOR 2015.
- c4dm/pyin source: `MonoPitchHMM.cpp`, `PYinVamp.cpp`, `MonoNoteParameters.cpp`, `MonoNoteHMM.cpp` — https://github.com/c4dm/pyin
- librosa `librosa/core/pitch.py` (`pyin`) — https://github.com/librosa/librosa
- Bittner et al., "vocadito: A dataset of solo vocals with f0, note, and lyric annotations", 2021.
