# Time-scale modification for monophonic notes: findings and toolkit notes

*Status: research input to ADR-0026; not normative.* *Compiled 2026-10-23. Covers the literature on stretching and squishing monophonic audio, and implementation notes for each toolkit worth considering from C++. Licensing is deliberately left out of the analysis (license names appear only as one-line facts).*

**Legend for confidence tags**

- **[V]** Verified in a source I retrieved during this research (links in section 9).
- **[M]** From my general knowledge, not checked against a source this session. Verify against the library's headers or docs before relying on it.
- **[I]** My own inference or proposal. Nothing marked this way has been built or listened to.

---

## 1. Summary

- Your two ideas map onto the two classical families. **Cut/copy/splice** is the overlap-add family: OLA, SOLA, WSOLA, PSOLA, ESOLA. **Full reconstruction** is analysis-resynthesis: phase vocoder, sinusoidal and harmonic+noise models, source-filter vocoders, neural decoders. [V]
- The two families have opposite failure modes. Splicing preserves timbre and waveform exactly but doubles or skips transients and tracks only one periodicity. Phase-vocoder reconstruction handles complex harmonic material but smears transients and gets "phasiness". [V]
- Most of the improvements since about 2000 are hybrids: phase vocoder plus periodic splice resets (PVSOLA), harmonic/percussive split with different methods per part, and sines + transients + noise with each part treated differently. [V]
- Independent listening tests do not show hybrids as automatic wins. Implementation details dominate. [V]
- Published work specifically on stretching an isolated monophonic note by extending its sustain (loop/crossfade plus vocoder fallback) is thin. The closest ideas are in sections 3 and 4.

---

## 2. Algorithm families

### 2.1 Splice family

| Method | Idea | Notes |
|---|---|---|
| OLA | Window fixed-position frames and overlap-add them at a different hop | Phase-jump artifacts on harmonic signals; works for pure percussion with ~10 ms frames [V] |
| SOLA / SOLAFS | Shift frames to maximize similarity with the output so far | Time-domain; efficient [V] |
| WSOLA | Shift each analysis frame within ±Δmax to best match the natural continuation of the previous frame (cross-correlation) | Locks onto the dominant periodicity only: good for monophonic, bad for polyphonic [V] |
| TD-PSOLA | Pitch marks; windows about two periods; repeat or delete segments to stretch | Depends on accurate pitch marks. "Null model": no explicit signal model [V] |
| ESOLA / FESOLA | Align frames on epochs (glottal closure instants). FESOLA cross-correlates time-smeared epochs | ESOLA claims exact scaling for 0.5 to 2. Works for speech and monophonic, not polyphonic [V] |
| NFC-TSM | Ring buffer; AMDF search for the best splice point on a low-passed signal | Built for real-time monophonic musical tones [V] |

Known artifacts of the splice family:
- Transient doubling when stretching, transient skipping when compressing. [V]
- Warbling if the frame doesn't capture a full period. [V]

Fix for doubling and skipping: set the analysis hop equal to the synthesis hop near detected transients, so those regions are copied untouched, and compensate the stretch elsewhere. [V]

### 2.2 Reconstruction family

| Method | Idea | Notes |
|---|---|---|
| Phase vocoder (PV-TSM) | STFT; estimate instantaneous frequency per bin; propagate phases at the synthesis hop | Horizontal phase coherence kept; vertical coherence lost, causing transient smearing and phasiness [V] |
| Identity phase locking (Laroche & Dolson 1999) | Propagate phase only at spectral peaks; lock neighbouring bins to the nearest peak | Reduces phasiness and smearing; can add metallic roughness [V] |
| Phase-gradient reconstruction (Průša & Holighaus) | Phase reconstruction from magnitude without peak picking or transient detection | Authors claim no typical PV artifacts even at extreme factors [V; their claim] |
| Sinusoidal / SMS | Sinusoid tracks plus a stochastic residual; stretch the envelopes | Transients break the model [V] |
| Sines + transients + noise (Verma & Meng 1998) | Transient regions are translated in time, not stretched; sines and noise are stretched | Early versions needed explicit transient detection [V] |
| HNM (Stylianou et al.) | Harmonic sinusoids plus AR-modelled, time-modulated noise; pitch-synchronous analysis and synthesis; PSOLA-inspired modification | Speech origin [V] |
| WORLD vocoder | F0 + spectral envelope + aperiodicity; resynthesize | Speech-oriented. Instrument quality unverified [V for features, I for the caveat] |
| Neural | Latent "Neuralgram" interpolation (Chu et al. 2022); STN split with a WaveNet for the noise part (Fierro et al. 2022); FiLM-conditioned speech TSM (2025) | Aimed at extreme factors or speech. Not obviously suited to a note editor [V] |

