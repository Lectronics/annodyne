# ADR-0031: formant-shift envelope stability, breathy frames and note joins: adoption candidates under rule 9

- **Date:** 2026-10-23
- **Status:** proposed (research outcome; nothing here is implemented or measured by the author, and no listening has been done: rule 7)
- **Phase:** 6
- **Related:** ADR-0014 (c4dm/pyin, GPL-2.0-or-later), ADR-0017 (frame classes), ADR-0018 (vocal resynthesis research), ADR-0019 (SPTK), ADR-0026/0027/0029 (Rubber Band), ADR-0030 (glide between touching notes), `docs/design/pitch-glide-transitions.md`, `docs/design/vocal-resynthesis-research.md`

## Context
The Phase 6 formant render (`audition/phase6-formant-file`) now uses the offline Rubber Band stretcher for a constant formant offset (ADR-0027 amendment 3). Reported measurements (not repeated here): the envelope moves by about +240..+404 cents (-262..-337 for the other file); no third-difference peak above 6x that is not already in the source. The working hypothesis is that the shifter re-estimates the spectral envelope in every frame and the estimate jitters. Two boundary hitches on `vocadito_2` (4.322 s, 6.61-6.63 s) remain open (ADR-0030).

CLAUDE.md rule 9 asks for tested libraries or reference implementations before our own design, and ADR-0018 left licensing out of its analysis. This ADR records what a research pass found on three questions, with licences, and proposes what to try, in what order. The product is bound to GPL-2.0-or-later (ADR-0014) and is effectively GPLv3-or-later with Apache-2.0 (SPTK, ADR-0019) or JUCE. MIT, BSD, Apache-2.0, LGPL, GPL-2.0-or-later and GPL-3.0 code fits; GPL-2.0-only does not; AGPL is avoided.

### Findings
**F1. Rubber Band R3 estimates the envelope per hop with no memory.** `third_party/rubberband/src/finer/R3Stretcher.cpp:1454-1522`: cepstrum of the classification-FFT magnitude (2048 points at 44.1 and 48 kHz), lifter cut-off fixed at `floor(rate/650)` (67 bins at 44.1 kHz), `exp`, square; `adjustFormant` multiplies bins below 10 kHz by `env(i/scale)/env(i)`, clamped to x/÷60. No smoothing across hops, no f0-aware order. The live shifter mirrors it, so the move to the offline stretcher changed the phase-vocoder windows, not the envelope estimator. R3 honours only `OptionWindowShort`, `OptionFormantPreserved/Shifted` and `OptionChannelsTogether`: no setting reaches the estimator. The vendored 4.0.0 matched upstream HEAD (e4296ac, 2025-02-27) when checked, so there is no upstream fix. Logic Pro's Flex Pitch is the one product found that exposes the rate as a control ("Formant Track": "the interval at which formants are tracked along the signal").

**F2. Latent trap.** `setFormantScale` has no effect unless `OptionFormantPreserved` is set (`R3Stretcher.cpp:1340`), although the header says otherwise. `RubberBandPitchEngine` is safe today because `PitchFormants::preserved` is the default; `shifted` with a non-zero formant offset would silently do nothing.

**F3. Envelope estimators.**

| Method | Evidence | C++ code · licence |
|---|---|---|
| Plain cepstrum (Rubber Band) | "following the mean of the spectrum" (Röbel & Rodet, DAFx-05) | Rubber Band 4.0.0 · GPL-2.0-or-later |
| LPC | at high pitch "the all-pole model may start to represent those peaks" (same paper) | SPTK `lpc`, has tests · Apache-2.0 |
| True Envelope | follows peaks; order <= R/(2 dF); the paper does not address frame-to-frame variance | in repo (`core/analysis/envelope`), no time smoothing yet |
| CheapTrick (WORLD) | F0-adaptive: window of 3 periods (`cheaptrick.cpp:66`), smoothing of 2/3 f0 (`:176`), spectral recovery; Morise 2015 reports better accuracy and temporal stability than two other estimators (speech) | WORLD · modified BSD; inside SPTK as `pitch_spec` (`test_pitch_spec.bats`) · Apache-2.0 |
| WORLD vocoder / worldline | formant shift warps the envelope, `out[i]=in[i*ratio]`; periodic and aperiodic parts synthesised separately, aperiodic from random noise | OpenUtau `cpp/worldline`, unit-tested · MIT; WORLD `test/test.cpp` is a demo (f0 and formant scaling), not a test suite |
| Signalsmith Stretch | heuristic max-hold then min-hold across frequency, width tied to an f0 estimate (or `setFormantBase`); per frame | MIT, header-only |
| PSOLA grain resampling | no envelope estimate; reference: Praat `Sound_Pitch_changeGender_old` (resample, then PSOLA back to the old pitch) | Praat · GPL-2.0+/3.0+; ADR-0019 declined it as a dependency |

