# Singing Vocal Resynthesis: Methods, Hard Sound Classes, and Metrics

Research report, 2026-10-20.

> **For Claude Code:** This is a research brief for building an offline, quality-first resynthesis pipeline for singing vocals that routes each frame to a handling path by sound class. The routing rules and metric definitions are in "Metrics" and "Routing pipeline". Every threshold is a starting value to calibrate per singer (see "Calibrating thresholds"). Where a value is my guess rather than sourced, the Basis column says so. Source URLs are inline and listed at the end.

## Summary

Yes: plosives, sibilants and breaths, breathy phonation, and rough phonation each fail in a different way, so the best approach is to classify every frame and route it to a matching path rather than resynthesize the whole vocal with one method.

- **Methods.** Classical analysis–modify–synthesis (phase vocoder, PSOLA, WORLD/STRAIGHT, harmonic-plus-noise) and neural vocoding (GAN, Fourier, source-filter, DDSP hybrids, diffusion) each bet on how periodic and noise parts split. For pitch edits on singing, hybrids with an explicit F0 and a harmonic-plus-noise excitation fit best.
- **What breaks.** Plosive bursts (5–20 ms) are shorter than analysis frames; sibilants turn into buzz or metallic noise under harmonic models; rough voices confuse F0 trackers; breathy voices need noise that stays put when pitch moves.
- **Metrics.** Thirteen measurements separate the classes: voicing confidence, spectral flux, centroid, zero-crossing rate, HNR, aperiodicity and maximum voiced frequency, CPPS, GNE, H1–H2, jitter, shimmer, SHR, and a two-tracker octave ratio. The thresholds are starting values, and the tables mark which are my guesses.
- **Calibration.** Set the thresholds per singer by round-tripping their own recordings through each candidate path and scoring each class with its own error metric.

## Scope and assumptions

This report treats singing resynthesis as a routing problem: no single model handles vowels, plosives, sibilants and rough or breathy phonation equally well, so each segment is measured and sent to the method that fits it.

Two pipelines are compared, both offline and quality-first:

- **Analysis–modify–synthesis:** phase vocoders, PSOLA, WORLD/STRAIGHT-style vocoders, and harmonic-plus-noise models.
- **Neural vocoding:** GAN vocoders, source-filter neural vocoders, and DDSP-style hybrids that rebuild a waveform from features.

Assumptions I made, which change the advice if wrong:

- The source is a monophonic, close-miked lead vocal with no reverb, doubles or backing vocals. Reverb tails and doubles corrupt pitch tracking and aperiodicity estimates.
- Audio is 44.1 or 48 kHz, and "resynthesis" means re-rendering a recorded take after editing pitch, timing or timbre, not generating singing from text.
- Threshold values given later are starting points to tune on your own material, not universal constants.

## Analysis–modify–synthesis methods

Each classical method bets on one way a vocal splits into parts, and each fails where that bet is wrong: phase vocoders assume stable sinusoids, PSOLA assumes clean periodicity, WORLD and STRAIGHT assume a clean periodic-plus-aperiodic split, and harmonic-plus-noise models assume noise is smooth and shapeable.