### 2.3 Hybrids

| Hybrid | Splice side | Reconstruction side |
|---|---|---|
| Dorran, Lawlor, Coyle (JAES 2006) | Time-domain behaviour to avoid phasiness | Phase vocoder [V] |
| PVSOLA (Moinet & Dutoit 2011) | Periodically insert an unmodified input frame, positioned by cross-correlation, to reset phase coherence | Ordinary phase vocoder in between [V] |
| Röbel 2010 | Frequency-domain SOLA that uses the PV for phase sync; beat PSOLA and HNM in his perceptual tests | Phase vocoder [V] |
| Driedger, Müller, Ewert 2014 (HPTSM) | OLA with ~10 ms frames on the percussive part | PV with identity phase locking on the harmonic part; split by median-filter separation [V] |
| Bonada; Röbel 2003; Duxbury et al. 2002 | Stretch factor 1 and phase reset at attacks (Röbel does this per bin) | Phase vocoder on steady-state regions [V] |
| Sines+transients+noise (Fierro & Välimäki; Moliner et al. "noise morphing") | Transients relocated | Fuzzy three-way split; noise handled by morphing a white-noise excitation to the interpolated noise spectrum [V] |
| Nagel & Walther 2009 | Cut transients out, fill the gap with linear prediction, PV-stretch the rest, reinsert transients at the new positions | Phase vocoder [V, as described in the review] |
| Rubber Band R2 | Phase resets on percussive transients | Block-based PV with adaptive stretch ratio between resets and a "lamination" step for vertical coherence [V] |

---

## 3. Evidence on quality (Roberts & Paliwal, J. Acoust. Soc. Am. dataset paper)

42,529 ratings from 633 sessions. Material: speech, solo instruments, music. Findings: [V]

- Slowing down (stretching) is harder than speeding up. Quality drops as the ratio moves away from 1.
- PV is rated above WSOLA for slowdowns; the order reverses for speedups. The authors' explanation: WSOLA's transient doubling bothers listeners more than PV's smearing when stretching.
- Monophonic harmonic instruments were rated highest; synth bass and transient-heavy material lowest.
- In their implementation, plain identity-phase-locked PV beat HPTSM at nearly every ratio, contradicting the original paper's result. Suspected causes: separation artifacts, the very short WSOLA frame, or sample-rate differences. Hybrids are not automatic wins.
- Existing objective measures (SER, DM) correlate poorly with listener scores (about 0.37 and 0.16). Plan on listening tests.
- The authors argue ideal TSM would be source-aware and would modify only the sustain of a note's envelope. A piano's decay, for example, arguably should not be lengthened. They call content-aware TSM underexplored.

Dataset: IEEE DataPort entry 1987. Their implementations (including FESOLA) are at github.com/zygurt/TSM. [V]

---

## 4. Proposed note-level design (all [I])

1. **Segment** the note into attack, sustain and release with an onset and envelope detector.
2. **Attack:** copy at ×1, shifted in time only (Bonada, S+T+N and Duxbury all keep transients unstretched).
3. **Sustain, stable pitch and modest factor:** pitch-aware splicing (epoch or WSOLA style, tolerance of at least half a period). Advance slowly through the source instead of looping a few fixed periods, which sounds static.
4. **Sustain, ill-defined period** (strong vibrato, inharmonic partials) **or large factor:** phase-locked vocoder, with a phase reset at the handoff (PVSOLA idea).
5. **Noise component** (breath, bow noise) for large factors: stretch separately (noise morphing).
6. **Release:** instrument-dependent. Keep the decay slope for percussive instruments (piano); scale it for bowed or blown ones.
7. **Evaluate** by ear. Use the Roberts & Paliwal dataset's solo-instrument files as a benchmark.

