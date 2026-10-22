# How timing markers are found in audio: onset and transient detection, and the C/C++ options

Research and test report, 2026-10-22.

> **For Claude Code.** The project is a C++ vocal-processing project that already has a TD-PSOLA core and pitch-mark sources (SPTK / REAPER / Praat). This report covers how onsets and transients are detected, what current best practice is, and which C/C++ libraries help. Licences are listed as facts only; the project owner is handling licensing. Every claim is tagged **Read** (I read the paper or source), **Ran** (I built and ran it here) or **Not tested**. Everything I ran used synthetic signals with known onset times, not real singing, so treat the test numbers as a sanity check on behaviour and not as accuracy figures.

## Summary

- **Standard method.** Every mainstream detector has three stages: optional pre-processing, a *detection function* (one number per analysis frame that rises when something new starts), and *peak picking* on that function. The detectors differ almost entirely in the middle stage. *Read.*
- **Best practice for an unsupervised detector is SuperFlux** (Böck & Widmer, 2013): log-scaled, band-reduced spectral flux where each band is compared with the *maximum of its neighbours* in an earlier frame. That one trick suppresses vibrato false positives. It is the baseline that 2024 papers still compare against, and Essentia ships it. *Read.*
- **Neural detectors score a few points higher** on mixed music (for example 0.873 vs 0.836 F on the same 321-excerpt set), but they need trained models, and the best-known ones (madmom) ship models under a non-commercial licence. *Read.*
- **Singing is the hard case.** Soft onsets, vibrato, portamento and legato pitch changes that carry no energy change all defeat energy-based methods. The 2023–2024 singing-specific systems use pitch and phoneme information, not only the spectrum. *Read.*
- **My tests (synthetic singing-like signals, ±25 ms).** The Python SuperFlux reference found all 49 amplitude onsets with one false positive at its default settings. aubio's library defaults ranged from 0.36 to 0.75 pooled F-measure, and five of its methods produced 120 or more false positives, inside sustained notes. Details in section 9. *Ran.*
- **Library recommendation.** For a C++ project: **write SuperFlux yourself.** It is about 150 lines on top of any FFT. I did that (`onset_test/cpp/superflux.hpp`, no dependencies) and checked it against the reference: identical detections on 15 of 16 test cases. Use **aubio** (C, GPL-3.0-or-later) as a drop-in multi-method detector and cross-check, **Essentia** (C++, AGPL-3.0-or-later) if you want its SuperFlux and the rest of its toolbox, **QM-DSP** (C++, GPL-2.0-or-later) for the classic detection functions, and **Rubber Band** (C++, GPL-2.0-or-later) as a reference for how a time-stretcher treats transients. *Read, Ran (aubio, own SuperFlux), Not tested (Essentia, QM-DSP, Rubber Band).*

## 1. What a "timing marker" is

Bello et al. (2005), the standard tutorial, separate three things. *Read.*

| Term | Definition (their wording) | Practical meaning |
|---|---|---|
| Attack | "the time interval during which the amplitude envelope increases" | A duration. |
| Transient | "short intervals during which the signal evolves quickly in some nontrivial or relatively unpredictable way" | A duration. They assume the ear cannot separate two transients less than 10 ms apart. |
| Onset | "a single instant chosen to mark the temporally extended transient" | A point in time. This is what detectors output. |

So an onset is a *choice* of one instant inside an interval, and detectors differ in where in the interval they put it. Three further notions matter for vocals and are not the same thing as the onset a detector reports:

- **Perceived beat position (P-center).** For syllables, the moment that listeners hear as "the beat" is tied to the start of the vowel rather than the start of the consonant. A review article (PMC3313478) describes it as linked to a rise of mid-range spectral energy (around 500–1500 Hz, the first formant) and says it is *not* the same as the amplitude-envelope rise time. *Read (secondary source; I did not read the original studies).*
- **Note boundary.** A change of pitch with no change of energy (legato). An energy- or flux-based onset detector may see little there. The singing-transcription literature treats this as a separate, pitch-aware problem. *Read.*
- **Phoneme boundary.** A consonant burst or frication noise before the vowel. A detector can mark the consonant, the vowel, or both (section 9, signal E).

Decide per use which marker you need (consonant start, vowel start, note change) because the same signal gives different answers for each.

## 2. The three-stage pipeline

### 2.1 Pre-processing (optional)

