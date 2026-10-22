# Listening sheet: phase 4 — pitch edit (listening gate #1)

- **Listener:** the project owner
- **Date:** 2026-10-21 (rounds on the synthetic package, the real-voice packages and the separation package)
- **Playback:** not recorded
- **Audition packages:** `audition/phase4-pitch/INDEX.md` (13 synthetic scenarios x 2 engines), `audition/phase4-real/` (24 real-voice pairs, PSOLA, first marks), `audition/phase4-real-hn/` (the same through harmonic_noise), `audition/phase4-real-v2/`, `audition/phase4-separation/` (PSOLA with 0, 1, 2 and 4 cycles of period-synchronous separation). All are git-ignored; regenerate with `scripts/phase4_audition.py`.

**How this sheet was filled.** The owner listened across the session and reported in words; no 1-5 rating per scenario was given, and not every scenario was heard ("I didn't listen to all of the auditions"). This sheet therefore records the owner's statements, quoted or closely paraphrased, and leaves the ratings table empty rather than inventing numbers. Nothing here is a claim by Claude about how the audio sounds.

## What the owner reported (in order)

| Round | Material | Statement |
|---|---|---|
| 1 | `phase4-pitch`, synthetic vowels, both engines | "every single one I listened to, the psola was much better than harmonic noise. The harmonic noise added a lot of distortion to the wave that psola did not." Keeping the vibrato rate is better than not; formant drift "is a definite issue", to be dealt with in the formant work of Phase 6. Wants to hear real audio. |
| 2 | `phase4-real` (PSOLA, real voices, first marks) | "all of the samples exhibit this type of artifact": what the vibrato row measured on real voices was not vibrato but "hiccups" in the PSOLA shifting. "otherwise, the psola sounds alright." |
| 2 | `phase4-real-hn` (harmonic_noise, real voices) | "definitely more consistent, however, it still adds a lot of distortion, so its very audible." |
| 3 | `phase4-real-v2` (PSOLA with waveform-matched marks) | "The tests sound pretty good." Heard "the noise in the background drop during the pitch shifting on the files with a lot of noise." |
| 4 | `phase4-separation` (0 / 1 / 2 / 4 cycles) | "either zero or one is probably best. I heard the least amount of noise difference overall in separation 1." Then: "I want to keep 1 cycle. This is decent." |

The last round was heard before the noise-power gain was made adaptive (`edited_FIXEDGAIN_old_*` in the package); the adaptive version is the committed default and the owner kept 1 cycle on that basis. A listening confirmation of the adaptive gain specifically is not recorded.

## Ratings (AUDIO_VERIFICATION section 4)
Not given. Rows left empty on purpose.

| Scenario | Edit size | Rating | Problems (type @ timestamp) | Notes |
|---|---|---|---|---|
| (not rated) | | | | |

## Engine or setting comparisons
- **PSOLA over harmonic_noise** for the pitch shifts heard (up to the 700-cent PSOLA limit); harmonic_noise audibly distorts on synthetic and real voices alike. -> ADR-0021.
- **Separation 1 cycle** over 0, 2 and 4 for the noise bed. -> default `separation_cycles` 1.

## Problems named by the owner, and where they went
| Problem | Type | Outcome |
|---|---|---|
| Hiccups in PSOLA on real voices | warble / flutter | fixed in the engine: analysis marks follow the waveform (Stage 9b of `docs/design/phase4-pitch-edit.md`); cycle-to-cycle jump back at the source's level on 11 of 12 scenarios; owner: "pretty good" |
| Background noise drops on noisy files | level jump | period-synchronous separation (Stage 9c/9d); noise above 8 kHz within 0.3-0.9 dB on four of five sweep scenarios |
| First formant moves with the shift | chipmunk / giant | **open, Phase 6** (`formant` tool); owner's decision |
| Vibrato rate and extent change on a few edits | unnatural vibrato | open: rate FAIL rows remain within about 1 Hz; pinned bound +-0.5 Hz |
| harmonic_noise distortion | metallic / robotic | engine not the default (ADR-0021); not pursued |

## Pass?
Not decided on this sheet. The criteria of AUDIO_VERIFICATION section 4 need ratings the owner has not given. The owner's decision to move on to Phase 5 and to keep PSOLA with 1 cycle is recorded as "accepted for now" in `docs/reviews/phase4-review.md`.
- [ ] No scenario below 3 (not rated)
- [ ] No clicks or pops (objective click detector passes on every package pair; not reported by the owner either way)
- [ ] No obvious problem on moderate edits (the owner named formant drift, which is deferred)

## New scenarios to add
- A note with more vibrato cycles (longer than 1 s) to separate the vibrato-rate measure's jitter from a real rate change.
- Rough and breathy notes and 440-880 Hz material (also needed by ADR-0019).
- A noisy-background recording as a standing scenario for the noise bed.