---

## 5. Toolkit notes

### 5.1 Rubber Band

- **Language / build:** C++, one public class `RubberBand::RubberBandStretcher`. Meson is the primary build system; requires a C++11 compiler. Pluggable FFT and resampler backends. [V]
- **Engines:** R2 ("faster") and R3 ("finer"). Both support offline and real-time modes. Mode and engine are fixed at construction. R3 almost always sounds better but costs significantly more CPU. [V]
- **Offline mode:** handles padding and delay compensation so the stretched result has an exact start and duration. `setKeyFrameMap()` supports pre-planned variable stretching, conceptually your TimeMap. [V]
- **Real-time mode:** time and pitch ratios can change at any time. You may need `getPreferredStartPad()` and `getStartDelay()` for padding and delay compensation. RT-safe with normal parameters. [V]
- **CLI:** `--time X` stretches to X times the duration (so >1 = longer). `--timemap F` reads a file of lines with two sample-frame numbers (source, target); when supplying a map you must also give an overall stretch with -t, -T or -D. There's an analogous pitch map. R2 has a crispness setting (0 to 6, default 5). Shorter window on R3 is a quick draft mode. [V]
- **Algorithm (R2 technical notes):** block-based phase vocoder; phase resets on percussive transients; adaptive stretch ratio between reset points; lamination for vertical phase coherence. Pitch shift is done by resampling before or after a stretch. The author says it implements no single published method, and the adaptive-stretch and lamination parts are unpublished. R3 differs technically; I did not find R3 details. A Vamp plugin can show the dynamic stretch ratios it chooses. [V]
- **Typical offline call sequence:** construct with sample rate, channel count, options (offline + finer engine), set time ratio and pitch scale, `setExpectedInputDuration`, `study()` over all blocks, then `process()` blocks and drain with `available()` / `retrieve()`. [M]
- **Fit for your project:** treat it as a black box. It takes ratios or maps, not per-note pitch marks or frame classes. Good as an A/B reference and as a possible renderer for unedited stretched spans, but your joint pitch-and-time schedule needs inputs it can't take.

### 5.2 Signalsmith Stretch