| Method | Models the voice as | Works well on | Breaks on |
| --- | --- | --- | --- |
| Phase vocoder (STFT) | Per-bin magnitude plus propagated phase | Sustained vowels, especially with phase locking | Transient smearing and "phasiness" at large stretch factors; phase locking still fails on percussive events ([Phase Vocoder Done Right](https://ar5iv.arxiv.org/html/2202.07382)) |
| Sines + transients + noise phase vocoder | Three layers, each processed differently | Transients are repositioned instead of stretched, sinusoid phases are locked, noise phases are randomized ([Enhanced fuzzy STN decomposition](https://arxiv.org/pdf/2210.14041)) | Quality depends on how cleanly the three layers separate |
| TD-PSOLA | Windowed pitch-period grains placed at pitch marks | Modest pitch and time changes on clean voiced material, with formants preserved ([tdpsola README](https://rust-digger.code-maven.com/crates/tdpsola)) | Not robust to noise, voiced sounds only, needs the F0 in advance (same README); noisy signals get repeated periodically ([US 7,805,295 background](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7805295)); quality depends on accurate pitch marks ([US 9,685,170](https://patents.google.com/patent/US9685170)) |
| WORLD / STRAIGHT | F0, a smooth spectral envelope, and band aperiodicity driving a mixed excitation ([WORLD](https://github.com/mjansche/World), [STRAIGHT mixed excitation](https://arxiv.org/pdf/2001.01000)) | Independent edits of pitch, timbre and noise; used for singing pitch correction ([Singing voice correction](https://arxiv.org/pdf/1711.08600)) | Standard STRAIGHT is tuned for modal voices, and F0 errors in vocal fry also feed into the aperiodicity estimate ([Silén et al.](https://www.isca-archive.org/interspeech_2009/silen09_interspeech.pdf)) |
| Harmonic + noise (HNM/HNS) | Harmonic sinusoids plus autoregressive noise shaped by a time-domain envelope, pitch-synchronous ([Stylianou et al.](https://www.isca-archive.org/eurospeech_1995/stylianou95b_eurospeech.html)) | Pitch and time scaling without explicit vocal-tract estimation | Pitch scaling that only interpolates the original harmonic amplitudes sounds unnatural unless the spectrum is warped with the pitch ([Lehana & Pandey](https://labs.sonicfield.org/library/speech-synthesis-with-pitch-modification-using-harmonic-plus-noise-model)) |

Only the sines-transients-noise design has a dedicated layer for bursts. WORLD and STRAIGHT carry three streams (F0, envelope, aperiodicity), so a plosive burst has to ride on envelope and aperiodicity frames unless you add separate handling. HNM at least modulates its noise with a time-domain envelope, which helps short bursts.

## Neural vocoding methods

Neural vocoders trade the hand-built split of classical methods for learned waveform generation, and the families differ mainly in how much of the periodic/noise structure is given to the network versus left for it to learn.

| Family | How it works | Strength on singing | Documented failure modes |
| --- | --- | --- | --- |
| Time-domain GAN (HiFi-GAN, [BigVGAN](https://arxiv.org/pdf/2206.04658)) | Upsampling network turns a mel spectrogram into a waveform | BigVGAN adds periodic activations and anti-aliasing, and generalizes zero-shot to singing and music although trained on clean speech | Plain HiFi-GAN falls short on high-pitched and out-of-distribution audio, with spectral inconsistencies and checkerboard artifacts ([EVA-GAN](https://arxiv.org/html/2402.00892v1)); its pitch controllability is severely limited ([Source-filter HiFi-GAN](https://awesomepapers.io/speech-audio/papers/yoneyama2022source)) |
| Fourier-based ([Vocos](https://arxiv.org/abs/2306.00814v1)) | Predicts complex STFT coefficients, then inverse STFT | An order of magnitude faster than time-domain GANs and reduces their periodicity problems | Phase prediction stays the hard part ([Revisiting Vocos](https://audiolabs-erlangen.de/content/resources/NLUI/00_2026-IWAENC-vocoder/main.pdf)) |
| Neural source-filter ([h-NSF](https://arxiv.org/pdf/1908.10256), [uSFGAN](https://arxiv.org/abs/2205.06053v1)) | F0-driven sine excitation filtered by a network, with separate harmonic and noise branches | Explicit F0 input gives direct pitch control; h-NSF learns a time-varying maximum voiced frequency that splits periodic from aperiodic bands | The first uSFGAN modeled aperiodic excitation insufficiently; a harmonic-plus-noise excitation network was added to fix it |
| DDSP and hybrids ([SawSing](https://arxiv.org/pdf/2208.04756), [VISinger 2](https://arxiv.org/pdf/2211.02903)) | Harmonic oscillators plus filtered noise, with a network supplying the control signals | Light and trainable on limited singer data | SawSing buzzes on breathy passages and unvoiced /s/ and /t/, because its harmonic path models transients as harmonics; VISinger 2 reports audible glitches when harmonic components change suddenly, and fixes them by conditioning HiFi-GAN on the DSP waveform |
| Diffusion ([PriorGrad, hierarchical](https://arxiv.org/pdf/2210.07508), [PeriodGrad](https://www.themoonlight.io/de/review/periodgrad-towards-pitch-controllable-neural-vocoder-based-on-a-diffusion-probabilistic-model)) | Iterative denoising conditioned on mel features | A hierarchical variant targets singing by modeling low-frequency pitch first, then higher sampling rates | Standard DDPM vocoders struggle with flexible pitch control; PeriodGrad adds explicit periodic signals to fix this |

Two patterns matter for resynthesis. First, the hybrids that give the network an explicit F0 and a harmonic-plus-noise excitation (h-NSF, uSFGAN, VISinger 2) are the ones built for pitch edits, while mel-only vocoders must infer pitch from timbre. Second, the weak spot repeats across families: short noisy events (breaths, /s/, /t/) are where harmonic assumptions or frame resolution break down.

## Which sound classes need different handling

Yes: plosives, sibilants, breaths, breathy phonation and rough phonation each need a path that differs from a modal vowel, and recording noise needs a step before any of them.

| Class | Path | Defining property | What fails | Preferred handling | Key metrics |
| --- | --- | --- | --- | --- | --- |
| Recording noise | Pre-step: denoise | Present in pauses, not gated by voicing | Inflates aperiodicity and jitter estimates | Remove it, or estimate the floor from pauses, before analysis | Noise floor in pauses |
| Plosive burst | Protect and splice | 5–20 ms broadband transient after a closure | Phase vocoders smear it; 10–25 ms vocoder frames smear it; WORLD has no transient stream; harmonic branches buzz | Protected window of original samples or a sines-transients-noise split; stretch vowels only | Spectral flux, closure dip, high-frequency rise |
| Sibilant or fricative | Noise bypass | Pitchless turbulence noise, energy above about 4 kHz | PSOLA and harmonic paths buzz; vocoders add hiss or metallic noise | Skip pitch and formant shifts; stretch as noise; check 4–16 kHz for tonal peaks | Voicing, centroid, zero-crossing rate |
| Breath | Noise bypass | Broadband noise, weak or no harmonics | Harmonic branches invent harmonics | Skip pitch processing | Voicing, HNR, centroid |
| Breathy phonation | Harmonic and noise split | Harmonics only up to the maximum voiced frequency, noise above, steep tilt | Harmonic-only synthesis buzzes; noise moves with pitch | Harmonics below MVF, noise pinned to the original envelope above it, period-synchronous noise modulation | MVF, HNR, CPPS, GNE, H1–H2 |
| Rough, fry or growl | Texture-preserving | Irregular or subharmonic vibration, octave-ambiguous F0 | Tracker octave errors; aperiodicity corrupted; PSOLA pitch marks fail | Two trackers, keep the texture, re-apply perturbation, noise-conditioned neural vocoder or bypass | Octave ratio, SHR, jitter, shimmer, HNR |
| Modal vowel | Standard | Stable harmonics, low noise | Little; neural vocoders can add pitch shake on vibrato | WORLD or HNM, or a neural vocoder with explicit F0 | Voicing, high HNR, low jitter and shimmer |

## Consonants and plosives

Plosives fail because the burst is shorter than the analysis window: bursts last about 5–20 ms, while neural vocoders typically use 10–25 ms windows with 5–10 ms hops, so stops often come out smeared or muffled ([Phonetically explainable deepfake detection](https://arxiv.org/pdf/2606.15454)).

What goes wrong, by method:

- **Phase vocoder:** transients smear, and phase locking at transients only works if the transient is detected correctly ([Phase Vocoder Done Right](https://ar5iv.arxiv.org/html/2202.07382)).
- **Time stretching:** burst length ranges from 10 to 50 ms, and its character may not survive time-scale changes ([Keshet et al.](https://keshet.net.technion.ac.il/files/2022/05/KeshetChBo01.pdf)).
- **PSOLA:** a transient grain must not be repeated when stretching or dropped when compressing ([DSPRelated](https://www.dsprelated.com/dspbooks/sasp/Time_Scale_Modification.html)).
- **Harmonic paths in hybrids:** SawSing modeled transients as harmonic signal and buzzed on /t/ ([SawSing](https://arxiv.org/pdf/2208.04756)).

Recommended handling. This is my synthesis of the findings above, not a single published recipe, and the millisecond values are starting points:

1. Detect the closure and burst with spectral flux plus a high-frequency energy rise (see Metrics).
2. Protect a window from the end of the closure through the voice onset: burst, aspiration, and the first pitch periods. Apply no time stretch or pitch shift inside it.
3. Fill the window with the original samples, or use a sines-transients-noise split that repositions the transient intact ([Enhanced fuzzy STN decomposition](https://arxiv.org/pdf/2210.14041)). Crossfade 2–5 ms into the resynthesized vowel, aligned to a pitch period.
4. Put any time stretch into the vowel, never the consonant, so the consonant-to-vowel timing stays natural.
5. In a neural pipeline, run burst regions at a 5 ms hop or finer, or splice the original burst back after vocoding, and listen for energy before the burst onset (pre-echo).

Voiced stops (/b/, /d/, /g/) keep their prevoicing in the normal voiced path; only the burst is protected.

## Sibilance and fricatives

Sibilants are pitchless turbulence noise, so they should skip pitch and formant processing and keep their high-frequency spectral shape; harmonic and pitch-synchronous methods turn them into buzz.

What the sources show:

- A sibilant waveform fluctuates randomly because turbulence generates it, and its useful features are centroid frequency and degree of sibilance ([sibilant spectral estimation](https://pmc.ncbi.nlm.nih.gov/articles/PMC4376757)).
- One study's table (audio down-sampled to 24 kHz) gives /s/ spectral means near 8.7 kHz for female and 7.3 kHz for male talkers, and /ʃ/ near 5.4 and 4.5 kHz ([sibilant moments table](https://pmc.ncbi.nlm.nih.gov/articles/PMC7242990/table/T2)). I could not open that page to confirm who was recorded, and singers will differ, so measure your own.
- Singing is recorded at 24–48 kHz with rich high-frequency content, and speech vocoders produce hiss or metallic noise there ([SingGAN](https://arxiv.org/pdf/2110.07468)); GAN vocoders can also add aliasing and imaging artifacts ([Avocodo](https://arxiv.org/pdf/2206.13404v2)).
- PSOLA repeats noisy signals periodically, which sounds buzzy ([US 7,805,295](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7805295)), and engineers advise avoiding pitch processing on consonants and breaths ([Sound on Sound](https://www.soundonsound.com/techniques/vocal-editing-pitch-time?amp)).

Recommended handling (my synthesis; thresholds in Metrics):

1. Classify a frame as fricative when it is unvoiced and high-frequency dominated.
2. Bypass pitch and formant shifting. If timing is unchanged, keep the original samples.
3. If the segment must be stretched, stretch it as noise: randomize phases and crossfade, never phase-lock.
4. Treat voiced fricatives (/z/, /v/) as mixed excitation: harmonics below the maximum voiced frequency, noise above (see Breathy vocals).
5. Check the output between 4 and 16 kHz for tonal peaks, which signal buzz or metallic artifacts.
6. De-ess after resynthesis, not before. De-essing first changes the spectral envelope the model analyzes.

## Noisy, rough, growled and fry vocals

"Noisy" means two things that need opposite treatment: noise in the recording is removed before analysis, while noise in the voice (rasp, growl, fry) is preserved through resynthesis. Recording noise sits in the pauses and is not gated by phonation; vocal noise follows the voicing. Recording noise also inflates aperiodicity and jitter measures, so estimate or remove it first. Praat's manual notes the MDVP jitter thresholds were based on noise-influenced measurements ([Praat jitter](https://www.fon.hum.uva.nl/praat/manual/Voice_2__Jitter.html)).

Rough phonation breaks analysis in four ways:

- **Ambiguous F0.** Creaky voice has several subtypes (low F0, irregular F0, constricted glottis), and no single acoustic measure identifies all of them ([Keating et al.](https://pages.ucsd.edu/~mgarellek/files/Keating_etal_ASA_2023_Spring.pdf)). On disordered sustained vowels, subharmonic errors dominated an autocorrelation tracker's mistakes; CREPE cut them by 94.3% relative to it but introduced a few frequency-doubling errors ([Ikuma](https://www.medschool.lsuhsc.edu/otorhinolaryngology/docs/Ikuma_ICVPB2024_Abstract_ML_Pitch.pdf)).
- **Real octave jumps.** In creaky voice an octave jump can be a genuine change in vibration, not a tracker error ([Watkins et al.](https://www.fon.hum.uva.nl/paul/papers/2024-intsp-WatkinsBoersmaHamann.pdf)).
- **Modal-voice assumptions.** Standard STRAIGHT is tuned for modal voices, and F0 errors in fry spill into the aperiodicity estimate ([Silén et al.](https://www.isca-archive.org/interspeech_2009/silen09_interspeech.pdf)); TD-PSOLA needs accurate pitch marks.
- **Subharmonics.** Growl comes from co-oscillation of the vocal folds with other structures such as the false folds or epiglottis, which modulates the glottal signal and creates subharmonics with integer partials ([Fuks](https://compmus.ime.usp.br/sbcm/1999/papers/Leonardo_Fuks.html)). Extreme vocals show low harmonicity and high roughness ([Czedik-Eysenberg et al.](https://pure.hud.ac.uk/en/publications/towards-an-acoustic-semantic-space-of-extreme-metal-vocal-styles/)).

Recommended handling (my synthesis):

1. Track F0 with two independent trackers and flag frames where their ratio is near 2. A codec fry detector used a 1.74–2.3 window against the long-term average pitch ([US 9,047,865](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9047865)).
2. For each flagged segment, decide whether the target is the perceived pitch or the vibration rate. Do not force a harmonic comb onto a subharmonic voice.
3. For growl and heavy fry, keep the original texture: bypass, or use waveform-domain methods with small shifts only.
4. Separate the slow contour (note plus vibrato) from fast cycle-to-cycle perturbation, and re-apply the original perturbation after pitch edits. One harmonic-plus-noise singing system kept the input's fluctuations for naturalness ([Rajan](https://idr.nitk.ac.in/jspui/handle/123456789/7011)).
5. Route rough but periodic material to a neural vocoder with explicit noise conditioning (h-NSF, or a VISinger 2-style noise branch), not a harmonic-only synthesizer.
6. Do not low-pass pitch contours to "clean" them; that removes the rasp.

## Breathy vocals

Breathiness is aspiration noise on a weakened harmonic structure, so resynthesize the periodic and noise parts separately and keep the noise in place when pitch moves.

What the sources show:

- Rising aspiration noise level and a steeper spectral slope are the main perceptual cues ([PMC review](https://pmc.ncbi.nlm.nih.gov/articles/PMC2891879), [formant-aware analysis](https://repository.lsu.edu/comd_pubs/87)).
- In the example vowel of the [lecture notes from LMU Munich](https://www.phonetik.uni-muenchen.de/~hoole/kurse/expphon3/voicequality/voicequality_notes.pdf), harmonics reached 4 kHz when modal but only about 1 kHz when breathy. The maximum voiced frequency therefore varies, and h-NSF learns it as a trainable, time-varying cutoff ([h-NSF](https://arxiv.org/pdf/1908.10256)).
- Classic singing synthesis models breath as high-frequency noise made by phase randomization, and warns that parameters need careful adjustment to avoid an unnaturally noisy result ([US 6,304,846](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6304846)).
- STRAIGHT's mixed excitation controls both the noise level and the temporal envelope of the noise ([Kawahara et al.](https://www.isca-archive.org/maveba_2001/kawahara01_maveba.html)).
- Failures: SawSing buzzes on breathy passages, and speech vocoders reconstruct singing's high frequencies poorly ([SawSing](https://arxiv.org/pdf/2208.04756), [SingGAN](https://arxiv.org/pdf/2110.07468)).

Recommended handling (my synthesis):

1. Estimate the maximum voiced frequency per frame from band aperiodicity. Synthesize harmonics only below it and noise above it.
2. When shifting pitch, move the harmonic positions but keep the noise filtered by the original, pitch-independent spectral envelope.
3. Re-impose the noise's pitch-period amplitude modulation on the new period grid. HNM and STRAIGHT both carry this time-domain envelope.
4. Treat pure breaths (inhale, exhale) like fricatives: bypass pitch processing.
5. Keep the full 44.1–48 kHz bandwidth and check the 6–16 kHz noise floor against the original; add noise only where the original had it.

## Metrics: what to measure and starting thresholds

Thirteen measurements separate the sound classes. Every threshold is a starting value to calibrate on the singer's own recordings, and the Basis column marks which values come from a source and which are my guesses.

### Segment classification (every 2–5 ms frame)

| Metric | Detects | Computation | Starting threshold | Basis |
| --- | --- | --- | --- | --- |
| Voicing confidence | Voiced vs unvoiced | pYIN voiced probability or CREPE confidence per frame | Voiced at 0.5 or above | CREPE confidence of 0.5 was used as a voicing decision ([Kroon et al.](https://arxiv.org/pdf/2206.14357)); a codec fry detector treated frames below 0.2 as unvoiced ([US 9,047,865](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9047865)) |
| Positive spectral flux | Plosive bursts | Formula below; 5–10 ms window, 2–5 ms hop; adaptive threshold from a moving median plus an offset, with a minimum gap between onsets | Median plus 3 median absolute deviations; minimum gap 30 ms; confirm with an energy dip before the peak (closure) and a high-frequency energy rise | Adaptive median threshold and minimum interval: [Audio AI guide](https://aiunderstanding.org/learn/onset-detection-in-audio); the numbers are my guesses |
| Spectral centroid | Sibilants and fricatives | Formula below | Fricative if unvoiced and centroid above about 4 kHz; tune between your singer's /ʃ/ mean and the highest vowel centroid | One study's /ʃ/ means of 4.5–5.4 kHz ([moments table](https://pmc.ncbi.nlm.nih.gov/articles/PMC7242990/table/T2)); the cut-off is my guess |
| Zero-crossing rate | High-frequency noise | Formula below; a narrowband signal gives about 2f/fs crossings per sample | Above about 0.2 per sample at 44.1 kHz, equivalent to roughly 4.4 kHz | Derived from the narrowband approximation; my guess |

### Phonation quality (voiced segments, 20–50 ms windows)

| Metric | Detects | Computation | Starting threshold | Basis |
| --- | --- | --- | --- | --- |
| HNR | Noise vs harmonics | Formula below | Flag below 10 dB as noisy, breathy or rough; below 0 dB is noise-dominated | 0 dB means equal harmonic and noise energy ([LMU notes](https://www.phonetik.uni-muenchen.de/~hoole/kurse/expphon3/voicequality/voicequality_notes.pdf)); healthy sustained /a/ averaged about 20 dB in a 1,000-speaker study ([BLDE](https://doaj.org/article/daa48a766a444357b93ddf287ef2ea22)); the 10 dB cut-off is my guess |
| Band aperiodicity and maximum voiced frequency (MVF) | Where harmonics stop | D4C aperiodicity per band (0 is periodic, 1 is noise); MVF is the lowest band whose aperiodicity exceeds 0.5 | MVF below 2 kHz selects the breathy path; mean aperiodicity above 0.5 across 0–2 kHz selects the rough or noisy path | STRAIGHT uses five bands (0–1, 1–2, 2–4, 4–6, 6–8 kHz) ([STRAIGHT mixed excitation](https://arxiv.org/pdf/2001.01000)); the values are my guesses |
| CPPS | Breathiness and noise | Cepstral peak height above its regression line in the F0 quefrency range, smoothed over time and quefrency | Lower means noisier; flag the lowest 15% of the singer's own vowel frames | CPPS and GNE were the most valid breathiness measures in [VOXplot](https://elib.uni-stuttgart.de/handle/11682/13694); the percentile is my guess |
| GNE | Turbulence noise vs vocal-fold vibration | Hilbert-envelope correlation across bands; a 1000 Hz bandwidth with 300 Hz shift discriminated well ([GNE study](https://core.ac.uk/works/5169756)) | Below 0.5 suggests added noise | Cut-off from a [tool's documentation](https://glama.ai/mcp/servers/Vocametrix/vocametrix-mcp/tools/vocametrix_calculate_gne), not a study |
| H1–H2 | Open glottis | Formula below; use the formant-corrected H1\*–H2\* where possible | Modal is slightly negative; breathy is above about 10 dB | LMU notes (one speaker's example); it correlated only weakly with breathiness ratings in [one study](https://repositorio.ufpb.br/jspui/handle/123456789/39253), so use it as a secondary cue |
| Jitter (local) | Period irregularity | Formula below; sustained vowels only; high-pass the period sequence above about 10 Hz first, because vibrato runs about 5–7 Hz ([Louder](https://loudersound.com/news/scientists-break-down-freddie-mercury-s-vocal-skills)) | Clinical reference 1.04%; Praat says the correct threshold is probably lower | [Praat jitter](https://www.fon.hum.uva.nl/praat/manual/Voice_2__Jitter.html); the 10 Hz cut is my guess |
| Shimmer (local) | Amplitude irregularity | Formula below; same conditions as jitter | 3.81%, or 0.35 dB | [Praat shimmer](https://www.fon.hum.uva.nl/praat/manual/Voice_3__Shimmer.html) |
| Subharmonic-to-harmonic ratio (SHR) | Fry and growl | Formula below | Flag frames above the singer's 90th percentile | High SHR is a known fry correlate ([Keating et al.](https://pages.ucsd.edu/~mgarellek/files/Keating_etal_ASA_2023_Spring.pdf)); the percentile is my guess |
| Two-tracker octave ratio | F0 octave errors | Ratio of F0 from two independent trackers | Flag a ratio between 1.74 and 2.3, or its inverse | Window from [US 9,047,865](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9047865) |

### Formulas

Positive spectral flux, with X(t,k) the STFT magnitude at frame t and bin k:

```latex
SF(t) = \sum_{k} \max\bigl(0,\ |X(t,k)| - |X(t-1,k)|\bigr)
```

Spectral centroid, with f_k the frequency of bin k:

```latex
C(t) = \frac{\sum_k f_k\,|X(t,k)|}{\sum_k |X(t,k)|}
```

Zero-crossing rate over N samples:

```latex
Z = \frac{1}{N-1}\sum_{n=1}^{N-1} \mathbb{1}\{x[n]\,x[n-1] < 0\}
```

HNR from the normalized autocorrelation r at the pitch lag, the standard autocorrelation definition:

```latex
\mathrm{HNR} = 10\log_{10}\frac{r(\tau_0)}{1 - r(\tau_0)}
```

Local jitter over period lengths T_i, and local shimmer over peak amplitudes A_i (Praat's definitions):

```latex
\mathrm{Jitter} = \frac{\frac{1}{N-1}\sum_{i=1}^{N-1}|T_i - T_{i+1}|}{\frac{1}{N}\sum_{i=1}^{N} T_i}, \qquad \mathrm{Shimmer} = \frac{\frac{1}{N-1}\sum_{i=1}^{N-1}|A_i - A_{i+1}|}{\frac{1}{N}\sum_{i=1}^{N} A_i}
```

First-minus-second harmonic level, and a common definition of the subharmonic-to-harmonic ratio:

```latex
H_1 - H_2 = 20\log_{10}|X(f_0)| - 20\log_{10}|X(2f_0)|, \qquad \mathrm{SHR} = \frac{\sum_k A\bigl((k-\tfrac{1}{2})f_0\bigr)}{\sum_k A(k f_0)}
```

Cepstral peak prominence, with c(q) the cepstrum and a q + b the regression line fitted over the F0 quefrency range:

```latex
\mathrm{CPP} = c(q^*) - (a\,q^* + b), \quad q^* = \arg\max_{q}\, c(q)
```

## Routing pipeline

Run the tests in order on every frame and send it down the first path that matches; modal vowels are the default. The first test that matches decides the path; a frame that matches none is treated as a modal vowel.

| Order | Test (thresholds from Metrics) | Path | Handling |
| --- | --- | --- | --- |
| 1 | Spectral flux peak after a closure dip | Plosive burst | Protect window; original samples or sines-transients-noise split |
| 2 | Voicing under 0.5 and centroid above 4 kHz | Fricative or sibilant | No pitch or formant shift; stretch as noise |
| 3 | Voicing under 0.5 | Breath or other noise | Bypass pitch processing |
| 4 | Octave-ratio flag or high SHR | Rough, fry or growl | Keep texture; bypass or noise-aware vocoder |
| 5 | MVF under 2 kHz or HNR under 10 dB | Breathy phonation | Harmonics below MVF, pinned noise above |
| 6 | None of the above | Modal vowel (default) | Explicit-F0 resynthesis: WORLD or h-NSF-style |

The same logic as pseudocode:

```text
for each frame (2–5 ms hop):
    if flux_peak_after_closure_dip:          path = PLOSIVE_BURST
    elif voicing < 0.5 and centroid > 4 kHz: path = FRICATIVE
    elif voicing < 0.5:                      path = BREATH_NOISE
    elif octave_flag or shr_high:            path = ROUGH
    elif mvf < 2 kHz or hnr < 10 dB:         path = BREATHY
    else:                                    path = MODAL
```

Rules around the tests (my recommendations, not from a single source):

- Run the burst test first, so voiced stops (/b/, /d/, /g/) are caught before the voiced path swallows them.
- Smooth class labels with a 3–5 frame median filter and a minimum segment length of about 20 ms, except for bursts, which are shorter.
- Crossfade 2–5 ms at every class boundary, aligned to a pitch period when the next segment is voiced.
- Log the metric values for every segment; they feed the calibration loop in the next section.

## Calibrating thresholds and checking results

Set the thresholds per singer by round-tripping their own recordings: resynthesize each class through each candidate path, score it with that class's own error metric, and keep the threshold where routing lowers the error. This procedure is my synthesis; no single published study prescribes it.

1. **Label ground truth.** Hand-label a few dozen segments per class from the singer's material (plosive, sibilant, breath, breathy, rough, modal).
2. **Run candidates.** Send each segment through every candidate path (for example WORLD, HNM, a neural vocoder, original bypass) with a null edit, then with typical edits such as ±2 semitones and ±20% duration.
3. **Score each class with its own metric** (table below). A null-edit round trip should return close to the original, so a path that fails it is wrong for that class.
4. **Sweep each threshold** and pick the value that best matches the labels, then read the confusion matrix for classes that bleed into each other (breathy vs rough is the likeliest pair).
5. **Listen.** Run ABX or MUSHRA-style checks on the worst segment per class, because objective scores miss buzz and smear.

| Class | Error metric | How to compute | Starting tolerance (my guess) |
| --- | --- | --- | --- |
| Plosive | Onset time, burst level, pre-burst energy | Run the same spectral-flux detector on the original and the resynthesis; compare peak time, peak level, and energy in the 10 ms before the burst | Onset within 2 ms; level within 3 dB; pre-burst energy within 10 dB of the original |
| Sibilant | Centroid, high-frequency spectral distance, tonality | Compare centroid and log-spectral distance above 4 kHz; compare spectral flatness between 4 and 16 kHz, where a drop signals tonal buzz | Centroid within 5%; flatness within 10% of the original |
| Breathy | HNR, MVF, CPPS | Compute each metric on original and resynthesis and take the difference | HNR within 3 dB; MVF within 500 Hz; CPPS within 1 dB |
| Rough, fry, growl | Octave-error rate, jitter and shimmer ratio, SHR | Count frames where F0 differs by about an octave; divide resynthesis jitter and shimmer by the original's | Octave errors under 2% of frames; jitter and shimmer ratios between 0.7 and 1.3 |
| Modal | F0 error, mel-cepstral distortion, multi-resolution STFT distance | Compare F0 in cents on a null-edit round trip; compute standard spectral distances | F0 error under 10 cents |

## Caveats and open questions

Most numeric thresholds here are starting guesses, and no study I found tests a per-segment routing pipeline of exactly this kind, so the handling steps are my synthesis of method-level findings.

- **Speech and clinical norms.** The jitter, shimmer and HNR reference values come from speech and voice-disorder work, and the /s/ and /ʃ/ means come from one study of unconfirmed population. Singing, and growl in particular, will exceed them.
- **Source quality.** Several citations are patents, lecture notes, a tool's documentation, or conference abstracts. They describe designs or claims, not independent benchmarks.
- **How I read sources.** I opened three pages in full (the LMU notes, the phonetic vocoder-failure paper, and the Ikuma abstract). For the rest I worked from the excerpt search returned, and the sibilant moments table was blocked, so its figures are unconfirmed.
- **Vocoder results transfer unevenly.** Many vocoder findings come from speech-trained systems or one or two singers. Test any candidate on your own recordings before trusting a published ranking.
- **Out of scope.** Polyphonic material, reverb-heavy vocals, and text-to-singing generation.

Three things would change the thresholds: the singing style (growl-heavy or clean), whether the vocal is dry, and whether you need the edit to be inaudible or can accept a creative effect.

## Sources

As of 20 October 2026. Pages marked (full) were opened in full; for the rest I read the excerpt returned by search.

### Classical methods

- [Phase Vocoder Done Right](https://ar5iv.arxiv.org/html/2202.07382): transient smearing and phase locking
- [Enhanced fuzzy decomposition into sines, transients and noise](https://arxiv.org/pdf/2210.14041): repositioning transients when stretching
- [DSPRelated, Time Scale Modification](https://www.dsprelated.com/dspbooks/sasp/Time_Scale_Modification.html): transient frames in OLA stretching
- [tdpsola README](https://rust-digger.code-maven.com/crates/tdpsola): TD-PSOLA strengths and limits
- [US 7,805,295](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7805295) and [US 9,685,170](https://patents.google.com/patent/US9685170): PSOLA on noise; pitch-mark accuracy
- [WORLD repository](https://github.com/mjansche/World), [STRAIGHT mixed excitation](https://arxiv.org/pdf/2001.01000), [Singing voice correction with WORLD](https://arxiv.org/pdf/1711.08600): F0, envelope and aperiodicity streams
- [Silén et al. 2009](https://www.isca-archive.org/interspeech_2009/silen09_interspeech.pdf), [Kawahara et al. 2001](https://www.isca-archive.org/maveba_2001/kawahara01_maveba.html): STRAIGHT on non-modal voices; aperiodicity control
- [Stylianou et al. 1995](https://www.isca-archive.org/eurospeech_1995/stylianou95b_eurospeech.html), [Lehana & Pandey](https://labs.sonicfield.org/library/speech-synthesis-with-pitch-modification-using-harmonic-plus-noise-model), [Rajan 2018](https://idr.nitk.ac.in/jspui/handle/123456789/7011): harmonic-plus-noise modeling and singing use

### Neural vocoders

- [BigVGAN](https://arxiv.org/pdf/2206.04658), [EVA-GAN](https://arxiv.org/html/2402.00892v1), [Source-filter HiFi-GAN](https://awesomepapers.io/speech-audio/papers/yoneyama2022source), [Avocodo](https://arxiv.org/pdf/2206.13404v2), [SingGAN](https://arxiv.org/pdf/2110.07468): GAN vocoders on singing and high frequencies
- [Vocos](https://arxiv.org/abs/2306.00814v1), [Revisiting Vocos](https://audiolabs-erlangen.de/content/resources/NLUI/00_2026-IWAENC-vocoder/main.pdf): Fourier-based vocoding and phase
- [h-NSF](https://arxiv.org/pdf/1908.10256), [uSFGAN with harmonic-plus-noise excitation](https://arxiv.org/abs/2205.06053v1): neural source-filter models
- [SawSing](https://arxiv.org/pdf/2208.04756), [VISinger 2](https://arxiv.org/pdf/2211.02903): DDSP-style singing vocoders and their artifacts
- [Hierarchical diffusion for singing](https://arxiv.org/pdf/2210.07508), [PeriodGrad review](https://www.themoonlight.io/de/review/periodgrad-towards-pitch-controllable-neural-vocoder-based-on-a-diffusion-probabilistic-model): diffusion vocoders

### Consonants, sibilants and onset detection

- [Phonetically explainable speech deepfake detection](https://arxiv.org/pdf/2606.15454) (full): burst length, vocoder frame sizes, fricative artifacts
- [Keshet et al., plosive spotting](https://keshet.net.technion.ac.il/files/2022/05/KeshetChBo01.pdf): burst duration range
- [Audio AI guide, onset detection](https://aiunderstanding.org/learn/onset-detection-in-audio): spectral flux with adaptive threshold
- [Sibilant spectral estimation](https://pmc.ncbi.nlm.nih.gov/articles/PMC4376757), [sibilant moments table](https://pmc.ncbi.nlm.nih.gov/articles/PMC7242990/table/T2) (could not be opened): centroid and spectral means
- [Sound on Sound, vocal editing](https://www.soundonsound.com/techniques/vocal-editing-pitch-time?amp): avoid pitch processing on consonants and breaths

### Rough, creaky and growled voice

- [Keating et al.](https://pages.ucsd.edu/~mgarellek/files/Keating_etal_ASA_2023_Spring.pdf), [Watkins et al.](https://www.fon.hum.uva.nl/paul/papers/2024-intsp-WatkinsBoersmaHamann.pdf), [Ikuma](https://www.medschool.lsuhsc.edu/otorhinolaryngology/docs/Ikuma_ICVPB2024_Abstract_ML_Pitch.pdf) (full): creak subtypes, octave jumps, tracker errors
- [Fuks](https://compmus.ime.usp.br/sbcm/1999/papers/Leonardo_Fuks.html), [Czedik-Eysenberg et al.](https://pure.hud.ac.uk/en/publications/towards-an-acoustic-semantic-space-of-extreme-metal-vocal-styles/): subharmonics and extreme vocals
- [US 9,047,865](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9047865): fry detector thresholds

### Breathy voice and perturbation measures

- [LMU voice-quality notes](https://www.phonetik.uni-muenchen.de/~hoole/kurse/expphon3/voicequality/voicequality_notes.pdf) (full): H1–H2, HNR, harmonic range in breathy voice
- [PMC review of breathiness cues](https://pmc.ncbi.nlm.nih.gov/articles/PMC2891879), [formant-aware analysis](https://repository.lsu.edu/comd_pubs/87), [VOXplot](https://elib.uni-stuttgart.de/handle/11682/13694), [H1–H2 and CPP study](https://repositorio.ufpb.br/jspui/handle/123456789/39253): breathiness measures
- [GNE study](https://core.ac.uk/works/5169756), [GNE tool documentation](https://glama.ai/mcp/servers/Vocametrix/vocametrix-mcp/tools/vocametrix_calculate_gne): glottal-to-noise excitation
- [US 6,304,846](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6304846): breath noise by phase randomization
- [Praat jitter](https://www.fon.hum.uva.nl/praat/manual/Voice_2__Jitter.html), [Praat shimmer](https://www.fon.hum.uva.nl/praat/manual/Voice_3__Shimmer.html): definitions and reference thresholds
- [Kroon et al.](https://arxiv.org/pdf/2206.14357), [BLDE norms](https://doaj.org/article/daa48a766a444357b93ddf287ef2ea22), [Louder, vibrato rate](https://loudersound.com/news/scientists-break-down-freddie-mercury-s-vocal-skills): voicing threshold, HNR norms, vibrato range