TANDEM-STRAIGHT (Kawahara et al., ICASSP 2008) and STAR (Morise & Yamashita, ICA 2013) are designed for temporal stability; no usable C++ was found for either. None of the papers tests singing, and no paper compares LPC, cepstral and CheapTrick on frame-to-frame jitter in singing.

**F4. Unpitched and breathy material.**
- Melodyne (Melodic algorithm, manual p. 55): "Sibilants remain unaltered even when the word or syllable to which they belong is shifted upwards or downwards in pitch"; "k"/"t" fragments and inhaling/exhaling count as sibilants. Breathy voiced notes: not covered in the readable part.
- Cubase VariAudio: segments are the tonal parts; consonants and breaths fall into gaps and pitch edits affect only the tonal parts unless a segment is extended. Sound On Sound (2012) advises cutting breaths out because spectral pitch shifting dulls noise, which implies noise inside a processed segment is processed too.
- Auto-Tune: US 5,973,252, a tracking failure sets the resampling ratio to 1 ("no change of output pitch"). zita-AT1 / x42 fat1 (GPL-2.0-or-later): after 5 failed estimates the signal is unvoiced and the error resets to 0 (`retuner.cc:290-302`).
- No source documents shifting only the harmonic part of a voiced breathy note.

| Item | Licence | Notes |
|---|---|---|
| HNM, Stylianou, IEEE TSAP 9(1):21-29, 2001 | paper | harmonics below a time-varying cut-off (the maximum voiced frequency, MVF), modulated noise above; pitch changed by resampling the harmonic spectrum, noise kept in sync; no code found |
| WORLD with D4C aperiodicity | modified BSD | C++; also in SPTK (`ap`, `world_synth`, both with tests) |
| Loris (Fitz & Haken) | GPL-2.0-or-later (README) | C++, `test/test_*.C`, version 1.8 (c) 1999-2010; noise carried per partial as bandwidth, not left as a residual |
| MVF estimation, Drugman & Stylianou (arXiv:2006.00521) | paper | abstract: earlier MVF methods degraded singing synthesis; theirs improves high-pitched voices |
| libsms (Eakin & Serra, DAFx-09) | not found | C |
| sms-tools (MTG) | AGPL-3.0+ | Python |
| Ahocoder (Erro et al., IEEE JSTSP 8(2):184-194, 2014) | no terms stated | executable only |

`HarmonicNoiseEngine` already realises "move the harmonics only, never move the residual" (tested on synthetic signals only). Its README names the missing piece: the "below the maximum voiced frequency" clause of ADR-0017 needs MVF stored per frame.

**F5. Note joins.**
- Auto-Tune: Retune Speed "controls how rapidly the pitch correction is applied"; Humanize applies a slower speed only to the sustained part of longer notes; the patent's Decay smoothing is user-set. fat1: one-pole filter on the error while the nearest note is unchanged, an immediate step (`_error = dm`) when it changes (`retuner.cc:483-487`). None scales time with the interval.
- OpenUtau (MIT): portamento default 80 ms long starting at -40 ms (centred on the boundary), default shape sine in-out = `y0+(y1-y0)(1-cos(pi t))/2` (a raised cosine), editable 2-320 ms.
- Melodyne separates connected ("soft") from disconnected ("hard") notes; the readable pages tie this to how neighbours move on edits, not to transition time. Cubase splits tonal segments from gaps.
- de Krom & Bloothooft (ICPhS 1995): the interval effect on transition duration is "most outspoken for the slow tempo" and "not much influenced by interval size" at medium and fast tempi; mean durations 135 vs 168 ms (/a/) and 130 vs 169 ms (/u/), female vs male, measured extremum to extremum (so including overshoot). Xu & Sun (JASA 2002, speech): "the maximum speed of pitch change varies quite linearly with excursion size", which would make duration roughly interval-independent (inference). Saitou et al. (WASPAA 2007): fixed second-order response per note change; overshoot omega 0.0348 rad/ms, zeta 0.5422, time constant about 53 ms (arithmetic).
- A raised cosine over 87 cents peaks at about 34 semitones/s in 40 ms, 17 in 80 ms, 9 in 150 ms (pi/2 x interval / duration).