- **Language:** C++11, header-only (`signalsmith-stretch.h`), template class `signalsmith::stretch::SignalsmithStretch<float>`. [V]
- **API surface:** `presetDefault(channels, sampleRate)`, `presetCheaper(...)`, `configure(channels, blockSamples, intervalSamples)`, `blockSamples()`, `intervalSamples()`, `reset()`, `setTransposeSemitones(semitones, tonalityLimit)`, `setFreqMap(lambda)`, `setFormantBase(...)`, `inputLatency()`, `outputLatency()`, `seek()`, `flush()`. [V]
- **Time-stretching:** hand differently sized input and output buffers to `process()`. If block lengths are integers, you must make sure they average to the ratio you want over time. Latency is ambiguous for a stretch effect: supply input slightly ahead of the processing time and expect output slightly behind. Call `seek()` with the first `inputLatency()` samples to align processing time with input start. At the end you get roughly `outputLatency() + inputLatency()*stretchFactor` extra samples of zero input to flush. [V]
- **Quality range:** pitch shifts of multiple octaves are fine; time-stretching sounds best for modest changes (0.75x to 1.5x). [V]
- **Origin:** the approach from the ADC22 talk "Four Ways To Write A Pitch-Shifter" (more detail in the author's blog post). I haven't read it. Rust bindings exist (`ssstretch`). [V]
- **Following a TimeMap:** the block-size-ratio interface suggests you can vary the input/output block ratio per call to follow a non-constant map. [I; check the author's notes on varying rate]
- **Fit:** the lowest-friction dependency to try first, and a good modern baseline.

### 5.3 SoundTouch

- **Language:** C++. Compiles on most platforms with SSE/MMX-optimized paths; Java, C# and Delphi import modules included. [V]
- **Algorithm:** time-domain WSOLA-like stretching with performance tweaks. Tempo is pure time-stretch. Pitch is time-stretch combined with sample-rate transposing. Rate transposing affects both. [V]
- **Constraints:** mono or stereo; 16-bit integer or 32-bit float; roughly 100 ms input/output latency when time-stretching. [V]
- **Typical usage:** create a `SoundTouch` object, set sample rate and channels, set tempo or pitch, push with `putSamples()`, pull with `receiveSamples()`, call `flush()` at the end. Tunable via `setSetting` (sequence, seek window, overlap lengths). [M]
- **Fit:** quick WSOLA baseline for monophonic notes. It's tuned for general music and speech, not isolated notes, so expect transient doubling on attacks.

### 5.4 Praat (Manipulation / TD-PSOLA)

- **Language:** mostly C++ with some C; needs C99 and C++17. Sources live in directories `fon` and `sys` and are built per-platform with the makefiles in the source tree. [V]
- **Manipulation object contents:** the original Sound, a PointProcess of glottal pulses, a PitchTier, and a DurationTier. Header API (Boersma): `Manipulation_create`, `replaceOriginalSound`, `replacePulses`, `replaceIntensityTier`, `replacePitchTier`, `replaceDurationTier`, `Sound_to_Manipulation(sound, timeStep, minPitch, maxPitch)`, `Manipulation_to_Sound(me, method)`. [V]
- **Creation steps:** pitch analysis (typical values: 10 ms step, 75 Hz floor, 600 Hz ceiling) → place pulses where the sound has much energy → convert the pitch contour to a PitchTier → create an empty DurationTier. [V]
- **Synthesis methods:** overlap-add (original sound + pulses + pitch tier + duration tier), LPC, or pulses/pitch only. [V]
- **Overlap-add rules** [V]:
  1. Generate new pulses along the time axis from the PitchTier.
  2. Remove new pulses that fall in voiceless intervals, defined as places where adjacent original pulses are more than 20 ms apart.
  3. Copy voiceless parts from the source, reusing parts when the local duration factor is above 1.
  4. For each target pulse, find the nearest source pulse and copy a bell-windowed piece centred there to the target location. The window's left half-length is the smaller of the adjacent left periods at source and target; analogously on the right.
- **DurationTier:** points of (time, relative duration), linearly interpolated, where 1.5 means slower. A TextGrid-to-DurationTier command builds ramps (1.0 → factor → factor → 1.0). [V]
- **Quality note from the manual:** resynthesis quality depends critically on the pitch/pulse measurements, so check them before synthesis. [V]
- **Where to read** (filenames from memory): `fon/Manipulation.cpp`, `fon/PitchTier_to_PointProcess*.cpp`, `fon/Sound_to_PointProcess*.cpp`. [M]
- **Reuse cost:** heavy coupling to Praat's own object model (`Thing`, `Data`, `oo_CLASS` macros, `Sound`, `PointProcess`). Treat it as a reference, not a lift-out library.
- **Python oracle:** the `psola` package wraps parselmouth (Praat) so you can generate reference renders without building Praat. See 5.7.

### 5.5 TSM Toolbox, libtsm, PyTSMod: porting guide

**Existing code**
- **TSM Toolbox (MATLAB):** `wsolaTSM.m` (OLA or WSOLA), `pvTSM.m` (PV with or without identity phase locking), the HPS-based method, a wrapper around élastique, demo scripts including non-linear TSM. [V]
- **libtsm:** Python re-implementation of the MATLAB toolbox, pip-installable, with tests that compare against the MATLAB output. API docs at meinardmueller.github.io/libtsm. It also covers adaptive pitch-shifting. [V]
- **PyTSMod:** Python: OLA, WSOLA, PV (with phase locking), HPTSM, TD-PSOLA (needs an external F0 track; takes separate time and pitch scale factors). Scale factors can be fixed or given as anchor points. [V]
- **No C++ port found** for any of these.

**Framework (all methods)** [V]
- Frames of N samples (typically 50 to 100 ms), analysis hop Ha, synthesis hop Hs (commonly N/2 or N/4), stretch α = Hs/Ha, so Ha = Hs/α.
- OLA: window each analysis frame (Hann) and normalize by the sum of overlapping windows; with Hann and Hs = N/2 the sum is 1.

**WSOLA** [V]
- For each next frame, compute the natural progression of the previous adjusted frame (the samples that follow it by Hs).
- Search the extended region (±Δmax around the nominal position at (m+1)·Ha) for the shift maximizing cross-correlation with that natural progression. Window, normalize and add.
- Defaults from the review: N = 50 ms, Δmax = 25 ms. Requirement: frame covers at least one full period; tolerance at least half a period.
- Start at m = 0 with Δ0 = 0. Δmax = 0 reduces to OLA.
- Time-varying stretch (anchor points): synthesis instants s_m = m·Hs/Fs, analysis instants a_m = τ⁻¹(s_m), where τ interpolates linearly between anchor pairs.

**Phase vocoder** [V]
- STFT with window length around 100 ms (large, for frequency resolution) and a small analysis hop.
- Instantaneous frequency per bin: IF = ω + Ψ(φ2 − (φ1 + ω·Δt)) / Δt, where ω is the bin frequency, Ψ wraps to the principal range, and Δt = Ha/Fs.
- Phase propagation: φMod(m+1, k) = φMod(m, k) + IF·Hs/Fs, starting from φMod(0, k) = φ(0, k).
- Inverse FFT per frame, synthesis window, normalize by the sum of squared windows.
- Identity phase locking: detect peaks in the magnitude spectrum; update phase only at peaks as above; for the remaining bins, keep their original phase offset relative to the nearest peak (rotate by the peak's phase change) [M, for the exact rotation formulation; the review describes it in words].

**HPS hybrid** [V]
- Median-filter the magnitude spectrogram horizontally (harmonic) and vertically (percussive), with binary masks (harmonic where the horizontal result is larger).
- Harmonic part: PV with identity phase locking, large frame. Percussive part: OLA with very short frames (about 10 ms). Sum the two.

**Parameters used in the Roberts & Paliwal listening tests** (44.1 kHz) [V]
- PV and IPL: frame 2048 samples (46.4 ms), synthesis hop 512.
- WSOLA: frame 1024 (23.2 ms), hop 512, tolerance 512 samples.
- HPTSM percussive WSOLA: frame 256 (5.8 ms), hop 64.
- Note these differ from the review's defaults. For a monophonic note, a lower F0 needs longer frames or tolerance (the dataset paper notes male voices rated worst, with low-frequency resolution and short frames suggested as causes).

**Porting tips** [M]
- Use a real-FFT library (pocketfft, KissFFT, FFTW).
- Normalized cross-correlation is more robust than raw.
- For a note with known pitch, constrain the WSOLA search to ±(0.5 to 1) period around the nominal position.
- Handle first-frame phases and the tail (flush) explicitly.

### 5.6 WORLD vocoder

- **What it is:** analysis and resynthesis based on F0, a harmonic spectral envelope (CheapTrick) and aperiodicity (D4C). F0 comes from DIO or Harvest, refined with StoneMask. The authors state no patents cover any of its algorithms. The C++ code is the main distribution; pyworld wraps it. [V]
- **Caveats:** designed for speech sampled at 16 kHz or above. The real-time synthesis function is not updated, and the spectral-envelope codec isn't compatible with earlier versions. [V]
- **Time stretching** is done by re-timing the three parameter tracks and resynthesizing. A cited use runs a 1024-point FFT with a 5 ms frame shift. Everything is regenerated, so source waveform detail (including vibrato phase relationships and attack shape) is not preserved. [V for the settings; I for the consequences]
- **Fit:** speech-trained. I found no evidence on instrument quality.

### 5.7 Python `psola` package

- Wraps parselmouth around Praat's TD-PSOLA. `pip install psola`. Entry points: `psola.vocode`, `from_file`, `to_file`, `from_file_to_file`. Pitch shifting takes a numpy array of target pitch values equally spaced in time; variable-rate time stretching uses forced phoneme alignment (pypar). For constant stretch you pass a constant. [V]
- **Use:** generate Praat reference renders from Python to compare against your engine.

### 5.8 Methods with papers but no easy code

- **ESOLA** (arXiv 1801.06492): epoch-synchronous overlap-add; the authors claim at least 3x faster than SOLAFS. [V]
- **FESOLA** (WASPAA 2019): zygurt's repo may contain it (the dataset paper says implementations are at github.com/zygurt/TSM). [V for the pointer]
- **NFC-TSM** (DAFx-07): AMDF splice search on a low-passed signal, ring buffer. Paper only. [V]
- **PVSOLA** (DAFx-11), **Röbel 2003 / 2010**: papers only (DAFx archive). [V]
- **Phase Vocoder Done Right** (arXiv 2202.07382): no code pointer found. [V]
- **SiTraNo:** a MATLAB app for sines-transients-noise decomposition by Fierro, mentioned on the author's profile. I didn't find the repo. [V for existence of the name, weak source]
- **Neural:** TSM-Net, DiffATSM, STSM-FiLM; research code only. [V]

---

## 6. How this maps onto your project (from the ruleset doc only; I have not read the code)

- **Praat's overlap-add rule is the nearest-source-mark rule** your ruleset's grain-event analysis (section 4.3) assumes. The Python `psola` wrapper lets you cross-check your simulated event rates against real Praat output on real pulses. [I]
- **ESOLA/FESOLA and NFC-TSM** choose the splice by cross-correlation or AMDF rather than the nearest mark. That is close to your S4 recipes 3 (cross-fade neighbouring grains) and 4 (best-matching grain placement). [I]
- **Rubber Band's key-frame map** is a TimeMap analogue, so it can render a stretched-source reference for the same map. [I]
- **HNM and WORLD** are the closest published relatives of your `HarmonicNoiseEngine`. HNM's pitch-synchronous synthesis in particular is a PSOLA-inspired harmonic model. [I]
- **PVSOLA's phase reset** is the pattern to borrow if you route runs between PSOLA and the harmonic engine: copy a frame unmodified at the boundary and align by cross-correlation. Your 4 ms edge fades are the simpler version. [I]
- **Attack protection** (Bonada, Röbel, S+T+N, Duxbury) matches your "consonants and attacks keep slope 1 and vowels absorb" rule. Roberts & Paliwal's sustain-only argument supports the same design. [I]
- **Vibrato under stretch** is not addressed by anything I found. It stays an open question in your D2 decision. [I]

---

## 7. Suggested experiment plan [I]

1. **Renderers to compare:**
   - Your PSOLA engine.
   - Praat via the `psola` wrapper.
   - Rubber Band R3 offline with a key-frame map.
   - Signalsmith Stretch.
   - SoundTouch.
   - Ported WSOLA and identity-phase-locked PV.
   - Your attack/sustain hybrid.
2. **Test notes:** sustained vowel with vibrato; flute or clarinet; plucked or piano; bowed with noise; one note with a sharp consonant or attack.
3. **Stretch factors:** 0.8, 1.03, 1.25, 2, 4. Include opposing pitch shifts for the combined cases in your ruleset.
4. **Objective checks:** pitch drift, vibrato rate and extent, cycle-jump measure, click detector. Treat these as sanity checks only.
5. **Listening:** a fixed blind A/B sheet. Use the dataset's solo-instrument files as external references.

---

## 8. Gaps and caveats

- Several key papers are paywalled (AES, some JAES). I relied on abstracts for those.
- I did not verify Rubber Band R3's internals, the `setKeyFrameMap` argument format, or SoundTouch's setting names.
- I did not find a C++ port of libtsm, and I did not confirm libtsm's or PyTSMod's own license text.
- I did not run any code. Everything marked [I] is untested.
- The perceptual claims in section 3 come from one dataset study with its own implementations. They don't settle which method is best for your notes.

---

## 9. Sources

**Reviews and evaluation**
- Driedger & Müller, *A Review of Time-Scale Modification of Music Signals* (Appl. Sci. 2016): https://www.cs.bu.edu/fac/snyder/cs583/Literature%20and%20Resources/AReviewOfTimeScaleModification.pdf
- Roberts & Paliwal, *A Time-Scale Modification Dataset with Subjective Quality Labels*: https://arxiv.org/pdf/2006.00848
- Driedger, Müller, Ewert, *Improving TSM of Music Signals using Harmonic-Percussive Separation*: https://publica.fraunhofer.de/handle/publica/236969
- Liuni & Röbel, *Phase vocoder and beyond*: https://hal.archives-ouvertes.fr/hal-01250848

**Splice family**
- Charpentier & Moulines, *Pitch-synchronous waveform processing techniques*: https://www.isca-archive.org/eurospeech_1989/charpentier89_eurospeech.html
- Roelands & Verhelst, *WSOLA: structures and evaluation*: https://www.isca-archive.org/eurospeech_1993/roelands93_eurospeech.html
- ESOLA: https://ar5iv.labs.arxiv.org/html/1801.06492
- FESOLA: https://research.usq.edu.au/item/zz13q/time-scale-modification-using-fuzzy-epoch-synchronous-overlap-add-fesola
- NFC-TSM (Haghparast et al.): https://dafx.de/paper-archive/details/68OcHesH2q-0CyfhEqyPhA

**Reconstruction family**
- Laroche & Dolson, *Improved Phase Vocoder TSM of Audio*: https://seniordesign.cs.tcu.edu/1516Ribbit/resources/ImprovedPhaseVocoder.pdf
- Laroche & Dolson, *Phasiness*: https://www.ee.columbia.edu/~dpwe/papers/LaroD97-phasiness.pdf
- Průša & Holighaus, *Phase Vocoder Done Right*: https://arxiv.org/pdf/2202.07382
- Serra & Smith, SMS: https://mtg.upf.edu/node/251
- Stylianou et al., HNM: https://www.isca-archive.org/eurospeech_1995/stylianou95b_eurospeech.html
- Verma & Meng, S+T+N: https://dafx.de/paper-archive/details/pmF6ZgSLbsx9SOayuwZq5g
- Julius O. Smith, TSM and S+N+T: https://ccrma.stanford.edu/%7Ejos/sasp/TSM_S_N_T.html
- Fierro & Välimäki, fuzzy decomposition: https://arxiv.org/abs/2210.14041v2
- Moliner et al., noise morphing: https://arxiv.org/html/2312.14586v1
- Fierro et al., neural extreme stretching: https://arxiv.org/abs/2211.16992
- Chu et al., Neuralgram TSM: https://arxiv.org/abs/2210.17152

**Hybrids**
- Dorran et al., hybrid time-frequency TSM: https://mural.maynoothuniversity.ie/12736
- PVSOLA: https://dafx.de/paper-archive/details/9UQ9PWY6URdHRFgtlSGduA
- Röbel 2010: https://dafx.de/paper-archive/details/nuzjTGQc-Ps8SNT_pYgTXA
- Röbel 2003: https://dafx.de/paper-archive/details/svqqbE0vLRFuz4BsmGhoDg
- Duxbury et al. 2002: https://aes2.org/publications/elibrary-page/?id=11324
- Nonstationary Gabor phase vocoder (summarizes Bonada's attack handling): https://arxiv.org/pdf/1612.05156

**Toolkits**
- Rubber Band: https://breakfastquay.com/rubberband/ (docs https://breakfastquay.com/rubberband/code-doc ; technical notes https://breakfastquay.com/rubberband/technical.html)
- Signalsmith Stretch: https://github.com/Signalsmith-Audio/signalsmith-stretch
- SoundTouch: https://codeberg.org/soundtouch/soundtouch
- Praat source: https://github.com/praat/praat.github.io ; manual: https://www.fon.hum.uva.nl/praat/manual/overlap-add.html and https://www.fon.hum.uva.nl/praat/manual/Manipulation.html
- TSM Toolbox: https://audiolabs-erlangen.de/resources/MIR/TSMtoolbox
- libtsm: https://github.com/meinardmueller/libtsm/
- PyTSMod: https://pypi.org/project/pytsmod
- psola (Python): https://pypi.org/project/psola
- WORLD: https://github.com/mjansche/World ; PyWORLD: https://github.com/JeremyCCHsu/Python-Wrapper-for-World-Vocoder
