# Building Melodyne-Class Processing: Implementation Guide

*Companion to `melodyne-features.md`. For every Melodyne tool: what back-end processes it needs, and how researchers and commercial developers get very good versions of those processes. Researched October 2026.*

---

## How to read this document

Melodyne's tools look independent, but they all sit on **one shared engine**: analyse the audio into notes, represent each note in an editable model, change the model, then resynthesise. So this guide has three parts:

1. **Part A – The core building blocks (B1–B15).** The ~15 back-end processes everything depends on. For each: what it does, the classic methods, the current best methods, open implementations, and the tricks that separate "works" from "sounds professional".
2. **Part B – Tool-by-tool breakdown.** For each Melodyne tool: which building blocks it needs, plus the tool-specific processing and quality tricks.
3. **Part C – Architecture, build order and how to measure quality.**

A recurring theme: **the best systems are hybrids.** Neural networks now win at *analysis* (finding pitch, onsets, notes, chords), while classic, interpretable DSP still dominates *editing and resynthesis* because it gives exact, artifact-controlled, explainable changes. Neural vocoders are closing the gap on resynthesis for voice specifically.

> Melodyne's own algorithms are proprietary (DNA is patented). Nothing here describes Celemony's code; it describes the published state of the art you would use to build equivalent functions.

---

## The shared engine at a glance

```
Audio ─► [B1 f0 tracking] ─► [B2 onsets] ─► [B3 note segmentation] ─► [B4 perceived pitch]
   │                                                                          │
   ├─► [B5 sinusoidal analysis] ─► [B6 harmonic + noise split] ─► [B7 spectral envelope]
   │                                                                          │
   ├─► (polyphonic) [B9 multi-pitch / transcription] ─► [B10 per-note separation]
   │                                                                          │
   ├─► [B11 beats/tempo]   [B12 key/scale/chords]   [B13 loudness]            │
   ▼                                                                          ▼
                    NOTE MODEL  (per note: pitch curve, timing, amplitude,
                                 envelope/formants, harmonics, noise part)
                                         │  edits from tools / macros
                                         ▼
                 [B8 resynthesis: phase vocoder / PSOLA / additive / vocoder]
                                         │
                                [B15 host integration (ARA)]
```

---

# Part A – Core Building Blocks

## B1. Monophonic pitch (f0) tracking

**What it must do:** Produce a continuous pitch curve (typically every 5–10 ms) plus a voiced/unvoiced decision, accurate to a few cents, with no octave errors. Everything downstream (note blobs, correction, vibrato tools) inherits its errors, so this is the single most important analysis stage.