## Decision (proposed)
1. **Measure the jitter before changing anything.** Add an objective check (a design note defines it; a candidate: mean absolute frame-to-frame change in dB of the envelope, sampled at fixed frequencies after mapping by the intended shift, on steady voiced notes, render vs source). The envelope comes from `core/analysis/envelope` on the analysis of the render and of the source. The tolerance is set from the first measurement; per `.claude/rules/audio-changes.md` it is not loosened afterwards.
2. **If the render's jitter is clearly above the source's, A/B in this order** (each by ear, owner's call, with the objective metric alongside): (a) a patch to the vendored `analyseFormant` that averages the cepstra across hops, reset where the spectrum changes abruptly (B7: smooth a little, never across phoneme boundaries); needs `third_party/rubberband/ANNODYNE.md` and `THIRD_PARTY.md` to stop saying "unmodified"; (b) CheapTrick via SPTK (`SpectrumExtractionByWorld`) as the envelope source for the gain ratio, after the ADR-0019 trial builds, with per-frame f0 from the existing tracker (WORLD substitutes a default f0 for unvoiced frames, `cheaptrick.cpp:218`); (c) full WORLD resynthesis only if (a) and (b) fail. PSOLA grain resampling is a fourth option if the epoch-mark path (ADR-0019) is ready; Praat is a reference only. If the jitter is not above the source's, the hypothesis is rejected and the hitch cause is sought elsewhere.
3. **Guard F2.** `RubberBandPitchEngine` validates that a non-zero formant offset is only combined with `PitchFormants::preserved` (an `invalid_argument` error otherwise), with a unit test.
4. **Unpitched material stays unshifted** (consistent with Melodyne, Cubase, the Auto-Tune patent and fat1). For voiced-breathy frames no commercial precedent exists; `HarmonicNoiseEngine` is the in-repo implementation. Store a per-frame MVF in the analysis; candidates to derive it: band aperiodicity from D4C (WORLD/SPTK; proposed here, not from a source) or Drugman & Stylianou. A design note chooses between them.
5. **No new harmonic+noise library is adopted.** Not adopted: sms-tools (AGPL), Ahocoder (no terms, executable only), libsms (licence not found), Loris (different noise model, last release 2010). Revisit only if the in-repo engine fails its objective checks on real breathy notes.
6. **Do not scale glide time with the interval.** No source supports it. A/B `glide_time` at 40, 80 and 150 ms on the 87-cent pair (`n006525`/`n006625`) and the 10 ms-gap pair; adopt the default proposed in `docs/design/pitch-glide-transitions.md` (the source's own glide time, clamped 30-150 ms) if the owner prefers it. Keep the raised-cosine shape (OpenUtau's default) and the touching/separated split at `k_touching_gap` (Melodyne's soft/hard precedent).

## Alternatives considered
- **Adopt WORLD resynthesis for all formant edits now.** Largest likely change in envelope behaviour, but it replaces the phase vocoder, regenerates aperiodic noise from random numbers, and breaks "no edits, same audio" unless confined to edited zones. Kept as last resort.
- **Write our own temporal smoothing outside Rubber Band.** Avoids patching vendored code, but needs the per-hop envelope that only R3 holds; a separate STFT envelope stage duplicates what Rubber Band does.
- **Switch to Signalsmith Stretch (MIT).** Different engine with a heuristic envelope that is also per frame; no evidence it is steadier. Possible A/B, not a fix.
- **Adopt Loris or libsms for harmonic+noise.** Rejected above; the repo's engine already does the harmonic-only move.
- **Interval-dependent glide time.** Unsupported by tuner documentation and by the singing data found.

## Consequences
- Item 1 adds a check to `docs/AUDIO_VERIFICATION.md` and a test; item 3 adds a test. Nothing changes rendered audio until item 2 is chosen; then a Rubber Band patch (a) changes rendered audio and must pass `ctest --preset debug-asan -L audio` and go through the `audio-verifier` and a listening round.
- Option (b) puts WORLD's CheapTrick in the render path through the SPTK wrapper `anno_sptk`; `AnnoLayers.cmake` needs an allowlist entry and `THIRD_PARTY.md` entries for SPTK, REAPER and the bundled WORLD, as ADR-0019 already requires.
- Harder: a vendored-code patch must be re-applied on every Rubber Band upgrade.

## Evidence limits
- No singing-specific evidence on envelope jitter was found. The 2015 CheapTrick evaluation was read as an abstract only; the TANDEM-STRAIGHT ICASSP page did not open.
- libsms's licence is not stated on any page read. The Melodyne manual copy found is a third-party excerpt that stops before the Pitch Tool and transition pages. Xu & Sun's volume and pages were not printed on the page read.
- Quotes from manuals and papers were obtained through a page-summarising fetch tool and re-fetched once; check any you rely on against the original.
- Items marked "inference", "arithmetic" or "proposed here" are the author's, not a source's.

## Spec updates
None overridden. Item 2(a) implements `docs/specs/annodyne-implementation-guide.md` B7 ("smooth the envelope over time a little, but not across phoneme boundaries"), which `core/analysis/envelope/README.md` lists as not yet done. If item 6 changes the default `glide_time`, ADR-0030 gets an amendment.

## Sources
- Code: Rubber Band https://github.com/breakfastquay/rubberband; WORLD https://github.com/mmorise/World; SPTK https://github.com/sp-nitech/SPTK; OpenUtau https://github.com/stakira/OpenUtau; Signalsmith Stretch https://github.com/Signalsmith-Audio/signalsmith-stretch; Praat https://github.com/praat/praat; x42 fat1 https://github.com/x42/fat1.lv2; Loris https://github.com/tractal/loris
- Manuals and patent: Logic Pro Flex Pitch https://help.apple.com/logicpro/mac/10.4/en.lproj/lgcpba8e3301.html; Melodyne 5 manual (third-party copy) https://images.equipboard.com/uploads/item/manual/112861/celemony-melodyne-5-assistant-manual.pdf; Celemony help https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_ToolMain; Steinberg segments and gaps https://archive.steinberg.help/nuendo/v11/en/cubase_nuendo/topics/sample_editor_variaudio/sample_editor_variaudio_segments_segments_and_gaps_r.html; Sound On Sound https://www.soundonsound.com/sound-advice/q-should-include-breath-sounds-when-doing-vocal-pitch-correction; US 5,973,252 https://patents.google.com/patent/US5973252; Auto-Tune Pro guide https://r2.gear4music.com/media/70/709346/download_709346.pdf
- Papers: Röbel & Rodet, DAFx-05 https://dafx.de/paper-archive/2005/P_030.pdf; Morise publications https://www.isc.meiji.ac.jp/~mmorise/world/publications.html; Morise, IEICE E98-D(7) https://global.ieice.org/en_transactions/information/10.1587/transinf.2015EDL8015/_p; TANDEM-STRAIGHT https://ieeexplore.ieee.org/document/4518514; Stylianou 2001 https://ee.columbia.edu/~dpwe/classes/e6820-2007-05/papers/Styl01-hnm.pdf; Drugman & Stylianou https://arxiv.org/abs/2006.00521; Erro et al. https://ekoizpen-zientifikoa.ehu.eus/documentos/5ed32d4c2999526aa74180d9?lang=en; Ahocoder https://aholab.ehu.eus/ahocoder/info.html; libsms paper https://dafx.de/paper-archive/details/fyg4D-T54Hdkr8Tm_dxWcQ; sms-tools https://pypi.org/project/sms-tools; de Krom & Bloothooft https://www.coli.uni-saarland.de/Phonetics/icphs/ICPhS1995/13_ICPhS_1995_Vol_1/p13.1_206.pdf; Xu & Sun https://labs.sonicfield.org/library/maximum-speed-of-pitch-change-and-how-it-may-relate-to-speech; Saitou et al. https://staff.aist.go.jp/m.goto/PAPER/WASPAA2007saitou.pdf