- Split into frequency bands (examples in Bello: Scheirer's six-band filter bank, Klapuri's eight bands, Duxbury's five constant-Q bands). *Read.*
- Separate tonal and transient parts (sinusoidal-model residual, MDCT-based). *Read.*
- **Logarithmic magnitude:** `Xlog = log(λ·X + 1)`, with λ tuned between 0.01 and 20 in Böck et al. 2012. *Read.*
- **Adaptive whitening** (per-bin normalisation by a decaying running maximum; Böck 2012 used m = 10, r = 0.005). Both log magnitude and whitening improved every flux, complex-domain and weighted-phase-deviation detector they tried. *Read.*

### 2.2 Detection functions

`X_k(n)` is the STFT bin `k` of frame `n`, `H(x) = max(x, 0)` is half-wave rectification.

| Function | Definition | Strength (from the sources) | Weakness |
|---|---|---|---|
| Energy / envelope | Frame energy or smoothed rectified signal; usually the first difference of its log | Simple; works for very percussive material | Blind to pitch changes; fooled by tremolo |
| High-frequency content (HFC) | `Σ_k k·\|X_k(n)\|²` | Sharp peaks, precise timing; best on percussive sounds (Bello) | Blind to low-frequency pitched onsets; sensitive to noise floor |
| Spectral flux | `Σ_k H(\|X_k(n)\| − \|X_k(n−1)\|)` | Good general purpose; the basis of SuperFlux | Fires on vibrato and tremolo |
| Spectral difference | Same family, signed or squared variants | Smoother, fewer spurious peaks, less precise timing (Bello) | |
| Phase deviation | Change of the phase increment, `princarg(φ_k(n) − 2φ_k(n−1) + φ_k(n−2))`, spread over bins | Best on pitched sounds (Bello) | Smoother but less precise in time (Bello) |
| Weighted phase deviation | Phase deviation weighted by magnitude (Dixon 2006) | Dixon's improvement on plain phase deviation | |
| Complex domain | `Σ_k \|X_k(n) − X̂_k(n)\|`, `X̂` predicted from magnitude `\|X_k(n−1)\|` and phase `2φ_k(n−1) − φ_k(n−2)` | Uses magnitude and phase together; Bello's "good general choice" | |
| Wavelet / time-frequency | Haar "regularity modulus" etc. | Very precise localisation | Noisier function |
| Statistical | Change-point tests, "surprise" (negative log probability under an ICA model) | In Bello's test the best single operating point: 90.6% TP at 4.7% FP | Needs training data and compute |
| **SuperFlux** | Flux on log, band-reduced magnitudes with a maximum filter across neighbouring bands (below) | Vibrato suppression; unsupervised; works online | Fixed threshold is level dependent (section 9) |
| ComplexFlux / LGD weighting | SuperFlux with a local-group-delay weight that discounts steady tones | Tremolo suppression; on solo opera singing F 0.653 → 0.711, false positives 450 → 221 | Slightly lower on general mixes (0.836 → 0.823) |
| Learned (RNN / CNN) | Network on a log-mel spectrogram with a context window | Highest scores on mixed music | Needs training data; see section 4 |

The strengths and weaknesses in the table come from Bello, Dixon and the SuperFlux papers except where my own test results say otherwise (energy and HFC rows). The phase-deviation and complex-domain formulas are the standard ones, written from my own knowledge; the sources I fetched describe them in words (phase increments; distance to a prediction from the previous frames). *Read.*

Bello's 2005 evaluation (1,065 onsets, ±50 ms): negative log-likelihood (ICA) 90.6% TP / 4.7% FP, HFC 90 / 7, spectral difference 83.0 / 4.1, phase deviation 81.8 / 5.6, wavelet 79.9 / 8.3. Their summary: results "depend strongly on the signal and the database"; use the simplest method that meets the need; phase-based for strongly pitched material, time-domain for very percussive material. Dixon (2006) added weighted phase deviation and a rectified complex difference and concluded that magnitude-, phase- and complex-domain methods "can all achieve similarly high performance". *Read.*

### 2.3 SuperFlux in detail

Steps as described in Böck & Widmer (ISMIR 2013, which restates DAFx-13) and implemented in the CPJKU reference code. *Read, Ran.*

1. Hann-windowed STFT, 2048 samples at 44.1 kHz, 200 frames per second (hop 220.5 samples; the paper says this allows reporting to within 5 ms).
2. Magnitude, then a triangular filterbank on a log frequency scale, quarter-tone spacing. The paper says 138 filters. The reference code (24 bands per octave, 30 Hz to 17 kHz) gave 141 bands in my run at 44.1 kHz.
3. `log10(1·X + 1)`.
4. Maximum filter along frequency: each band takes the maximum of itself and its two neighbours.
5. Compare the current frame with the maximum-filtered frame `µ` frames back. The reference picks `µ` from the window shape (`ratio` 0.5); it comes out at 2 frames here.
6. `SF(n) = Σ_m H(Xlog(n, m) − max over m−1..m+1 of Xlog(n−µ, m))`.

Why the maximum filter works (my paraphrase of the paper's argument): vibrato moves a partial by a fraction of a semitone, so in a quarter-tone filterbank its energy slides into the neighbouring band. Comparing against the neighbours' maximum means that slide is not counted as new energy. The paper reports up to 60% fewer false positives on vibrato-heavy music. *Read.*

### 2.4 Peak picking

A frame `n` is an onset if all three hold (Böck 2012 and the SuperFlux papers):

1. `SF(n)` is the maximum over `[n − pre_max, n + post_max]`.
2. `SF(n) ≥ mean over [n − pre_avg, n + post_avg] + δ`.
3. `n` is more than a minimum distance after the previous onset (the `combine` window).

Parameter sets I read:

| Source | pre_max | post_max | pre_avg | post_avg | combine / min distance |
|---|---|---|---|---|---|
| LGD paper (ISMIR 2013) | 30 ms | 30 ms | 100 ms | 70 ms | 30 ms |
| CPJKU reference code defaults (what I ran) | 10 ms | 50 ms | 150 ms | 0 | 30 ms |
| Essentia `SuperFluxPeaks` / `SuperFluxExtractor` | 30 ms | not exposed | 100 ms | not exposed | 30 ms in the peaks algorithm; 20 ms default in the extractor's docs |
| Böck 2012 online mode | 3 frames (10 ms hop) | 0 | 4–12 frames | 0 | 3 frames |
| Böck 2012 offline mode (pre-windows assumed as online) | 3 frames | 3 frames | 4–12 frames | 1 frame | 0 |

- `δ` is tuned on a dataset; the papers give no single value. The reference code defaults to 1.1 (on log10 magnitudes of full-scale-normalised input), Essentia to 0.05 plus a ratio test (`ratioThreshold` 16).
- Online use (no look-ahead) costs little for the same method: Böck 2012 report that going offline gained the online methods only about 0.25% F on average. They also note detections arrive about one frame early on average and report each one a frame later. *Read.*
- Adaptive thresholds (a local median or mean, not a fixed level) are the norm because music and voice vary in level (Bello). *Read.*
- In the reference code the `combine` argument is documented in seconds but divided by 1000 inside `detect()`, so the CLI default 0.03 ends up as 0.03 ms. The maximum window already suppresses double peaks, so it rarely matters. In my harness I passed 30 (ms), which is what the code expects. *Read.*

## 3. Evaluation conventions (so you can compare numbers)

- A detection is correct if it lies within a tolerance window of an annotated onset, and each annotation can be matched once. MIREX-style windows are ±50 ms; Böck's group uses **±25 ms** and merges annotations closer than 30 ms; Bello used 50 ms; some singing papers use 50 or 100 ms. *Read.*
- Scores at different tolerances are not comparable. A wider window flatters a system, so 80 F at ±50 ms may be worse than 78 F at ±25 ms.
- F-measure alone hides timing bias. Report the signed offset of matched detections (median) too; section 9 does.

## 4. Where the state of the art stands

| Result | Data | Tolerance | F-measure | Note |
|---|---|---|---|---|
| Spectral flux, online | 321 mixed excerpts, 25,966 onsets | ±25 ms | 74.5 | Böck 2012 |
| Log-filtered flux (SuperFlux's precursor), online | same | ±25 ms | 80.3 (P 88.3, R 73.5) | Böck 2012 |
| Neural OnsetDetector.LL (unidirectional), online | same | ±25 ms | 81.7 | Böck 2012 |
| Neural OnsetDetector (bidirectional), offline | same | ±25 ms | 86.6 | Böck 2012 |
| SuperFlux vs neural OnsetDetector | same (25,927 after merging) | ±25 ms | 0.836 vs 0.873 | LGD paper |
| SuperFlux vs neural OnsetDetector on solo opera singing | 1,448 onsets | ±25 ms | 0.653 vs 0.662 (with LGD: 0.711) | LGD paper |
| 5-layer CNN, 81,541 parameters | same 321 excerpts | ±25 ms | 86.52 | Gong et al. 2018; their re-implementation of Schlüter & Böck's CNN scored 86.67 with 289,273 parameters |
| Same CNNs on solo singing (jingju syllables) | 1,203 test syllables | ±25 ms stated for the paper; not checked for this dataset | 74–78 by peak picking (four models); about 83 with score-informed decoding | Gong et al. 2018 |
| Chirp-group-delay detector vs SuperFlux vs CNN | guitar and piano subsets | ±50 ms | 0.889 / 0.884 / 0.874 (IDMT); 0.912 / 0.916 / 0.930 (GuitarSet); 0.856 / 0.855 / 0.909 (MusicNet) | arXiv 2408.13734; I do not know its peer-review status |
| Correntropy detector, singing | 1,567 onsets | ±50 ms | 80.6 | Cited best MIREX 2012 singing result: 55.9 (the authors say not directly comparable) |
| Vocal onsets in polyphonic audio, HMM baseline vs beat-aware | Turkish makam solo-singing excerpts and English pop clips | not found in the part I could read | 35.2 → 38.1–39.5 | Hard problem; Mauch-style note HMM baseline |
| Singing note onsets (COn), phoneme-informed CRNN | Singing datasets (SSVD v2.0 is the primary one) | 50 ms (also 100 ms) | 0.9305 (100 ms: 0.9506); MusicYOLO 0.9176; Omnizart 0.7951; TONY 0.6645 | arXiv 2304.05917 |
| Singing note onsets (COn), ROSVOT | Singing datasets (M4Singer and a new Mandarin set) | 50 ms | 94.0 clean, 93.8 noisy (vs 92.0 / 88.5 prior best) | arXiv 2405.09940 |

All *Read*. Reading across the table:

- On general music, a trained neural detector is ahead of SuperFlux by roughly 3–4 F points at ±25 ms; the simple method is competitive on instruments with vibrato and wins on cost and explainability.
- On solo singing the scores are lower and vary a lot with the data: 0.65–0.71 on opera at ±25 ms, 74–78 on jingju syllables, 80.6 at ±50 ms for the correntropy detector. The 2023–2024 note-onset networks reach the 90s, but they solve a different problem (note-level transcription with pitch, at a 50 ms window, on trained-in singing data), and "note onset" there is not "start of every consonant".
- I did not find 2025–2026 papers on acoustic onset detection that change this picture. My searches were not exhaustive.

## 5. Singing-voice specifics

What is hard, and what the sources say to do about it. All *Read* unless marked.

- **Vibrato.** Plain spectral flux, complex-domain and phase detectors fire repeatedly during vibrato (my test: section 9). SuperFlux's maximum filter fixes the frequency-modulation part; the LGD weighting addresses tremolo (amplitude modulation). My test reproduces both effects (section 9, signals A, D). *Ran.*
- **Soft onsets and portamento.** A slow attack gives a small flux peak. In my signal B (80 ms attacks) HFC, energy and spectral difference found none of the eight onsets, while spectral flux, phase and SuperFlux found all eight. *Ran.*
- **Legato.** A pitch change at constant level has little energy change. In my test SuperFlux caught all 8 pitch changes in the legato signal, and the simple HFC/energy/KL variants caught 2–3 of 8. Real legato is usually smoother than my 20 ms cross-fade, so do not trust that result for portamento. *Ran (synthetic only).*
- **Re-onsets** (same pitch, new syllable or energy). Hard for acoustic detectors; the phoneme-informed network was built for exactly this case. *Read.*
- **Consonant vs vowel.** Consonants are noisy and high in frequency; vowels are periodic and low in frequency. In my test, HFC marked the consonant and mostly missed the following vowel; SuperFlux marked both. Which one the application needs is a design decision (section 1). *Ran.*
- **Pitch-based segmentation as a complement.** pYIN (a Vamp plugin, GPL-2.0) tracks F0 with an HMM and its source contains a `MonoNote` model (`MonoNote.cpp`) for note segmentation. That would give note boundaries independent of the energy-based onsets; I did not run it. *Read.*

Suggested design (my suggestion, **Not tested** as a whole): run an energy/flux detector for *events*, run a voicing/F0 tracker (you already have REAPER and SPTK) for *voiced regions and pitch changes*, then label each event by what is true just before and just after it: unvoiced to voiced (vowel start), noise burst or frication (consonant start), voiced to voiced with a pitch jump (note change). Pick the marker type per use.

## 6. Localisation: how precise is the marker?

- The frame rate bounds the resolution. SuperFlux at 200 fps gives 5 ms steps; aubio's usual 1024/512 setting at 44.1 kHz gives 11.6 ms steps. In my tests on sharp onsets (signal A) the median offset to the true start was −5 ms (SuperFlux offline), +5 ms (online) and between −13 and +3 ms for the aubio variants. *Ran.*
- Offline analysis centres the window on the frame and online analysis ends it at the frame, which probably explains the sign of the SuperFlux offsets (I did not verify that).
- For sample-accurate markers, refine after picking: search a short window around the coarse marker for the sharpest rise of a short-time envelope (or of a band-limited one), or for the first sample where it passes a fraction of its local maximum. **Not tested.** Bello notes HFC and wavelet functions give sharp, precise peaks and smoother functions less so, which suggests using a smooth function for *detection* and a sharp one for *localisation*.
- For PSOLA-style processing the exact position matters less than consistency, as with the pitch marks in the earlier report. A constant bias is harmless; jitter between similar events is not.

## 7. Transients inside time-stretching and pitch-shifting

The same detectors are used in the stretchers, for a different purpose: to stop transients from smearing.

- Röbel (2003) proposed handling attacks per spectral bin in the phase vocoder, so stationary partials nearby are not disturbed, with no limit on the stretch factor; he also introduced a per-bin transient detection criterion that beat a previously published algorithm on a hand-labelled database. *Read (abstract).*
- Rubber Band exposes this as options, which per its header apply to the R2 engine only and have no effect in R3: `OptionTransientsCrisp` (the default; reset component phases at the peak of each transient), `OptionTransientsMixed` (reset outside a frequency range typical of musical fundamental frequencies), `OptionTransientsSmooth` (no reset); and a detector choice `OptionDetectorCompound` (the default), `OptionDetectorPercussive` or `OptionDetectorSoft`. Its R2 engine contains `CompoundAudioCurve`, `PercussiveAudioCurve`, `HighFrequencyAudioCurve` and `SilentAudioCurve`. *Read.*
- Praat's PSOLA copies segments it classifies as voiceless instead of shifting them (previous report). The same idea applies to onsets: **mark a short zone around each consonant or vowel onset as "do not warp"**. That zone then gets a unit time map, which also avoids the repeated or dropped grains described in the vibrato report. **Not tested**, but it follows directly from the earlier analysis.

## 8. C and C++ libraries

Dates are the last commit in a clone I made on 2026-10-22. Licence is what the files say. "Ran" means I built it and used it here.

| Library | Language | Licence | Last commit | Onset-relevant content | Notes |
|---|---|---|---|---|---|
| **aubio** (`aubio/aubio`) | C | GPL-3.0-or-later (file headers) | 2026-04-10 | Onset module with `energy`, `hfc`, `complex`, `phase`, `wphase`, `specdiff`, `kl`, `mkl`, `specflux`; peak picker; spectral descriptors; whitening and log compression options | Built in 3 s with CMake and no optional dependencies (bundles an FFT). Ran. Master reports 0.5.0-alpha; the published docs are 0.4.9. No SuperFlux among its methods. |
| **Essentia** (`MTG/essentia`) | C++ | AGPL-3.0-or-later (`COPYING.txt`); its licensing file lists third-party parts including GPL-licensed FFTW and libsamplerate as dependencies | 2026-10-15 | `OnsetDetection` (`hfc` default, `complex`, `complex_phase`, `flux`, `melflux`, `rms`), `Onsets` (combines several functions with weights), `SuperFluxExtractor`, `SuperFluxNovelty`, `SuperFluxPeaks`, attack descriptors (`LogAttackTime`, `Envelope`) | Not built here: its dependency list is long and pip could not reach the package index. Read only. `SuperFluxNovelty` defaults: binWidth 3, frameWidth 2. |
| **QM-DSP** (`c4dm/qm-dsp`) | C++ | GPL-2.0-or-later (file headers) | 2020-01-28 | `DetectionFunction` with `DF_HFC`, `DF_SPECDIFF`, `DF_PHASEDEV`, `DF_COMPLEXSD`, `DF_BROADBAND` and optional whitening; `PeakPicking` | Library behind the QM Vamp plugins (`c4dm/qm-vamp-plugins`, COPYING is GPL-2.0, last commit 2021-06-02). From the Centre for Digital Music, Queen Mary, per its file headers. Read only. |
| **BTrack** (`adamstark/BTrack`) and **Gist** (`adamstark/Gist`) | C++ | GPL-3.0-or-later | 2025-12-31 and 2021-09-18 | Each has an `OnsetDetectionFunction` class | Aimed at beat tracking and feature extraction, not at picking onsets. Read only. |
| **Rubber Band** (`breakfastquay/rubberband`) | C++ | GPL-2.0-or-later; commercial licence from the authors | 2025-02-27 | Transient detectors inside the stretcher (section 7) | A reference for how a stretcher treats transients, not an onset API. Read only. |
| **pYIN Vamp plugin** (`c4dm/pyin`) | C++ | GPL-2.0 (COPYING; headers not checked) | 2020-02-06 | F0 tracking with HMM plus note segmentation | Pitch-aware note boundaries (section 5). Read only. |
| **madmom** (`CPJKU/madmom`) | Python / Cython | Code BSD; **model and data files CC BY-NC-SA 4.0** | 2024-08-25 | `SpectralOnsetProcessor`, `RNNOnsetProcessor`, `CNNOnsetProcessor`, `OnsetPeakPickingProcessor` | Not usable from C++ directly; useful as the reference for the neural detectors and their peak picking. The non-commercial model licence matters if you ship them. Read only. |
| **CPJKU/SuperFlux** | Python | BSD 2-clause | 2014-11-02 | The reference SuperFlux and ComplexFlux | Python-2 era: needs the three small fixes listed in `onset_test/README.md` under current Python/numpy. Ran with those fixes. |
| **RTNeural** (`jatinchowdhury18/RTNeural`) | C++ | BSD-3-Clause | 2026-08-20 | Inference engine for small networks (Dense, GRU, LSTM, Conv1D, Conv2D, BatchNorm; MaxPooling is unchecked in its README) | A way to run a small trained onset network in C++ in real time. Read only. A model with max-pooling would need another runtime. |

Not checked: ONNX Runtime, libtorch, TensorFlow Lite as ways to run trained detectors; librosa (Python) as a prototyping reference; the many Vamp plugin hosts.

Any of these needs an FFT. aubio bundles one; for your own code any FFT library will do (my `superflux.hpp` includes a small radix-2 FFT so it has no dependencies).

### Recommendation

1. **Primary detector: your own SuperFlux** (`onset_test/cpp/superflux.hpp`). It is the best-supported unsupervised method for voices, it needs no library, and its parameters are the ones in the literature. *Ran.*
2. **Cross-check or fallback: aubio** for a quick second opinion, or when you want several detection functions behind one API. Use `specflux` or `mkl` with tuned thresholds, not the defaults (section 9). *Ran.*
3. **If you can ship an AGPL dependency:** Essentia's `SuperFluxExtractor` is the same algorithm in a maintained C++ library, with other descriptors available. *Read.*
4. **If you need a trained model later:** train or convert your own (the published madmom models carry a non-commercial licence), and run it with a small inference engine such as RTNeural. *Read.*
5. **For labelling events** (consonant / vowel / note change), pair the onset detector with the F0 and voicing tracker you already have. *Not tested.*

## 9. What I ran

### Setup

Seven signal families plus one control, 44.1 kHz mono, generated by `onset_test/gen.py` using the vowel synthesizer from the PSOLA test bench (220 Hz centre, four formants). Ground truth is the start of each amplitude ramp. A tolerance of ±25 ms; each truth onset can match one detection.

| Signal | What it is | True onsets |
|---|---|---|
| A | 8 notes with 5 ms attacks, vibrato ±50 cents at 5.5 Hz, 0.15 s gaps | 8 |
| A0 | Same as A without vibrato (control) | 8 |
| B | Same as A with 80 ms attacks | 8 |
| C | 8 contiguous notes, pitch change every 0.5 s with a 20 ms cross-fade, constant level (legato) | 8 |
| D | One 4 s note with ±100 cents vibrato and 15% tremolo at 6 Hz | 1 |
| E | 8 syllables: voiceless plosive (6 ms burst + 60 ms aspiration) or fricative (150 ms noise) then a vowel | 8 vowel starts, 8 consonant starts |
| F | A plus white noise at −30 dB relative to the signal RMS | 8 |
| G | 16 notes of 110 ms with 15 ms gaps (8 per second) | 16 |

Methods: aubio built from source with library defaults (window 1024, hop 512 unless stated; its default delay compensation applied); the CPJKU SuperFlux reference with its default settings (threshold 1.1, 200 fps, offline and online); my C++ SuperFlux.

### Results at default settings

Cell = true positives / false positives / false negatives. Totals over A, A0, B, D, F, G are 49 true onsets. Full table with offsets is in the appendix.

| Method | A (vibrato) | A0 (no vibrato) | B (soft) | C (legato) | D (vib+trem) | F (noise) | G (fast) |
|---|---|---|---|---|---|---|---|
| aubio `default` (HFC, log compression) | 8/0/0 | 8/0/0 | 0/8/8 | 2/0/6 | 1/0/0 | 0/1/8 | 16/0/0 |
| aubio `complex` | 8/32/0 | 8/19/0 | 6/30/2 | 1/40/7 | 1/37/0 | 0/1/8 | 16/1/0 |
| aubio `specflux` | 8/32/0 | 8/24/0 | 8/24/0 | 7/40/1 | 1/47/0 | 6/1/2 | 12/5/4 |
| aubio `wphase` | 8/22/0 | 8/12/0 | 4/26/4 | 4/30/4 | 1/45/0 | 8/33/0 | 16/0/0 |
| aubio `mkl` | 8/0/0 | 8/0/0 | 1/7/7 | 2/0/6 | 1/6/0 | 0/1/8 | 16/0/0 |
| SuperFlux reference, offline | 8/0/0 | 8/0/0 | 8/0/0 | 8/0/0 | 1/0/0 | 8/1/0 | 16/0/0 |
| SuperFlux reference, online | 8/0/0 | 8/0/0 | 8/0/0 | 8/0/0 | 1/0/0 | 8/1/0 | 16/0/0 |

*Ran.* What it shows:

- **Vibrato.** The `complex`, `specflux`, `phase`, `wphase` and `specdiff` variants raised 22–32 false positives on signal A and 29–47 on D (the single sustained note). On A I checked where they fell: inside the sustained notes, none in the gaps. Without vibrato (A0) `complex` (19), `specflux` (24), `wphase` (12) and `phase` (8) still produced false positives, and `specdiff` almost none (2). I did not investigate why; adaptive whitening and strong log compression are candidates. SuperFlux produced none on A, A0 or D.
- **Soft attacks (B).** HFC, `energy` and `specdiff` found none of eight; `specflux`, `phase` and SuperFlux found all eight.
- **Legato (C).** SuperFlux found 8/8. The HFC, energy, `kl` and `mkl` variants found 2–3 of 8. The cross-fade in my test is abrupt; real portamento would be harder.
- **Noise (F).** With white noise at −30 dB, HFC, `complex`, `kl`, `mkl` and `phase` found nothing (0/1/8); `energy` and SuperFlux were fine. White noise carries far more high-frequency energy than a voice does, so HFC is the natural victim. This is a harsh case; I did not test pink or recorded room noise.
- **Timing bias.** On signal A the median offset to the true start was −5 ms (SuperFlux offline), +5 ms (online), and between −13 and +3 ms for the aubio variants. *Ran.*

### Consonants and vowels (signal E)

Which marker each method places, per syllable, within ±25 ms of the consonant start (C) and the vowel start (V):

| Method | Plosive syllables | Fricative syllables | Other detections |
|---|---|---|---|
| aubio `default` (HFC) | C only in 3 of 4; C and V in 1 | C only, 4 of 4 | 0 |
| aubio `energy` | C and V, 4 of 4 | C and V, 4 of 4 | 1 |
| aubio `kl` | C and V, 4 of 4 | C only, 4 of 4 | 0 |
| aubio `complex` / `specflux` / `phase` | C only, 4 of 4 | C only, 4 of 4 | 32 |
| SuperFlux (offline and online) | C and V, 4 of 4 | C and V, 4 of 4 | 0 |

*Ran.* HFC-style detectors put the marker on the consonant and mostly miss the vowel, which is the wrong marker if you want the perceived beat (section 1). SuperFlux marks both and leaves you to choose.

### Defaults against tuned thresholds

Pooled over A, A0, B, D, F, G (49 true onsets). "Best" is the best single threshold from a coarse grid applied to all six signals together, so it is optimistic: it was tuned on the same signals it is scored on.

| Method | Default F (TP, FP, FN) | Best threshold → F (TP, FP, FN) |
|---|---|---|
| aubio `default` (HFC) | 0.725 (33, 9, 16) | 0.5 → 0.822 (37, 4, 12) |
| aubio `complex` | 0.375 (39, 120, 10) | 1.2 → 0.684 (26, 1, 23) |
| aubio `specflux` | 0.382 (43, 133, 6) | 2.0 → 0.864 (38, 1, 11) |
| aubio `energy` | 0.641 (41, 38, 8) | 1.2 → 0.828 (41, 9, 8) |
| aubio `phase` | 0.381 (40, 121, 9) | 2.0 → 0.667 (26, 3, 23) |
| aubio `wphase` | 0.388 (45, 138, 4) | 1.2 → 0.911 (46, 6, 3) |
| aubio `specdiff` | 0.355 (38, 127, 11) | 0.8 → 0.773 (34, 5, 15) |
| aubio `kl` | 0.747 (34, 8, 15) | 0.15 → 0.747 (34, 8, 15) |
| aubio `mkl` | 0.701 (34, 14, 15) | 0.8 → 0.876 (39, 1, 10) |
| SuperFlux reference, offline | 0.990 (49, 1, 0) | 4.0 → 1.000 (49, 0, 0) |
| SuperFlux reference, online | 0.990 (49, 1, 0) | 0.7 → 0.990 (49, 1, 0) |

*Ran.* Two points. aubio's defaults are not appropriate for sung material, and threshold tuning recovers much of the loss, but the tuned thresholds differ by method and I tuned on the test set. SuperFlux's F was flat (0.98–1.00 offline) for thresholds from 0.5 to 4.0 on this set, but it fell off below 0.5 (0.645 at 0.3), so the usable range is wide but has an edge.

### Level dependence

The same six signals attenuated by 0 to −40 dB, defaults unchanged. Pooled F (TP, FP, FN):

| Level | aubio `default` | aubio `specflux` | aubio `mkl` | aubio `wphase` | SuperFlux offline, threshold 1.1 |
|---|---|---|---|---|---|
| 0 dB (peak −6 dBFS) | 0.73 (33, 9, 16) | 0.38 (43, 133, 6) | 0.70 (34, 14, 15) | 0.39 (45, 138, 4) | 0.99 (49, 1, 0) |
| −10 dB | 0.73 | 0.38 | 0.75 | 0.40 | 0.99 |
| −20 dB | 0.75 | 0.38 | 0.75 | 0.52 | 0.99 (48, 0, 1) |
| −30 dB | 0.68 | 0.40 | 0.76 | 0.63 | 0.91 (41, 0, 8) |
| −40 dB | 0.68 | 0.39 | 0.76 | 0.63 | 0.87 (38, 0, 11) |

*Ran.* SuperFlux with a fixed threshold starts missing onsets at −30 dB because `log10(1·X + 1)` is level dependent. Normalise the input level (or tune `λ`/the threshold) before relying on a fixed value. The adaptive-whitening methods in aubio are more level tolerant but have the false-positive problem above.

### The C++ SuperFlux

`onset_test/cpp/superflux.hpp` is a header-only C++17 implementation with no dependencies (own radix-2 FFT). Written from the paper and checked against the Python reference. *Ran.*

- Detections identical on 15 of 16 signal/mode combinations. The exception is one onset in signal B, online mode, that landed one frame (5 ms) away.
- The detection function matches the reference to about 5e-6 when both read the same 16-bit samples. With float input against the reference's 16-bit input the difference is up to 4e-3 of the peak, which is the input quantisation, not a bug.
- Clean under AddressSanitizer and UBSan on three signals, offline and online.
- About 137 ms wall time per 5.3 s file including process start (unoptimised; the filterbank is applied as a plain loop and the FFT is simple radix-2).

```cpp
#include "superflux.hpp"
superflux::Params p;            // 44.1 kHz, 200 fps, 2048 window, 24 bands/oct, threshold 1.1
p.sample_rate = 44100.0;
p.online = false;               // true: causal window and no look-ahead in peak picking
std::vector<double> onsets = superflux::detect(samples /* float, [-1,1] */, p);
```

### Limits of these tests

- Synthetic only: one vowel model, white-noise consonants, a clean recording chain. No real singing, no rough or breathy voices, no reverb, no accompaniment.
- Ground truth is the start of my amplitude ramp, which is one definition of an onset. For the 80 ms ramps, a listener would place the moment later.
- aubio was run with its library defaults and a 1024/512 window; a finer hop would shrink its timing offsets. Its false-positive behaviour was not diagnosed.
- Essentia, QM-DSP, Rubber Band and the neural detectors were not run.
- The tuned-threshold table is optimistic (tuned on the test set).

## Appendix: full results at default settings (±25 ms)

Cell = TP/FP/FN (median signed offset in ms, matched detections only).

| Method | A | A0 | B | C | D | F | G |
|---|---|---|---|---|---|---|---|
| aubio default 1024/512 | 8/0/0 (−5) | 8/0/0 (−5) | 0/8/8 | 2/0/6 (−12) | 1/0/0 (−2) | 0/1/8 | 16/0/0 (+7) |
| aubio complex 1024/512 | 8/32/0 (−7) | 8/19/0 (−10) | 6/30/2 (+18) | 1/40/7 (−9) | 1/37/0 (−8) | 0/1/8 | 16/1/0 (+2) |
| aubio specflux 1024/512 | 8/32/0 (−13) | 8/24/0 (−13) | 8/24/0 (+6) | 7/40/1 (−20) | 1/47/0 (−16) | 6/1/2 (−11) | 12/5/4 (−10) |
| aubio energy 1024/512 | 8/2/0 (+1) | 8/0/0 (0) | 0/10/8 | 2/2/6 (+5) | 1/23/0 (+9) | 8/3/0 (+1) | 16/0/0 (+10) |
| aubio phase 1024/512 | 8/32/0 (−9) | 8/8/0 (−10) | 8/32/0 (−2) | 2/40/6 (−10) | 1/47/0 (−7) | 0/1/8 | 15/1/1 (−4) |
| aubio wphase 1024/512 | 8/22/0 (−5) | 8/12/0 (−6) | 4/26/4 (+14) | 4/30/4 (−12) | 1/45/0 (−4) | 8/33/0 (−4) | 16/0/0 (−1) |
| aubio specdiff 1024/512 | 8/30/0 (−6) | 8/2/0 (−7) | 0/31/8 | 7/37/1 (−16) | 1/29/0 (−4) | 8/33/0 (−4) | 13/2/3 (0) |
| aubio kl 1024/512 | 8/0/0 (−5) | 8/0/0 (−7) | 1/7/7 (−13) | 2/0/6 (−9) | 1/0/0 (−15) | 0/1/8 | 16/0/0 (+5) |
| aubio mkl 1024/512 | 8/0/0 (−8) | 8/0/0 (−9) | 1/7/7 (0) | 2/0/6 (−13) | 1/6/0 (−13) | 0/1/8 | 16/0/0 (+3) |
| aubio default 1024/256 | 8/0/0 (+3) | 8/0/0 (+3) | 0/8/8 | 3/0/5 (−6) | 1/0/0 (+4) | 0/1/8 | 16/0/0 (+5) |
| aubio complex 1024/256 | 8/31/0 (0) | 8/24/0 (−1) | 7/26/1 (+21) | 8/40/0 (−8) | 1/47/0 (−3) | 0/1/8 | 16/13/0 (+2) |
| SuperFlux ref offline | 8/0/0 (−5) | 8/0/0 (−5) | 8/0/0 (+5) | 8/0/0 (−20) | 1/0/0 (−5) | 8/1/0 (−5) | 16/0/0 (−5) |
| SuperFlux ref online | 8/0/0 (+5) | 8/0/0 (+5) | 8/0/0 (+10) | 8/0/0 (−10) | 1/0/0 (+5) | 8/1/0 (+5) | 16/0/0 (+5) |

In signal C the true note change is at the end of a 20 ms cross-fade, so a detection at the start of the cross-fade reads as −20 ms.

## Sources

Papers and pages I read:

- Bello et al., [A Tutorial on Onset Detection in Music Signals](https://www.cs.bu.edu/fac/snyder/cs583/Literature%20and%20Resources/bello_onset_tutorial.pdf), IEEE TSAP 2005.
- Dixon, [Onset Detection Revisited](https://dafx.de/paper-archive/details/_kscRgr98aSFW4l-j1YJuA), DAFx 2006 (abstract page).
- Böck, Krebs, Schedl, [Evaluating the Online Capabilities of Onset Detection Methods](https://archives.ismir.net/ismir2012/paper/000049.pdf), ISMIR 2012.
- Böck, Widmer, [Maximum Filter Vibrato Suppression for Onset Detection](https://dafx.de/paper-archive/details/0oee-99Z88WL7pSo749gcA), DAFx-13 (abstract page; I read the method as restated in the ISMIR 2013 paper and the reference code).
- Böck, Widmer, [Local group delay based vibrato and tremolo suppression for onset detection](https://archives.ismir.net/ismir2013/paper/000175.pdf), ISMIR 2013.
- Schlüter, Böck, [Improved Musical Onset Detection with Convolutional Neural Networks](https://research.jku.at/en/publications/improved-musical-onset-detection-with-convolutional-neural-networ/), ICASSP 2014 (abstract page only; I did not read the paper or its scores).
- Gong et al., [Towards an efficient deep learning model for musical onset detection](https://arxiv.org/pdf/1806.06773), 2018.
- [Metrical-accent aware vocal onset detection in polyphonic audio](https://ar5iv.arxiv.org/html/1707.06163), 2017.
- [A Phoneme-informed Neural Network Model for Note-level Singing Transcription](https://arxiv.org/pdf/2304.05917), 2023.
- [Robust Singing Voice Transcription Serves Synthesis (ROSVOT)](https://arxiv.org/pdf/2405.09940), 2024.
- [A pairwise approach to simultaneous onset/offset detection for singing voice using correntropy](https://arxiv.org/pdf/1603.06065), 2016.
- [Chirp Group Delay based Onset Detection in Instruments with Fast Attack](https://arxiv.org/pdf/2408.13734), 2024.
- Röbel, [A new approach to transient processing in the phase vocoder](https://dafx.de/paper-archive/details/svqqbE0vLRFuz4BsmGhoDg), DAFx 2003 (abstract page).
- [Perceptual centers article](https://pmc.ncbi.nlm.nih.gov/articles/PMC3313478) (PMC3313478), for the P-center description.
- aubio [specdesc documentation](https://aubio.org/doc/latest/specdesc_8h.html); Essentia [SuperFluxExtractor](https://essentia.upf.edu/reference/std_SuperFluxExtractor.html) and [onset detection tutorial](https://essentia.upf.edu/tutorial_rhythm_onsetdetection.html); madmom [README](https://raw.githubusercontent.com/CPJKU/madmom/main/README.rst) for its licence split.

Source code I cloned and read (dates and licences in section 8): `github.com/aubio/aubio`, `github.com/MTG/essentia`, `github.com/CPJKU/SuperFlux`, `github.com/CPJKU/madmom`, `github.com/c4dm/qm-dsp`, `github.com/c4dm/qm-vamp-plugins`, `github.com/c4dm/pyin`, `github.com/adamstark/BTrack`, `github.com/adamstark/Gist`, `github.com/breakfastquay/rubberband`, `github.com/jatinchowdhury18/RTNeural`.

Test scripts and the C++ SuperFlux: `onset_test/` (zipped as `onset_test.zip`).