**Classic (DSP) approaches**
- Time-domain periodicity functions: autocorrelation, AMDF, normalised cross-correlation, and YIN's cumulative mean normalised difference. Frequency-domain alternatives include the cepstrum and spectrum-matching methods such as SWIPE, which picks the pitch of the sawtooth whose spectrum best fits the input. ([PESTO paper, related work](https://arxiv.org/pdf/2309.02265))
- **pYIN** (probabilistic YIN): instead of one threshold, it draws several f0 candidates per frame from a distribution of thresholds, then Viterbi-decodes an HMM to find a smooth path through them. It shipped as a C++ Vamp plugin and is the engine of the Tony annotation tool. ([pYIN project](https://code.soundsoftware.ac.uk/projects/pyin))
- Before deep learning, comparative studies put YIN-family methods at the top, with pYIN best. ([CREPE paper](https://arxiv.org/pdf/1802.06182))
- **Harvest** (from the WORLD vocoder) is a robust speech/singing f0 estimator commonly used in voice tools. ([WORLD.jl docs](https://r9y9.github.io/WORLD.jl/latest))

**State of the art (neural)**
- **CREPE**: a CNN on raw waveform (1024-sample, 64 ms frames at 16 kHz). It beat pYIN and SWIPE, is more noise-robust, and keeps over 90% raw pitch accuracy even at a strict 10-cent tolerance. ([CREPE paper](https://arxiv.org/pdf/1802.06182))
- **RMVPE**: designed for vocal pitch *inside polyphonic music*; on clean vocals it is also top-tier (best on MIR-1K in its own evaluation). ([RMVPE paper](https://arxiv.org/pdf/2306.15412))
- **PESTO**: a tiny (130k-parameter) self-supervised model trained with a transposition-equivariance objective; reaches ~97–98% raw pitch accuracy and runs in real time. It beats CREPE/PENN in most conditions except very low SNR (0 dB), where CREPE degrades more gracefully. ([PESTO, TISMIR](https://transactions.ismir.net/articles/10.5334/tismir.251))
- **FCPE**: a fast context-based model reported around 96.8% RPA on MIR-1K, roughly 5× faster than RMVPE and 77× faster than CREPE. ([FCPE paper](https://arxiv.org/pdf/2509.15140))

**How to get a very good version**
- **Use a neural estimator for candidates, a sequence model for decisions.** Treat network output as a per-frame pitch posterior (most models classify into ~360 bins of 20 cents), then run Viterbi/HMM smoothing with transition costs that penalise octave jumps. This is the pYIN idea applied to neural salience.
- **Refine to sub-cent precision** after the coarse estimate: take a weighted average of neighbouring bins in the posterior, or re-estimate locally from the harmonic peaks (B5). Coarse-bin models alone are too quantised for intonation work.
- **Ensemble and vote** on hard frames (breathy onsets, fry, falsetto): disagreement between, say, RMVPE and pYIN is an excellent flag for "show the user this region".
- **Validate with the 10-cent metric, not just 50-cent.** Standard RPA uses a 50-cent window, which hides errors that are clearly audible in pitch correction.
- **Keep voicing separate from pitch.** Sibilants and breaths should be unvoiced; a wrong voicing decision causes pitch-shifted "s" sounds later (see B6).

---

## B2. Onset (note-start) detection

**What it must do:** Find where notes begin: consonants and attacks in vocals, picks/hammers in instruments, hits in drums.

**Classic:** Spectral flux (positive change in the magnitude spectrum, summed over bins) followed by peak picking with an adaptive threshold.

**Best-known improvements**
- **SuperFlux** applies a maximum filter across frequency before differencing, so a partial gliding during vibrato doesn't look like a new onset. It cut false positives by up to 60% on vibrato-heavy music (opera voice, violin) without losing true onsets, and it is causal, so it also works in real time. ([Böck & Widmer, DAFx-13](https://dafx.de/paper-archive/details/0oee-99Z88WL7pSo749gcA))
- **ComplexFlux** additionally weights the difference by a local-group-delay mask to suppress vibrato *and* tremolo. ([implementation notes](https://yup.readthedocs.io/en/latest/dsp/onsets.html))
- **CNN onset detectors** (Schlüter & Böck) defined the top of the MIREX onset task, ahead of RNN/LSTM approaches; peak picking or HMM inference then selects onsets. ([efficient onset detection paper](https://arxiv.org/pdf/1806.06773))

**How to get a very good version**
- For **singing**, pure energy/spectral onsets are weak because sung notes often change pitch without a new attack (legato). Combine three cues: spectral onset activation, **pitch-change points** in the f0 curve, and **phoneme boundaries** (see B3).
- Use **vibrato-suppressing** detection functions on voice and strings.
- Make thresholds **level-independent** (normalise the detection function locally) so quiet passages aren't missed.
- For percussive material, keep onset timing sample-accurate: refine a frame-level onset with a short-window energy envelope around the peak.

---

## B3. Note segmentation (turning a pitch curve into notes)

**What it must do:** Group the continuous f0 curve into discrete notes with start, end and a single representative pitch, deciding where a scoop belongs to a note vs. a transition, and how to treat vibrato and slides.

**Established methods**
- **HMM note tracking on the pitch track.** pYIN's note estimator Viterbi-decodes a second HMM over the pitch track, with attack, stable and silence states for each candidate pitch. Tony uses this and is the common baseline for monophonic singing transcription. ([pYIN project](https://code.soundsoftware.ac.uk/projects/pyin); [Tony](https://code.soundsoftware.ac.uk/projects/singing-voice/wiki/Tony))
- **Phoneme-informed segmentation**: align lyrics/phonemes to the audio and use phoneme boundaries to split notes that pitch-based methods merge. This outperformed Tony, especially on under-segmentation of repeated same-pitch notes. ([Li, Demirel, Proutskova & Dixon](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2021/LiDemirelProutskovaDixon-NLP4MusA2021.pdf))
- **Beat-aware HMMs**: a semi-beat-synchronous HMM jointly estimates notes and their rhythmic positions from singing f0, so note starts gravitate to musically plausible positions. ([ISMIR 2016](https://archives.ismir.net/ismir2016/paper/000004.pdf)) A pYIN fork does something similar by scaling onset transition probabilities by bar position. ([pypYIN fork](https://www.Github.com/georgid/pypYIN))
- **Neural note segmenters**: recent automatic-pitch-correction research (e.g. BERT-APC) uses a dedicated learned note segmentator before pitch estimation. ([BERT-APC](https://arxiv.org/pdf/2511.20006))

**How to get a very good version**
- Fuse cues in one probabilistic model: f0 stability, onset activation (B2), energy dips, voicing changes and phoneme boundaries.
- Make **repeated same-pitch notes** a specific test case; it's the most common failure.
- Keep segmentation **editable** (Melodyne's Note Assignment Mode exists because no analysis is perfect). Design the data model so user corrections re-run only local analysis.

---

## B4. Perceived note pitch ("musically weighted" pitch)

**What it must do:** Assign each note the pitch a *listener* would hear, so that correcting it to the grid sounds in tune. Simple averaging of the f0 curve is biased by scoops, falls and vibrato.

**What the research says**
- For long vibrato tones, perceived pitch is close to the **geometric mean** of the frequency excursion; for short tones, it is a **weighted time average** with a threshold, and with very wide/slow vibrato listeners can hear the high and low parts separately (d'Alessandro & Castellengo). ([The pitch of short-duration vibrato tones](https://hal.archives-ouvertes.fr/hal-02008510))
- Some studies find stability-sensitive weighting fits better for asymmetric vibrato. ([Mesz & Eguia](https://www.aacademica.org/manuel.eguia/27.pdf))
- The pyAMPACT toolkit implements both a geometric-mean pitch and a **rate-of-change-weighted mean** (slow-changing frames weigh more, with a gamma parameter controlling the weighting range), plus vibrato rate and extent from the FFT of the note's f0 trace. ([pyAMPACT](https://arxiv.org/pdf/2412.05436))
- Annotation practice for "stationary pitch" marks the stable region (excluding attacks, releases and glides) and averages it; learned stationary-pitch predictors have been compared against note-wise average and weighted-median baselines. ([BERT-APC](https://arxiv.org/pdf/2511.20006))

**How to get a very good version**
- Compute pitch in **cents (log frequency)**, not Hz, before averaging (geometric mean = arithmetic mean in log space).
- **Weight by stability**: down-weight frames with high |d f0/dt|, low energy, or low voicing confidence; exclude the first and last ~30–80 ms or detect them as transitions.
- For vibrato notes, average over **whole vibrato cycles** to avoid bias from partial cycles.
- Calibrate with listening tests: have musicians adjust a reference tone to match note pitch and fit the weighting parameters to their judgements.

---

## B5. Sinusoidal (partial) analysis

**What it must do:** Measure every harmonic's frequency, amplitude and phase over time. This feeds noise separation, formant estimation, the Sound Editor, polyphonic analysis and additive resynthesis.

**Methods and accuracy tricks**
- **QIFFT** (quadratically interpolated FFT): fit a parabola to the log-magnitude of the three bins around a peak. With enough zero-padding (a factor of 5 or more), its errors are inaudible and it is very close to the maximum-likelihood estimator for well-separated, steady sinusoids. ([J.O. Smith, SASP](https://ccrma.stanford.edu/%7Ejos/sasp/Optimal_Peak_Finding_Spectrum.html))
- **Bias correction** for window choice and zero-padding (CQIFFT), and interpolation on a **power-scaled** magnitude (rather than log) can reduce bias further. ([Abe & Smith](https://artful-design.stanford.edu/papers/cqifft-correcting-bias-in-sinusoidal-parameter-estimator-based-quadratic-interpolation-of-fft); [Werner & Germain](https://pure.qub.ac.uk/en/publications/sinusoidal-parameter-estimation-using-quadratic-interpolation-aro/))
- **Vibrato is the hard case.** QIFFT is badly biased when frequency is changing, and in natural vibrato that bias leaves significant energy in the residual. Use non-stationary estimators: Röbel's frequency-slope bias correction, or the **reassignment** method, which estimates frequency better than QIFFT for modulated partials. ([Röbel](https://muse.jhu.edu/article/239289); [Marchand, DAFx-08](https://pageperso.univ-lr.fr/sylvain.marchand/Publications/dafx08.pdf))
- **Window length vs modulation**: Abe & Smith derive the maximum window length for a given error bound and AM/FM rate (e.g. keeping a fast chirp's bias under 1 Hz with a Hann window needs a window shorter than ~31 ms). ([CCRMA STAN-M-116](https://ccrma.stanford.edu/STANM/stanms/stanm116/index.html))
- **Partial tracking**: link peaks frame to frame (McAulay–Quatieri / SMS peak continuation). For voice, use the known f0 as a **harmonic sieve**: predict where harmonic *k* should be and search there, which is far more robust than blind tracking.

**How to get a very good version**
- Use **pitch-synchronous or f0-adaptive windows** (e.g. 3–4 periods long) so resolution follows the voice.
- Estimate frequency **slope** as well as frequency per frame, to handle vibrato and glides.
- Measure quality by the **residual**: subtract the resynthesised sinusoids from the original; a low-energy, noise-like residual means the analysis is good. ([ATS manual](https://ccrma.stanford.edu/~juan/ATS_manual.html))

---

## B6. Harmonic + noise decomposition (sibilants, breaths)

**What it must do:** Split each note into a pitched part and a noise part so sibilants and breaths can be leveled separately and are not pitch-shifted (Melodyne 5's Sibilant tool and noise display).

**Methods**
- **SMS (Spectral Modeling Synthesis)**, Serra 1989/1990: track sinusoids, subtract them, model the residual as time-varying filtered noise; resynthesise both separately so each can be transformed independently. ([SMS description](https://depts.washington.edu/dxscdoc/Help/Classes/SMS.html); [Serra 1997](https://repositori.upf.edu/items/0a7becdc-9673-4461-b011-48554c489c76/full))
- **Residual minimisation**: SMS analysis parameters (window type/size, hop, FFT size) can be optimised by measuring how free of partials the residual is. ([Serra, ASA 1994](https://auditory.org/asamtgs/asa94mit/4pMU/4pMU4.html))
- **ATS** extends this with a psychoacoustic, critical-band noise model and masking-aware tracking. ([ATS theory](https://dxarts.washington.edu/sites/dxarts/files/documents/wiki/ats_theory.pdf))
- **Harmonic plus Noise Model (HNM, Stylianou)** is the speech-oriented variant. ([SMS-Festival paper](https://www3.pd.istc.cnr.it/piero/homepage/papers/PieroCosi/sg-MAVEBA2007.pdf))
- **WORLD's D4C** estimates band aperiodicity (how noisy each frequency band is per frame), which gives a ready-made voiced/noise mix for voice. ([WORLD.jl docs](https://r9y9.github.io/WORLD.jl/latest))
- **DDSP** builds the same harmonic + filtered-noise synthesiser in a differentiable form, so a neural net can learn to predict harmonic amplitudes and noise filters directly. ([DDSP paper](https://ar5iv.labs.arxiv.org/html/2001.04643))

**Sibilant/breath classification specifically**
- Conventional de-essers use a band-pass (roughly 5–10 kHz) side-chain into a compressor, which fails on quiet sibilants and depends on absolute level. Level-independent designs compare sibilance-band energy to broadband energy instead. ([Patent US11322170](https://patents.google.com/patent/US11322170))
- Commercial level-independent detectors (e.g. Eiosis E2) and AI phoneme detection (sonible pure:deess) show the direction of travel: detection that ignores absolute level and recognises phonemes rather than frequency bands. ([SOS review](https://www.soundonsound.com/reviews/eiosis-e2-de-esser); [sonible manual](https://www.sonible.com/wp-content/uploads/2025/03/manual-pureDeess.pdf))

**How to get a very good version**
- Classify per frame with features: voicing/periodicity, aperiodicity ratio, spectral centroid, zero-crossing rate, high-band vs broadband energy ratio; better still, a small **frame classifier** (voiced / sibilant / breath / plosive / silence) trained on labelled vocals.
- Within voiced frames, still keep a **noise component** (breathiness) so voices don't sound synthetic.
- When stretching time, stretch sibilants **less** than vowels; when shifting pitch, **don't shift** noise, only re-shape it with the original envelope.

---

## B7. Spectral envelope (formant) estimation

**What it must do:** Estimate the smooth curve over the harmonics (the vocal-tract filter) so pitch can change without changing timbre, and formants can be shifted on purpose.

**Methods compared**
- **LPC** (all-pole): cheap, but follows harmonic peaks and suffers aliasing at high pitch; model-order choice is critical.
- **Cepstral smoothing**: low-pass the cepstrum, but plain smoothing under-estimates the envelope between harmonics.
- **True Envelope (TE)**: iterative cepstral smoothing that pushes the envelope up to pass through the harmonic peaks. Röbel & Rodet made it efficient (2.5–11× faster through sub-sampling and step-size control; Hamming smoothing to remove ringing) and identified it as the most promising envelope for envelope-preserving pitch shifting in a phase vocoder. ([DAFx-2005](https://dafx.de/paper-archive/details/EDSTSaO17Gd_0LV03iwDfw))
- TE works well even for **high-pitched** signals where LPC and discrete cepstrum struggle, and can run in real time. ([HAL](https://hal.archives-ouvertes.fr/hal-01161347))
- **TE-LPC**: compute a TE first, then fit an all-pole model to it; this sidesteps LPC's unknown-order problem and better fits the important low harmonics. ([HAL](https://hal.archives-ouvertes.fr/hal-01161354))
- Practical rule from IRCAM's tooling: the TE cepstral order must stay below SR / (0.5 × max f0), so set it from the highest pitch expected. ([AudioSculpt docs](https://support.ircam.fr/docs/AudioSculpt/3.0/co/True%20Envelope.html))
- **CheapTrick** (WORLD): F0-adaptive windowing plus pitch-synchronous smoothing; robust to small f0 errors and widely used in singing/voice-conversion pipelines. ([WorldNet](https://www.nuget.org/packages/WorldNet); [overview](https://aiunderstanding.org/learn/source-filter-vocoding-and-world))

**How to get a very good version**
- Use **f0-informed** estimators (TE with order set from f0, or CheapTrick) rather than fixed-order LPC.
- **Smooth the envelope over time** a little to avoid flutter, but not across phoneme boundaries.
- For large upward shifts, the original envelope is only sampled at the original harmonic spacing; **interpolate carefully** between harmonics to avoid hollow or buzzy timbre.
- Always shift formants **relative to the envelope**, never by resampling the waveform.

---

## B8. Resynthesis: time-stretching and pitch-shifting engines

This is where audio quality is won or lost. There are several families; top products typically **combine** them per signal type.

### B8a. Phase vocoder (frequency domain)
- Modifies STFT phases so frames overlap-add coherently at a new hop size. The raw method causes **transient smearing** and a reverberant "phasiness". ([PhaVoRIT](https://hci.rwth-aachen.de/PhaVoRIT))
- **Identity phase locking** (Laroche & Dolson 1999): assign each bin to its nearest spectral peak and give it the peak's phase rotation, which restores vertical phase coherence and removes most phasiness. ([implementation notes](https://docs.rs/crate/timestretch/latest/source/src/stretch/phase_locking.rs))
- **Phase Gradient Heap Integration ("Phase Vocoder Done Right", Průša & Holighaus)**: integrate phase from both time and frequency phase derivatives, visiting bins in order of magnitude via a max-heap. No peak picking or transient heuristics are needed, and in listening tests it was competitive with commercial time-stretchers. ([paper](https://arxiv.org/pdf/2202.07382)) It keeps chirps, vibrato and glides coherent; identity locking is still slightly better on steady polyphony. ([library notes](https://libraries.io/npm/@audio%2Fstretch-pvoc-lock))
- A real-time, causal variant (RTPGHI) exists for streaming. ([implementation](https://docs.rs/math-dsp/latest/src/math_audio_dsp/rtpghi.rs.html))

### B8b. Transient handling
- **Röbel's transient processing** (2003) detects transients in the phase vocoder and treats them specially; PGHI's authors suggest it for true transient sharpening because it reuses the frequency phase derivative they already compute. ([PVDR](https://arxiv.org/pdf/2202.07382))
- **HPSS-based stretching** (Driedger, Müller, Ewert): split into harmonic and percussive parts; stretch harmonic with a long-window phase vocoder and percussive with short-window overlap-add. This preserves transients without explicit detection. ([Fraunhofer](https://publica.fraunhofer.de/handle/publica/236969))
- Other refinements: phase reset in silences, and stretching *around* transients rather than through them. ([PhaVoRIT](https://hci.rwth-aachen.de/PhaVoRIT); [patent background](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9240196))

### B8c. Time-domain methods (PSOLA / WSOLA)
- **TD-PSOLA**: cut pitch-synchronous grains centred on glottal pulses and re-space them. Excellent for clean monophonic voice; needs accurate pitch marks (epochs). WSOLA is the simpler non-pitch-synchronous variant used for speech. ([library comparison](https://libraries.io/npm/@audio%2Fstretch-pvoc-lock))

### B8d. Model-based resynthesis
- **Additive (sinusoidal) + noise**, from B5/B6: change partial frequencies directly and re-impose the original envelope to preserve formants. ([SMS](https://depts.washington.edu/dxscdoc/Help/Classes/SMS.html))
- **WORLD vocoder**: resynthesise from f0 + spectral envelope + aperiodicity; each can be edited independently. ([overview](https://aiunderstanding.org/learn/source-filter-vocoding-and-world))
- **Neural source-filter vocoders**: NSF-HiFiGAN and Source-Filter HiFi-GAN feed a pitch-dependent excitation into a neural filter, giving high quality *and* pitch control; SiFi-GAN beat HiFi-GAN on singing quality and runs fast on CPU. ([SiFi-GAN](https://arxiv.org/pdf/2210.15533)) The open DiffSinger community's **PC-NSF-HiFiGAN** shifts pitch ±12 semitones while preserving formants, trained on ~79 h of singing. ([openvpi releases](https://github.com/openvpi/vocoders/releases))

### B8e. Production libraries
- **Rubber Band Library**: phase-vocoder stretcher with peak phase locking and phase resynchronisation at transients; v3 added the higher-quality but costlier **R3 "Finer"** engine and independent formant shifting. ([README diff](https://donkey.abanbytes.eu/david/librubberband/commit/a96c85befca3a1041a27d2ff45f561f7a94e5439.diff); [changelog](https://donkey.abanbytes.eu/david/librubberband/raw/commit/1aa241e6d0119212d10e462f6eb473be80aff042/CHANGELOG))

**How to get a very good version (the playbook)**
- **Choose engine by content**: PSOLA or harmonic-model resynthesis for clean monophonic voice; phase-locked/PGHI vocoder for sustained polyphony; HPSS-split or transient-aware processing for anything with attacks.
- **Note-aware processing beats file-wide processing**: because you know note boundaries, stretch vowels, protect consonants and attacks, and reset phase at onsets.
- **Multi-resolution**: long windows for low frequencies/steady tones, short windows for highs/transients.
- **Pitch-shift = formant-preserving transposition of the harmonic part + untouched noise part + envelope re-imposition.**
- **Smooth parameter changes** (pitch, gain, formant) with short crossfades/ramps at note boundaries to avoid clicks and zipper noise.
- **Optional neural polish**: resynthesise voice with a source-filter neural vocoder conditioned on the *edited* f0 and envelope; keep a DSP path as fallback.

---

## B9. Polyphonic multi-pitch estimation and transcription (DNA analysis)

**What it must do:** Find every note in a chord: pitch, onset, offset and level, for piano, guitar, pads, choirs.

**Classic DSP**
- **Klapuri's iterative estimation and cancellation**: find the most prominent f0, subtract its harmonics (using **spectral smoothness** to estimate how much energy at shared harmonics belongs to it), repeat. Works within a single frame and tolerates inharmonicity. ([patent citing Klapuri 2003](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11900825); [TUNI record](https://researchportal.tuni.fi/en/publications/multiple-fundamental-frequency-estimation-based-on-harmonicitiy-a/))
- **Harmonic-amplitude summation** with spectral whitening, offered as direct, iterative and joint estimators. ([Klapuri, ISMIR 2006](https://archives.ismir.net/ismir2006/paper/000125.pdf))
- **Joint evaluation of F0 hypotheses** (Yeh, Röbel, Rodet) scores combinations by harmonicity, spectral smoothness and synchronous amplitude evolution of partials. ([DAFx 2004](https://dafx.de/paper-archive/2004/P_234.PDF))
- **NMF with harmonic constraints**: factor the spectrogram into note templates × activations; plain NMF learns note mixtures, so templates are constrained to harmonic combs. ([FMP notebook](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C8/C8S3_NMFSpecFac.html))

**State of the art (neural)**
- **Onsets and Frames** (piano): jointly predicts onsets and frame activity, and only allows a note to start where the onset head agrees, more than doubling note-with-offset F1 on MAPS. ([paper](https://arxiv.org/pdf/1710.11153))
- **MT3**: a Transformer that outputs MIDI-like event tokens, multi-instrument and multi-dataset; set new state of the art across six datasets in frame, onset and onset+offset F1. ([MT3](https://ar5iv.labs.arxiv.org/html/2111.03017)) Follow-up work scaled it further with mixtures of monophonic transcriptions. ([ISMIR 2022](https://archives.ismir.net/ismir2022/paper/000004.pdf))
- **Basic Pitch** (Spotify): lightweight, instrument-agnostic, polyphonic, outputs MIDI with pitch bends; jointly predicts onsets, multi-pitch and note activations; works best on one instrument at a time. ([GitHub](https://github.com/spotify/basic-pitch); [IRCAM talk](https://brahms.ircam.fr/en/media/x6cb24a_basic-pitch-a-lightweight-model-for-multi))

**How to get a very good version**
- Use a **neural transcriber for note detection** (Onsets-and-Frames style for piano, MT3/Basic Pitch-style for general), then **refine each note with DSP**: exact f0 and inharmonicity from partials (B5), exact onset (B2).
- Train or fine-tune **per instrument family** (piano/decaying vs pads/sustained) mirroring Melodyne's Decay vs Sustain modes; the expected amplitude evolution is a strong prior.
- Model **inharmonicity** for piano and guitar strings (upper partials run sharp); fixed harmonic combs misassign energy otherwise.
- Expose **confidence**, and let users activate/deactivate candidate notes (Melodyne's "ghost notes").

---

## B10. Per-note separation of polyphonic audio (DNA editing)

**What it must do:** Once notes are known, split the actual audio energy into one signal per note so each can be shifted, faded or muted, then sum them back. Analysis alone isn't enough; editing needs separation.

**Methods**
- **Score-informed NMF**: use the note list as a "score" to constrain templates (harmonic combs at known pitches) and activations (only active between known onset/offset), then build soft (Wiener-like) masks per note. Score information considerably improves separation. ([Ewert/Müller work via FMP](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C8/C8S3_NMFSpecFac.html); [ISMIR 2015](https://www.ismir2015.uma.es/articles/99_Paper.pdf))
- This was demonstrated explicitly for **audio editing**: decomposing piano recordings into individual note events so selected notes can be modified while the rest stay in the mix. ([Driedger et al.](https://www.audiolabs-erlangen.com/resources/2013-ACMMM-AudioDecomp/2013_DriedgerGPEM_SourceSeparationInterface_ACM.pdf); [Müller & Zalkow tutorial](https://www.audiolabs-erlangen.de/content/resources/MIR/00_2019_TutorialFMP_ISMIR/2019_MuellerZalkow_ISMIR-Tutorial_AudioDecomp.pdf))
- **Separate-and-restore**: a second NMF stage plus refined masks to recover note signals in mono-timbral piano recordings. ([Dittmar et al., WASPAA 2015](https://www.audiolabs-erlangen.com/content/05_fau/professor/00_mueller/03_publications/2015_DittmarDM_SeparateRestore_WASPAA.pdf))
- **Harmonic + percussive templates** per note (attack noise plus harmonic body) improve piano note modelling. ([Jeong & Nam, AES 2017](https://mac.kaist.ac.kr/pubs/JeongNam-aes2017.pdf))

**How to get a very good version**
- **Divide shared harmonics** using each note's smooth spectral envelope (spectral smoothness): interpolate what each note "should" have at the shared bin, and split energy proportionally.
- Keep a **residual** for energy not explained by any note (reverb, noise); leave it untouched so unedited playback is bit-identical or near it.
- **Edit only what changed**: resynthesise the edited notes and subtract/replace their original contribution, rather than reconstructing the whole mix from parts. This minimises artifacts on untouched notes.

---

## B11. Beat tracking and tempo mapping

**Methods:** onset-strength function → periodicity (autocorrelation/comb filters) → beat phase, then a sequence model for consistency.

**State of the art**
- Most neural beat trackers post-process with madmom's **Dynamic Bayesian Network (DBN)**, which assumes stable tempo and meter. ([TISMIR](https://transactions.ismir.net/articles/238))
- **Beat This!** (Foscarin, Schlüter, Widmer, ISMIR 2024) drops the DBN for plain peak-picking, trains on diverse data (solo instruments, meter changes, classical with large tempo variation), uses a shift-tolerant loss and conv/transformer blocks, and reached state-of-the-art F1; it is weaker on continuity metrics. ([ISMIR program](https://ismir2024program.ismir.net/poster_10.html)) Its MIT-licensed weights and lack of a DBN make it practical commercially, as madmom's DBN carries non-commercial licensing. ([GGUF model card](https://huggingface.co/cstr/beat-this-GGUF))

**How to get a very good version**
- For **free, rubato performances** (Melodyne's tempo detection), avoid strong constant-tempo priors; allow the tempo curve to move, then let the user correct a few anchor beats and re-solve.
- Derive tempo as a **smoothed curve between beats**, and expose both "keep human tempo" and "flatten" options via note-aware stretching (B8).

---

## B12. Key, scale and chord recognition

**Key/scale**
- Build a **pitch-class profile** (chroma or HPCP, which accounts for harmonics and tuning) and correlate with key templates (Krumhansl–Schmuckler and successors). ([ISMIR 2004](https://archives.ismir.net/ismir2004/paper/000164.pdf)) Essentia ships many profile sets (Krumhansl, Temperley, Shaath for pop/electronic, EDM-derived, etc.). ([Essentia Key](https://essentia.upf.edu/reference/std_Key.html))
- Estimate the **reference tuning** first and correct for it before key detection. ([ISMIR 2006](https://archives.ismir.net/ismir2006/paper/000134.pdf))
- For **non-Western/microtonal scale extraction** (Melodyne editor/studio), build a high-resolution (e.g. 1–5 cent) histogram of *note* pitches (from B4, not raw frames), find peaks, and report them as a custom scale.

**Chords**
- Traditional pipeline: features → template matching → sequence decoding (HMM/CRF). ([BTC paper](https://arxiv.org/pdf/1907.02698))
- **BTC** (bi-directional Transformer) matches multi-stage systems with a single training phase. ([BTC](https://ar5iv.labs.arxiv.org/html/1907.02698))
- **ChordFormer** (Conformer) targets large vocabularies (7ths, inversions, extensions), using a re-weighted loss for rare chords; it reports +2% frame-wise and +6% class-wise accuracy. ([arXiv](https://arxiv.org/html/2502.11840v1))
- A 2026 bidirectional **Mamba** model (BMACE) reaches comparable performance with a fraction of BTC's parameters. ([arXiv](https://arxiv.org/pdf/2601.02101))

**How to get a very good version**
- When you already have **note-level transcription** (B9), recognise chords from the notes as well as the audio; combining symbolic and audio evidence is more robust.
- Use **structured chord outputs** (root, quality, bass, extensions) instead of one-hot labels to handle large vocabularies.
- Let the DAW's **chord track** (via ARA, B15) override or seed detection.

---

## B13. Loudness measurement (per note)

- Use **ITU-R BS.1770** loudness: K-weighting filter, mean-square, and for program loudness 400 ms blocks with 75% overlap and gating; momentary loudness uses a 400 ms window without gating. ([lupy glossary](https://lupy-nocarryr.readthedocs.io/en/latest/glossary.html))
- **Per-note adaptation:** notes are often shorter than 400 ms, so compute K-weighted loudness over the note's own span (excluding attack transients and noise parts), and optionally a frame-level curve for the "within-note" leveling.
- For timbre-aware loudness at very different pitches, a full psychoacoustic loudness model (Zwicker/Moore–Glasberg) is more accurate than K-weighting, at higher cost.

---

## B14. Tuning systems and adaptive just intonation

- **Fixed tunings**: store scales as cent tables relative to a tonic plus a reference frequency; snap targets come from the table.
- **Adaptive just intonation**: three main approaches.
  - **Hermode Tuning** retunes intervals between adjacent chord tones to just ratios on the fly, while shifting the global pitch to stay close to equal temperament and compensating pitch drift. ([Stange et al.](https://arxiv.org/pdf/1706.04338))
  - **Stange, Wick & Hinrichsen** continually solve a system of linear equations (think springs between notes); when not all intervals can be pure, it automatically yields a tempered compromise. Open source. ([abstract](https://paperswithcode.co/paper/1706.04338))
  - **Sethares-style dissonance minimisation** adjusts pitches by gradient descent on a sensory dissonance measure that depends on the timbre's partials. ([Sethares](https://minds.wisconsin.edu/bitstream/handle/1793/9494/file_1.pdf?sequence=1))
- **Key engineering issue:** drift. Pure intervals chained through progressions make the absolute pitch wander, so all practical schemes anchor or compensate. ([adaptive tuning FAQ](https://robertinventor.com/musicandvirtualflowers/ascii_tree/jdl/adaptive_tuning.html))

---

## B15. Host integration (ARA)

- **ARA (Audio Random Access)** lets a plug-in read the host's audio files directly and exchange musical information such as the tempo map and chord track, removing the "transfer" step. Celemony released the **ARA SDK under Apache 2.0** on GitHub in May 2021, with an open-source JUCE integration example. ([Celemony, 10 years of ARA](https://www.celemony.com/en/10-years-of-ara); [KVR](https://www.kvraudio.com/news-print.php?id=51704))
- **Practical build path:** JUCE + ARA SDK for the plug-in shell; analysis runs on background threads over the whole file; edits are stored as a document the host saves with the session.

---

# Part B – Tool-by-Tool Breakdown

Numbering follows `melodyne-features.md`. Each entry lists the **back-end processes needed** (with the building block from Part A) and then **how to make that tool very good**.

### 1. Audio analysis and note detection ("blobs")
**Back-end processes:** f0 tracking (B1) → onset detection (B2) → note segmentation (B3) → perceived pitch per note (B4) → partial analysis (B5) → harmonic/noise split (B6) → envelope (B7) → per-note amplitude curve (B13).
**Making it very good:**
- Run analysis **offline over the whole file** (ARA gives random access), so models can look ahead and behind; bidirectional/sequence models then beat causal ones.
- Store a **rich note object**: pitch curve, perceived pitch, onset/offset, attack end, amplitude curve, harmonic amplitudes per frame, noise envelope, spectral envelope, confidence values.
- Draw the blob from the amplitude envelope and the pitch line from the f0 curve, both sampled from the same model the resynthesis uses, so what the user sees is what they hear.

### 2. Detection algorithms (Melodic, Percussive, Percussive Pitched, Polyphonic Sustain/Decay, Universal)
**Back-end processes:** a content classifier plus one analysis/resynthesis chain per mode.
| Mode | Analysis chain | Resynthesis |
|---|---|---|
| Melodic | B1 → B2 → B3 → B4 → B5 → B6 → B7 | B8c PSOLA or B8d harmonic model/neural vocoder; B8a fallback |
| Percussive | B2 onsets only (CNN/SuperFlux) | B8b transient-preserving stretch; resample/PV for transposition |
| Percussive Pitched | B2 + f0 of the resonant tail (B1 restricted to post-attack) | split attack (keep) + tail (pitch-shift) |
| Polyphonic Decay / Sustain | B9 + B10 with decay vs sustain amplitude priors | per-note B8d/B8a, then remix |
| Universal | none at note level; global analysis | B8a PGHI/phase-locked + B8b transients |

**Making it very good:**
- **Auto-select** with a classifier on simple features (voicing ratio, polyphony estimate, onset density, spectral flatness) or a small CNN, but always let the user override.
- For Percussive Pitched, use **pitch of the decay** (after the attack) because drum attacks are noisy and their pitch often glides down.

### 3. Note Assignment Mode (fixing analysis errors)
**Back-end processes:** access to analysis *candidates*, not just the final decision: f0 posteriors (B1), alternative octave paths, onset activation curve (B2), multi-pitch candidate list with scores (B9).
**Making it very good:**
- Keep the **second-best Viterbi paths** and candidate notes so "move to other octave" or "activate this ghost note" simply re-selects a stored hypothesis.
- Re-run **local** segmentation/separation (B3/B10) only around the edited region, so corrections are instant.

### 4. Main Tool (pitch, position, length, snap)
**Back-end processes:** perceived pitch (B4) for snap targets, scale tables (B14), formant-preserving pitch shift (B7 + B8), note-aware time-stretch (B8a/b/c), crossfaded note boundaries.
**Making it very good:**
- Snap the **perceived** pitch, not the mean or the start of the note.
- Pitch-shift by shifting the **whole pitch curve** by a constant (preserves vibrato and scoops), with the envelope re-imposed (formants unchanged).
- When moving a note in time, **stretch neighbours'** boundary regions (consonants protected) to fill or remove gaps, rather than leaving silences.

### 5a. Pitch Tool (incl. note transitions)
**Back-end processes:** pitch curve per note (B1), transition regions between notes (B3), curve editing and resynthesis (B8).
**Making it very good:**
- Model the transition as a **parametric curve** (e.g. sigmoid in cents between the two note pitches) fitted to the original glide; changing "transition speed" changes the sigmoid's width. This stays natural compared with linear interpolation.

### 5b. Pitch Modulation Tool (vibrato)
**Back-end processes:** decomposition of each note's pitch curve into **centre + slow drift + fast modulation** (B4 math), vibrato rate/extent estimation, resynthesis from the edited curve (B8).
**Making it very good:**
- Decompose with **filtering in cents**: drift = low-pass of the curve (cut-off ~2–3 Hz, below typical vibrato rates); modulation = curve − drift − centre. Use zero-phase filters so nothing shifts in time.
- Estimate vibrato rate and extent from the FFT of the note's f0 trace, as pyAMPACT does, to display and scale it meaningfully. ([pyAMPACT](https://arxiv.org/pdf/2412.05436))
- Real vibrato also modulates **amplitude and timbre** in sync; reducing pitch vibrato while leaving the amplitude tremolo can sound odd, so optionally scale both.

### 5c. Pitch Drift Tool
**Back-end processes:** the same decomposition as 5b; scaling of only the drift component.
**Making it very good:** use a **stability-weighted** drift estimate (ignore attack scoop and release fall) so straightening drift doesn't flatten intentional ornaments; let scoops be handled by the transition tool.

### 5d. Correct Pitch macro
**Back-end processes:** perceived pitch (B4), target pitch from scale/chord grid (B12, B14), per-note offset scaling, drift scaling (5c), resynthesis (B8).
**Making it very good:**
- Per note: `offset_cents = target − perceived`; apply `offset × amount` to the whole curve, then `drift × (1 − drift_amount)`.
- Add a **tolerance** so notes already within e.g. ±10–15 cents can be left alone (sounds more human).
- **Context-aware targets** are the research frontier: Deep Autotuner predicts note-wise corrections from the relationship between the vocal and accompaniment spectrograms, treating pitch as continuous and learning to keep intentional deviations. ([Wager et al.](https://arxiv.org/pdf/2002.05511)) BERT-APC infers the intended pitch from musical context without a reference. ([BERT-APC](https://arxiv.org/pdf/2511.20006)) Such models can *suggest* targets, especially for blue notes and deliberate bends, while the user stays in control.

### 6. Formant Tool
**Back-end processes:** spectral envelope per frame (B7), harmonic or STFT resynthesis with envelope re-imposition (B8d/B8a), noise-part envelope shift (B6).
**Making it very good:**
- Implement formant shift as **frequency warping of the envelope** (e.g. scale the frequency axis by 2^(cents/1200)), then re-weight harmonics from the warped envelope.
- Use **True Envelope or CheapTrick**, not low-order LPC, especially for high voices. ([Röbel & Rodet](https://dafx.de/paper-archive/details/EDSTSaO17Gd_0LV03iwDfw))
- Shift the noise envelope together with the harmonic envelope so breathiness stays consistent.
- Optional: neural source-filter vocoders such as PC-NSF-HiFiGAN give strong formant-preserving pitch shifts on voice. ([openvpi](https://github.com/openvpi/vocoders/releases))

### 7. Amplitude Tool
**Back-end processes:** per-note gain stage in the note model; for polyphonic material, per-note separated signals (B10).
**Making it very good:** apply gain with **short ramps at note edges** (5–20 ms) and on the note's own components only; in DNA mode, gain the separated note and re-sum, keeping the residual untouched.

### 8. Leveling macro
**Back-end processes:** per-note loudness (B13), within-note loudness curve, gain computation, smoothing.
**Making it very good:**
- Compute targets in **loudness units (K-weighted)** rather than raw RMS, so bright and dark notes are balanced as perceived. ([BS.1770 glossary](https://lupy-nocarryr.readthedocs.io/en/latest/glossary.html))
- Exclude **noise parts** (sibilants, breaths) from the measurement, or they distort vowel-level targets.
- Two controls map naturally to two stages: note-to-note (scale each note's deviation from the phrase average) and within-note (scale the deviation of the frame curve from the note average, after smoothing).

### 9. Fade Tool
**Back-end processes:** per-note amplitude envelope applied before summation; per-note separation for polyphonic (B10).
**Making it very good:** use **equal-power or logarithmic** fade curves by default (linear-in-amplitude fades sound abrupt at the end); for DNA, fade only the selected note's separated signal.

### 10. Sibilant Balance Tool (and noise display)
**Back-end processes:** frame classification voiced/sibilant/breath (B6), harmonic+noise decomposition (B6), separate gain on noise part, separate stretch/shift rules in B8.
**Making it very good:**
- Use **level-independent** sibilance detection (relative band energy, or a learned phoneme/frame classifier) so quiet sibilants are caught. ([US11322170](https://patents.google.com/patent/US11322170))
- Apply gain to the **noise component**, not a frequency band; vowels keep their brightness.
- During pitch changes, keep sibilants **unshifted**; during stretches, stretch them less than vowels.

### 11. Time Tool and time handles
**Back-end processes:** note boundaries and attack/consonant regions (B2, B3, B6), piecewise time-warp maps, note-aware stretching (B8).
**Making it very good:**
- Represent timing as a **piecewise-linear time map** per note with anchors at onset, attack end, (user) handles, release; stretch ratios apply per segment.
- Protect consonants/attacks (ratio ≈ 1) and let vowels absorb change; reset/lock phases at segment boundaries.

### 12. Attack Speed Tool
**Back-end processes:** attack region detection (B2 + energy envelope), local time-warp of the attack segment, transient-preserving resynthesis (B8b).
**Making it very good:** separate attack **duration** (time-warp the transient region) from attack **shape** (amplitude envelope); keep transient phase coherent (Röbel-style transient handling or HPSS split). ([PVDR conclusion](https://arxiv.org/pdf/2202.07382))

### 13. Note Separation Tool
**Back-end processes:** segmentation editing (B3), separation types (hard vs soft boundaries), re-analysis of affected notes.
**Making it very good:** implement **soft separations** as shared transition regions where pitch/amplitude are interpolated between the two notes' edits; hard separations cut with short crossfades.

### 14. Quantize Time macro (and quantize to reference track)
**Back-end processes:** *perceptual* onset per note (B2 refined), beat grid or reference track's onset list (B11), time-warp (B8), alignment of reference notes to target notes.
**Making it very good:**
- Quantize the **perceptual onset**, which for sung syllables often sits at the vowel start rather than the consonant start.
- For reference-track quantizing, match notes with **dynamic time warping** on onset sequences (and pitch where helpful) before moving them, so missing or extra notes don't misalign everything.

### 15. Inspectors
**Back-end processes:** exposure of the note-model parameters; numeric edits feeding the same edit pipeline.
**Making it very good:** use the **same units** as the analysis (cents, dB/LU, ms) and make every value round-trip exactly (edit → resynthesise → re-measure gives the same number).

### 16. Pitch grid, scales, scale detection, Dynamic Just Intonation
**Back-end processes:** tuning tables (B14), key/scale detection (B12), high-resolution pitch histogram for scale extraction, adaptive JI solver (B14), scale-degree mapping for transformations.
**Making it very good:**
- Detect **reference tuning** first, then the scale. ([ISMIR 2006](https://archives.ismir.net/ismir2006/paper/000134.pdf))
- Extract custom scales from **note-level perceived pitches** weighted by note duration, then cluster.
- For dynamic JI, use a solver that yields **tempered compromises** when chords can't be pure and controls drift (Stange et al.; Hermode). ([Stange et al.](https://arxiv.org/pdf/1706.04338))
- Scale transformation = map each note to its **scale degree** in the old scale, then to the same degree in the new scale.

### 17. Chord detection, Chord Grid, Chord Track
**Back-end processes:** chroma/HPCP or transcription-based chord recognition (B12), chord track storage, mapping notes to nearest chord tone, DNA editing (B9/B10) to move notes.
**Making it very good:** use a modern sequence model (BTC/ChordFormer-style), combine with note transcription, and import/export the DAW's chord track over ARA. ([ChordFormer](https://arxiv.org/html/2502.11840v1); [ARA](https://www.celemony.com/en/10-years-of-ara))

### 18. Tempo detection and editing
**Back-end processes:** beat tracking (B11), tempo curve fitting, note-aware time-warping to new tempo (B8).
**Making it very good:** avoid hard tempo priors for rubato material (Beat This!-style models), smooth the curve, and support quick user correction of a few beats with automatic re-solve. ([Beat This!](https://ismir2024program.ismir.net/poster_10.html))

### 19. DNA Direct Note Access (polyphonic editing)
**Back-end processes:** multi-pitch transcription (B9), per-note partial analysis with inharmonicity (B5), per-note separation (B10), per-note resynthesis (B8), residual handling, remix.
**Making it very good:**
- **Transcribe with a neural model, separate with score-informed NMF/harmonic models**, refine each note's partials with DSP. ([Onsets and Frames](https://arxiv.org/pdf/1710.11153); [Driedger et al.](https://www.audiolabs-erlangen.com/resources/2013-ACMMM-AudioDecomp/2013_DriedgerGPEM_SourceSeparationInterface_ACM.pdf))
- Use **spectral smoothness** to split shared harmonics between notes. ([Klapuri](https://archives.ismir.net/ismir2006/paper/000125.pdf))
- **Change-only rendering**: subtract the original contribution of edited notes and add their edited versions, leaving everything else untouched.
- Constrain expectations: dense mixes, distortion and heavy reverb remain hard for every system.

### 20. Audio-to-MIDI
**Back-end processes:** note list (B3/B9), pitch-bend extraction from pitch curves (B1), velocity mapping from note loudness (B13), MIDI writing.
**Making it very good:** export **pitch bend** curves (as Basic Pitch does) for expressive lines, quantize optionally, and map velocity from a perceptual loudness scale calibrated per instrument. ([Basic Pitch](https://github.com/spotify/basic-pitch))

### 21. Copy/paste, harmony creation, recomposition
**Back-end processes:** note objects with source references, pitch shift with formant control (B7/B8), time placement, crossfades.
**Making it very good:** for generated harmonies, apply small **formant, timing and pitch-drift variations** so the copies don't phase or sound cloned; transpose by **scale degree** (diatonic harmony) rather than fixed semitones.

### 22. Multitrack note editing, cross-track macros, quantize to reference (studio)
**Back-end processes:** shared document model across tracks (B15/ARA), cross-track selection and macros, cross-track onset alignment (DTW), vertical intonation (B14).
**Making it very good:** offer intonation targets based on **vertical consonance** between voices (adaptive JI across tracks), not only each track against the grid.

### 23. Sound Editor (studio)
**Back-end processes:** per-note harmonic amplitudes (B5), harmonic-indexed gain (by harmonic number, not Hz), spectral envelope editing (B7), additive resynthesis (B8d), timbre transfer between tracks.
**Making it very good:**
- Implement the core as an **additive harmonic model**: gain per harmonic index *k*, macros for odd/even balance, brightness tilt and fundamental level.
- **Timbre transfer**: copy one source's harmonic distribution or envelope onto another's pitch/loudness. DDSP shows the learned version of exactly this (harmonic + filtered-noise synth driven by pitch and loudness, e.g. voice to violin). ([Magenta DDSP](https://magenta.tensorflow.org/ddsp); [vocal timbre effects with DDSP](https://vbn.aau.dk/files/593954283/DAFx23_paper_29.pdf))
- Keep the **noise part** separate so harmonic edits don't destroy breath or bow noise.

### 24. DAW integration and workflow
**Back-end processes:** ARA 2 plug-in implementation (B15), VST3/AU/AAX wrappers, document persistence, undo history, background analysis.
**Making it very good:** start from the open ARA SDK and its JUCE example; make analysis **incremental and cached** (hash audio regions), and store edits as parameters on top of immutable analysis so undo/selective reset are trivial. ([ARA SDK announcement](https://www.kvraudio.com/news-print.php?id=51704))

---

# Part C – Architecture, Build Order and Quality Measurement

## Suggested build order

1. **Monophonic core first** (covers most of what essential/assistant do): B1 f0 → B2 onsets → B3 segmentation → B4 perceived pitch → B8 resynthesis with B7 envelope preservation. Ship Main Tool, Correct Pitch, Quantize Time.
2. **Expression tools**: pitch-curve decomposition (centre/drift/vibrato), transitions, time handles, attack speed, amplitude, fades, Leveling (B13).
3. **Noise modelling**: B5 + B6 for sibilant/breath control and natural pitch shifts.
4. **Harmony and tempo**: B11, B12, B14, chord track; scale extraction.
5. **Polyphony (hardest)**: B9 transcription → B10 separation → per-note editing.
6. **Studio features**: multitrack model, cross-track macros, Sound Editor (additive editing on B5 data).
7. **Host integration** throughout via ARA (B15).

## Open toolbox (good starting points)

| Need | Options to evaluate |
|---|---|
| Mono f0 | pYIN (C++/Python), CREPE, RMVPE, PESTO, FCPE, WORLD Harvest |
| Onsets | SuperFlux/ComplexFlux, CNN onset detectors (madmom ecosystem; check licences) |
| Note segmentation | pYIN/Tony note HMM; custom HMM/CRF fusing f0, onsets, phonemes |
| Sinusoidal/noise modelling | SMS-style analysis, ATS, WORLD (CheapTrick + D4C), DDSP |
| Envelope | True Envelope / TE-LPC, CheapTrick |
| Stretch/shift | Rubber Band (R3), PGHI implementations, PSOLA, WORLD synthesis, NSF-HiFiGAN family |
| Polyphonic transcription | Onsets and Frames, MT3, Basic Pitch |
| Note separation | Score-informed NMF (FMP/libfmp examples) |
| Beats | Beat This! (MIT weights), madmom (non-commercial parts) |
| Key/chords | Essentia key profiles, BTC, ChordFormer |
| Loudness | Any BS.1770 implementation (adapted per note) |
| Adaptive JI | Stange et al. open-source solver; Hermode (commercial) |
| Host | ARA SDK (Apache 2.0) + JUCE |

Check each project's licence before commercial use; several research models and madmom's DBN restrict commercial use. ([Beat This! model card](https://huggingface.co/cstr/beat-this-GGUF))

## How to measure "very good"

**Analysis accuracy (objective)**
- **Pitch:** Raw Pitch Accuracy and Raw Chroma Accuracy at 50 cents *and* at 10 cents, plus voicing recall/false alarm. Common sets: MIR-1K (singing), MDB-stem-synth (synthetic resynthesised stems with perfect labels), PTDB (speech). ([RMVPE](https://arxiv.org/pdf/2306.15412); [PESTO](https://transactions.ismir.net/articles/10.5334/tismir.251))
- **Notes/transcription:** note F1 with onsets within ±50 ms, and onset+offset F1 (offset within the larger of 50 ms or 20% of note length), per mir_eval defaults. Sets: MAESTRO (piano), GuitarSet, MusicNet, Slakh2100, URMP. ([ISMIR 2022](https://archives.ismir.net/ismir2022/paper/000004.pdf))
- **Onsets:** F-measure with a ±25–50 ms window, including vibrato-heavy test material. ([SuperFlux](https://dafx.de/paper-archive/details/0oee-99Z88WL7pSo749gcA))
- **Beats/chords:** beat F1 plus continuity metrics (CMLt/AMLt); chord WCSR at major/minor and large-vocabulary levels. ([TISMIR](https://transactions.ismir.net/articles/238); [BMACE](https://arxiv.org/pdf/2601.02101))
- **Model fit:** residual energy after sinusoidal resynthesis; envelope fit at low harmonics.

**Processing quality (perceptual)**
- Objective metrics don't capture artifacts well; use **listening tests** (MUSHRA-style or mean opinion score with hidden reference). The PGHI paper's comparative listening test against commercial stretchers is a good template. ([PVDR](https://arxiv.org/pdf/2202.07382))
- Build a **torture-test set**: wide vibrato, fast melismas, breathy and fry voice, high soprano, sibilant-heavy lyrics, ±12 semitone shifts, 0.5×/2× stretches, piano chords with shared harmonics, distorted guitar, reverb tails.
- **Null tests**: with no edits, output should equal input (or be extremely close). Any audible difference means the engine is not transparent.

## Honest limitations of the state of the art

- Polyphonic note separation is still imperfect: shared harmonics, reverb and dense mixes cause bleed between notes. Multi-instrument transcription remains far weaker than piano-only (e.g. onset+offset F1 well below piano levels on mixed datasets). ([ISMIR 2022](https://archives.ismir.net/ismir2022/paper/000004.pdf))
- Neural pitch trackers degrade at very low SNR, and models can look better on datasets they were trained on (FCPE's authors note this about MIR-1K). ([PESTO](https://transactions.ismir.net/articles/10.5334/tismir.251); [FCPE](https://arxiv.org/pdf/2509.15140))
- Beat trackers without tempo priors trade some continuity for flexibility. ([Beat This!](https://ismir2024program.ismir.net/poster_10.html))
- Perceived pitch of complex sung notes is still an open research question; weighting models need calibration against listeners. ([d'Alessandro & Castellengo](https://hal.archives-ouvertes.fr/hal-02008510))

---

## References

Pitch and notes
- CREPE – https://arxiv.org/pdf/1802.06182
- PESTO (TISMIR) – https://transactions.ismir.net/articles/10.5334/tismir.251 ; related work – https://arxiv.org/pdf/2309.02265
- RMVPE – https://arxiv.org/pdf/2306.15412
- FCPE – https://arxiv.org/pdf/2509.15140
- pYIN / Tony – https://code.soundsoftware.ac.uk/projects/pyin
- Phoneme-informed note segmentation – https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2021/LiDemirelProutskovaDixon-NLP4MusA2021.pdf
- Semi-beat-synchronous HMM – https://archives.ismir.net/ismir2016/paper/000004.pdf
- BERT-APC – https://arxiv.org/pdf/2511.20006
- Deep Autotuner – https://arxiv.org/pdf/2002.05511
- Pitch of short vibrato tones – https://hal.archives-ouvertes.fr/hal-02008510
- pyAMPACT – https://arxiv.org/pdf/2412.05436

Onsets, beats, harmony
- SuperFlux – https://dafx.de/paper-archive/details/0oee-99Z88WL7pSo749gcA
- Efficient onset detection (review of CNN methods) – https://arxiv.org/pdf/1806.06773
- Beat This! – https://ismir2024program.ismir.net/poster_10.html ; model card – https://huggingface.co/cstr/beat-this-GGUF
- BTC – https://arxiv.org/pdf/1907.02698
- ChordFormer – https://arxiv.org/html/2502.11840v1
- BMACE (Mamba chords) – https://arxiv.org/pdf/2601.02101
- Essentia key profiles – https://essentia.upf.edu/reference/std_Key.html

Analysis/synthesis
- Sinusoidal peak estimation (QIFFT) – https://ccrma.stanford.edu/%7Ejos/sasp/Optimal_Peak_Finding_Spectrum.html
- QIFFT modulation bias – https://ccrma.stanford.edu/STANM/stanms/stanm116/index.html
- Non-stationary sinusoidal analysis – https://pageperso.univ-lr.fr/sylvain.marchand/Publications/dafx08.pdf
- SMS – https://depts.washington.edu/dxscdoc/Help/Classes/SMS.html
- ATS – https://ccrma.stanford.edu/~juan/ATS_manual.html
- True Envelope (Röbel & Rodet) – https://dafx.de/paper-archive/details/EDSTSaO17Gd_0LV03iwDfw
- TE-LPC – https://hal.archives-ouvertes.fr/hal-01161354
- WORLD – https://r9y9.github.io/WORLD.jl/latest
- Phase Vocoder Done Right (PGHI) – https://arxiv.org/pdf/2202.07382
- PhaVoRIT – https://hci.rwth-aachen.de/PhaVoRIT
- HPSS-based time-scale modification – https://publica.fraunhofer.de/handle/publica/236969
- Rubber Band changelog – https://donkey.abanbytes.eu/david/librubberband/raw/commit/1aa241e6d0119212d10e462f6eb473be80aff042/CHANGELOG
- Source-Filter HiFi-GAN – https://arxiv.org/pdf/2210.15533
- PC-NSF-HiFiGAN – https://github.com/openvpi/vocoders/releases
- DDSP – https://ar5iv.labs.arxiv.org/html/2001.04643

Polyphony
- Klapuri harmonic summation – https://archives.ismir.net/ismir2006/paper/000125.pdf
- Joint multi-F0 evaluation – https://dafx.de/paper-archive/2004/P_234.PDF
- Onsets and Frames – https://arxiv.org/pdf/1710.11153
- MT3 – https://ar5iv.labs.arxiv.org/html/2111.03017
- Basic Pitch – https://github.com/spotify/basic-pitch
- NMF with harmonic constraints (FMP) – https://www.audiolabs-erlangen.de/resources/MIR/FMP/C8/C8S3_NMFSpecFac.html
- Score-informed decomposition for editing – https://www.audiolabs-erlangen.com/resources/2013-ACMMM-AudioDecomp/2013_DriedgerGPEM_SourceSeparationInterface_ACM.pdf

Loudness, tuning, sibilance, integration
- BS.1770 glossary – https://lupy-nocarryr.readthedocs.io/en/latest/glossary.html
- Adaptive JI (Stange et al.) – https://arxiv.org/pdf/1706.04338
- Sethares adaptive tunings – https://minds.wisconsin.edu/bitstream/handle/1793/9494/file_1.pdf?sequence=1
- Level-independent de-esser patent – https://patents.google.com/patent/US11322170
- ARA SDK – https://www.celemony.com/en/10-years-of-ara
