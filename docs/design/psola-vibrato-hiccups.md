# PSOLA pitch shifting and vibrato: what actually changes, and how to compensate

Research report, 2026-10-21.

> **For Claude Code.** This is a brief for building or checking a PSOLA-based pitch shifter for singing (offline, quality first). Each claim carries a **Basis** tag: *Sourced* (a paper, lecture notes or patent I read), *Derived* (arithmetic or reasoning from sourced facts), *Simulated* (measured in my own test, described in section 8), or *Guess*. The simulated numbers come from one synthetic vowel with exact pitch marks. Re-measure them on real recordings before trusting any threshold.

## Summary

- **PSOLA does not speed up or slow down vibrato by itself.** If each synthesis time maps to the same analysis time (a duration-preserving, local time map), the vibrato rate in the F0 contour is unchanged at any shift ratio. In my test it stayed at 5.500 Hz from −5 to +12 semitones. *Simulated.*
- **The vibrato rate does change when the time map is not the identity.** A one-to-one mark mapping, or any uniform time-stretch not cancelled by an equal and opposite one, scales the rate by the local time-map slope. At +5 semitones that moved 5.5 Hz to 7.34 Hz. *Simulated, Derived.*
- **What you are hearing is more likely the "hiccups".** A duration-preserving shift must repeat grains (up-shift) or skip them (down-shift) at the rate `F0 × |β − 1|`. For small shifts that rate is 3–15 events per second, the same range as vibrato, so the output gets a periodic stutter layered on the vibrato. *Derived.*
- **The hiccups are discrete events, not a steady modulation.** The sinusoidal component at the repeat rate was only 0.01–0.23 cents and 0.001–0.04 dB in my test. At the junctions themselves the cycle-to-cycle waveform change was up to about 30% off normal with nearest-grain copying (7–32% across my cases), and 0–18% off with blended grains. *Simulated.*
- **Two smaller effects ride along.** Shifting by adding Hz instead of multiplying changes the vibrato extent in cents, and the amplitude-modulation (AM) half of the vibrato no longer matches what the same vocal tract would produce at the new pitch. *Simulated, Derived.*
- **Compensation, in order of payoff:** make the time map local and verify it; shift the note-level pitch and re-impose the vibrato; blend neighbouring grains instead of copying the nearest one; avoid grain-based shifting in the hazard zone (section 4). Details in section 6.

## 1. How PSOLA maps time and pitch

TD-PSOLA works on pitch-synchronous grains (Hann windows centred on analysis pitch marks, typically two to four local periods long). To change pitch it places a copy of a grain at each synthesis pitch mark, spaced `P(t)/β` apart, and overlap-adds the copies. The grain content is not altered, so formants stay roughly where they were. *Sourced: [Illinois ECE 420 PSOLA notes](https://courses.grainger.illinois.edu/ece420/5_PSOLA.pdf); [KAIST lecture slides](https://mac.kaist.ac.kr/~juhan/gct535/Slides/13%20Time%20Scale%20Modification%20and%20Pitch%20Shifting.pdf).* I did not open the original Moulines and Charpentier paper.

Three symbols matter for what follows:

```
β      pitch-scale factor   (F0_out = β · F0_in)
γ      time-scale factor    (output duration = γ · input duration)
τ(s)   analysis time that synthesis time s reads from
```

- For each synthesis mark, the algorithm picks the analysis mark nearest to `τ(s)`.
- When `β = γ`, every synthesis mark maps to a different analysis mark, one to one.
- When `β ≠ γ`, some analysis grains are used twice (to slow down or raise pitch at constant duration) or skipped (to speed up or lower pitch).
- A pitch shift that keeps duration is `γ = 1`, so `τ(s) = s`.

The notes also say the method adds artifacts at the edges of its design: windows longer than about four periods attenuate harmonics even at `β = 1.05`, and a window centre displaced by more than about 30% of a period sounds hoarse. *Sourced: Illinois notes, as I read them.*

Two common ways to build a shifter without a direct mark mapping are time-stretch then resample (stretch by `α`, resample by `α`; [KAIST slides](https://mac.kaist.ac.kr/~juhan/gct535/Slides/13%20Time%20Scale%20Modification%20and%20Pitch%20Shifting.pdf), [US 7,974,838](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7974838)) and resample with cycle insertion or deletion ([US 5,973,252](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5973252)). Both have the same repeat-or-drop structure inside.

## 2. Vibrato terms and typical values

Vibrato is a roughly sinusoidal F0 modulation with a **rate** (Hz) and an **extent** (usually cents or percent of F0). It also has an amplitude side: partials sweep back and forth through the vocal-tract resonances, so their levels rise and fall too.

| Quantity | Value | Basis |
|---|---|---|
| Rate | 5–7 Hz for classically trained singers | Sourced: literature range quoted in [Lester-Smith et al. 2021](https://pmc.ncbi.nlm.nih.gov/articles/PMC8995401); also measured in 48 recordings by [van den Berg et al. 1995](https://www.coli.uni-saarland.de/Phonetics/icphs/ICPhS1995/13_ICPhS_1995_Vol_1/p13.1_230.pdf) |
| Rate | 5.0–5.7 Hz across four singers (the most consistent parameter) | Sourced: [Maher and Beauchamp 1990](https://exponent.montana.edu/rmaher/publications/maher_applac_0290_219-245.pdf) |
| Rate | 4.8 Hz mean in nine singers' sustained vowels | Sourced: Lester-Smith et al. 2021 |
| Extent | ±2% to ±5% of F0 (Maher and Beauchamp); 2.8% mean (Lester-Smith); about 3% for normal vibrato (van den Berg) | Sourced |
| AM of partials | Partial levels move with the vibrato because the partials are swept through resonances | Sourced: Maher and Beauchamp 1990; [Arroabarren and Carlosena 2004](https://asp-eurasipjournals.springeropen.com/articles/10.1155/S1110865704401127) |
| AM rate | A partial swept symmetrically across a formant peak shows AM at **twice** the vibrato rate | Sourced: van den Berg et al. 1995; Maher and Beauchamp 1990 |

Listeners also seem to judge the rate of frequency change as much as the extent. Maximum acceptable excursion peaked at 5–7 Hz in a listening study, with large variation between listeners ([DTU abstract](https://orbit.dtu.dk/en/publications/maximum-acceptable-vibrato-excursion-as-a-function-of-vibrato-rat/)). I found no published just-noticeable difference for vibrato rate, so the rate tolerance in section 7 is a guess.

## 3. When the vibrato rate itself changes

The output F0 is `β(s) · F0_in(τ(s))`. The input vibrato has phase `2π · f_v · τ`, so:

```
f_v,out = f_v,in · dτ/ds        (Derived)
```

Anything that makes `dτ/ds ≠ 1` moves the rate. In my test the measured rate matched this formula to three decimals (section 8).

| Cause | `dτ/ds` | Effect on vibrato | Basis |
|---|---|---|---|
| Duration-preserving, local map (`τ(s) = s`) | 1 | Rate unchanged; extent unchanged in cents | Simulated: 5.500 Hz at −5, −2, +2, +5, +12 semitones |
| One-to-one mark mapping, no duration fix (`β = γ`) | β | Rate × β; duration ÷ β | Simulated: 4.120, 4.900, 6.174, 7.342, 11.000 Hz at −5, −2, +2, +5, +12 semitones; an independent tracker agreed to ±0.001 Hz up to +5 |
| Uniform time-stretch of the whole contour | 1/γ | Rate ÷ γ. A factor-2 stretch halves it | Sourced: [Baran, Malyska and Quatieri 2010](https://www.ll.mit.edu/r-d/publications/preserving-character-perturbations-scaled-pitch-contours) report "excessively slow flutter and vibrato" under uniform scaling |
| Stretch by one factor, resample by another (per-note vs per-frame factors, rounding) | stretch/resample ratio | Rate off by that ratio | Derived |
| Stretch then resample, both with a time-varying factor | Not 1 unless both stages use the same time axis | Rate and phase drift where the factors disagree | Derived; not tested |

Two things that look like a rate change but are not:

- **Extent.** A shift by adding Hz (`F0_out = F0_in + ΔF`) changes the extent in cents: ±59.8 cents became ±41.1 cents when 220 Hz moved to about 320 Hz this way, against a predicted ±41.2. A ratio shift kept ±59.8. *Simulated.*
- **Tracker smoothing.** A pitch tracker that limits how fast its estimate can move (as in [US 7,974,838](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7974838)) shrinks and delays the vibrato it passes on, but does not change its rate. *Derived from the patent text; not tested.*

## 4. The hiccups

### Mechanism

With the duration preserved, a shift by `β` needs `β` times as many synthesis marks per second as there are analysis marks. So:

```
f_rep = F0 · |β − 1|        events per second   (Derived)
```

- **Up-shift (β > 1):** every `1/(β − 1)` periods the nearest-mark rule picks the same analysis grain twice in a row. The waveform "stalls" for a cycle, and any noise in that grain is duplicated.
- **Down-shift (β < 1):** the rule skips a grain. The waveform "jumps" two cycles' worth of evolution in one.

The Antares patent describes exactly this as resampling plus "one cycle period" being subtracted from the output pointer, "a cycle of data" being dropped ([US 5,973,252](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5973252)). *Sourced.*

### Why it reads as a vibrato problem

Pitch correction usually moves a note by tens of cents, and that is where `f_rep` lands in the vibrato band:

| F0 (Hz) | +10 c | +25 c | +50 c | +100 c | +200 c | +300 c | Shift where `f_rep` = 4–8 Hz (up / down) |
|---|---|---|---|---|---|---|---|
| 110 | 0.6 | 1.6 | 3.2 | 6.5 | 13.5 | 20.8 | 62–122 c / 64–131 c |
| 165 | 1.0 | 2.4 | 4.8 | 9.8 | 20.2 | 31.2 | 41–82 c / 42–86 c |
| 220 | 1.3 | 3.2 | 6.4 | 13.1 | 26.9 | 41.6 | 31–62 c / 32–64 c |
| 330 | 1.9 | 4.8 | 9.7 | 19.6 | 40.4 | 62.4 | 21–41 c / 21–42 c |
| 440 | 2.5 | 6.4 | 12.9 | 26.2 | 53.9 | 83.3 | 16–31 c / 16–32 c |

*Table values are events per second for up-shifts; down-shifts are slightly lower. Derived.*

A periodic irregularity at about 6 Hz on top of a 5.5 Hz vibrato is easy to hear as a vibrato that wobbles or speeds up, even though the measured F0 rate is unchanged. That is my interpretation of your observation, not something I tested with listeners.

### What I measured

- **Not a steady line.** After removing the vibrato, the component at `f_rep` was 0.10, 0.03, 0.22 and 0.01 cents (clean source, +50, −50, +100, +300 cents) and at most 0.23 cents with jitter, shimmer and noise. In level it was at most 0.038 dB. This is too small to be what you hear as a periodic modulation. *Simulated.*
- **The events themselves.** I compared the waveform change between consecutive output cycles at repeat and drop points against ordinary pairs (`D`, defined in section 5). A ratio of 1.0 means no hiccup. *Simulated.*

| Grain selection | Repeat junctions | Drop junctions |
|---|---|---|
| Nearest analysis grain (standard) | 0.68–0.92 (the waveform stalls) | 0.93–1.23 (the waveform jumps) |
| Two neighbouring grains cross-faded by fractional position | 0.86–1.11 | 0.82–1.11 |

The ranges span clean and noisy sources, +50, −50 and +100 cents, and 0 and 0.2 ms of pitch-mark error. In my test the effect is modest (up to about 30% off normal) and cross-fading narrows it without removing it. I could not separate the effect of pitch-mark error from the effect of repeats; the 0.2 ms cases did not show a clear trend.

### Why real voices may be worse (untested)

Repeated grains duplicate noise realizations, which is how the lecture notes explain the tonal noise in repeated unvoiced segments ([Illinois notes](https://courses.grainger.illinois.edu/ece420/5_PSOLA.pdf)). Breathy, rough or consonant-adjacent material has far more cycle-to-cycle variation than my synthetic vowel, so the stall and jump at each junction should be larger. I have not tested this on real recordings.

## 5. The AM side effect

In a real voice the harmonics move across fixed resonances, so each partial's level follows the vibrato in a pattern set by where it sits relative to a formant. PSOLA copies waveform grains from the original, so the output's level pattern is inherited from the original harmonics in time and re-interpolated at the new harmonic positions, not recomputed from the new geometry.

I compared the PSOLA output with a fresh synthesis of the same vowel and vibrato at the new pitch:

| Source F0 | First formant bandwidth | Mean error in AM at the vibrato rate (dB), harmonics 2–8 below about 2.2 kHz | Control AM at that rate (dB) | Phase-inverted harmonics |
|---|---|---|---|---|
| 110 Hz | 80 Hz | 0.07–0.83 | 1.1–1.6 | 0 or 1 of 6–7 |
| 110 Hz | 250 Hz | 0.05–0.14 | 0.4–0.6 | 0 of 4–5 |
| 233 Hz | 80 Hz | 0.69–0.78 | 1.4–1.8 | 0 to 3 of 4–7 |
| 233 Hz | 250 Hz | 0.25–0.26 | 0.8–1.0 | 0 or 1 of 3–5 |

*Shifts of +2, −3 and +5 semitones. Simulated.* At 0 semitones the PSOLA output matched the control. The mismatch grows with F0 and with narrow formants, which is consistent with (not proof of) sparse harmonics giving PSOLA a poor estimate of the envelope between them. In a case where the original had a harmonic sitting on the first formant (AM at twice the vibrato rate), the 2× AM component of about 0.65 dB stayed in the output at +2 semitones where the control had about 0.12 dB.

At 110 Hz with a wide first formant the error was mostly under 0.4 dB. I did not test whether any of this is audible.

## 6. How to tell which problem you have, and how to compensate

### Diagnostics

1. **Duration ratio**, output over input. It should be 1.000 within one period. If not, the time map is not duration-preserving.
2. **Vibrato rate ratio**, `f_v,out / f_v,in`. Fit a sinusoid to the cents contour (rate, extent, linear trend) over the sustained part; trim 0.3 s at each end.
3. **Extent ratio** in cents.
4. **Repeat or drop rate**, `F0 · |β − 1|`, and whether it is in 3–9 Hz.
5. **Junction ratio.** For each output cycle between synthesis marks, resample to a fixed length (200 points) and compute `D_j = RMS(c_{j+1} − c_j) / RMS_mean_amplitude`. Take the mean `D` at pairs that straddle a repeat or a drop and divide by the median `D` at ordinary pairs.
6. **AM check** (optional, for high voices): demodulate the harmonic nearest the first formant using the synthesis-mark phase and compare its AM with a rendering that recomputes the envelope.

| Finding | Likely cause |
|---|---|
| Duration ≠ 1 and rate ratio ≈ duration ratio | Index-driven map or an uncancelled stretch (section 3) |
| Rate unchanged, extent in cents changed | Additive Hz shift, tracker smoothing, or a pitch contour edited in Hz |
| Rate and extent fine, audible stutter, `f_rep` in 3–9 Hz, junction ratios outside the band in section 7 | Grain repeat or drop |
| Rate and extent fine, no junction effect, vibrato "feels different" on a high voice | AM mismatch |

### Compensation, ranked

| # | Recipe | Status |
|---|---|---|
| 1 | **Make the time map local and identity.** For constant duration use `τ(s) = s`, and derive the synthesis period from the analysis period at that time. Confirm with diagnostics 1 and 2. | Simulated: rate held at 5.500 Hz |
| 2 | **Shift the note-level pitch, not the vibrato.** Split the cents contour into a note-level part and a vibrato part, move only the first, and re-impose the vibrato. A zero-phase moving average whose length is exactly one vibrato period removes the vibrato term (rounding the length to whole samples cost about 1.7 cents of extent in my test). To flatten a drifting, flat note to its target: set `F0_target(s) = F0_note · 2^(vib(s)/1200)`, synthesize with `τ(s) = s`. | Simulated: rate 5.498 Hz, extent ±58.1 cents (input ±59.8), mean pitch error +0.2 cents. A naive instant ratio `F0_note / F0_in(s)` removed the vibrato completely (extent 0). Supported by [Baran et al. 2010](https://www.ll.mit.edu/r-d/publications/preserving-character-perturbations-scaled-pitch-contours) and [Rossignol et al. 1999](https://dafx.de/paper-archive/details/N2Kpz2NdMSLXkSnquj5qQw) (extract vibrato from the f0 trajectory, leaving a non-vibrato contour) |
| 3 | **Cross-fade neighbouring grains by fractional source index** instead of copying the nearest. Both grains are aligned on the same synthesis mark and weighted `1 − α` and `α`. | Simulated: junction ratios moved from 0.68–1.23 to 0.82–1.11. Reduces the hiccup, does not remove it |
| 4 | **Put repeats and drops where grains are most alike** (for example, search a few periods around the nominal source grain for the best waveform match to the previous output cycle, as WSOLA does at sample level). | Untested; reasoning only |
| 5 | **Keep pitch marks accurate.** Displaced window centres add hoarseness. | Sourced (lecture notes); I did not isolate it in simulation |
| 6 | **Avoid grain-based shifting in the hazard zone** (section 4 table) for sustained, clean singing. Use a phase-locked vocoder, harmonic model or WORLD-style path for shifts under about a semitone. | Untested here; see the earlier vocal resynthesis report for the routing |
| 7 | **For high voices with narrow formants,** use an envelope-based method (resample a spectral envelope at the new harmonic frequencies) so the AM follows the new harmonic-formant geometry. | Untested; reasoning only |
| 8 | **If you want to change the vibrato rate on purpose,** set it in the F0 target. The F0 rate then follows exactly. | Simulated: 6.500 Hz achieved. The grains keep the original AM timing; that part is derived, not measured |

Two cautions:

- A stretch-then-resample shifter with a time-varying ratio (for example, pitch correction) needs both stages on the same time axis. If the resample ratio is looked up by input time but applied on the stretched axis, the time map drifts. *Derived; not tested.*
- You cannot fix a wrong vibrato rate after the fact without changing duration. Redo the shift with recipe 1 or 2, or re-impose the vibrato with recipe 2 on the already-shifted audio (the AM will still be at the old rate; derived).

## 7. Starting thresholds

| Check | Flag if | Basis |
|---|---|---|
| Duration ratio | outside 1.000 ± one period (≈ 5 ms) | Guess |
| Vibrato rate ratio ρ | `abs(ρ − 1) > 0.02` | Guess; I found no listening-test source for a rate threshold |
| Extent ratio (cents) | outside 0.95–1.05 | Guess |
| `f_rep` in the hazard band | 3–9 Hz and the shift is under about one semitone | Derived from the vibrato range in section 2 |
| Junction ratio (repeat or drop) | outside 0.85–1.15 | Derived from my simulation only: nearest-grain results reached 0.68 and 1.23, blended ones 0.82–1.11 |
| AM mismatch | mean error above about 0.5 dB at the vibrato rate, or any phase inversion at a harmonic with control AM above 0.3 dB | Derived from my simulation only; audibility untested |

## 8. What I simulated, and its limits

- **Signal.** A synthetic sung vowel: an impulse train with spectral tilt through four fixed formants (700, 1100, 2500 and 3500 Hz). Sinusoidal vibrato at 5.5 Hz, ±60 cents, 220 Hz centre (233.3 and 110 Hz for the AM tests), 4–8 s. Where noted, jitter 0.5%, shimmer 3% and noise at −25 dB were added.
- **Marks.** True glottal closure instants, so pitch-tracker error was not a variable.
- **Shifter.** TD-PSOLA with Hann windows two periods long, fractional-sample grain placement, and window-sum normalization so the overlap-add did not itself create amplitude modulation.
- **Measurements.** Vibrato rate and extent from a sinusoid fit to the cents contour of the synthesis marks, cross-checked on the rendered audio with a YIN-style tracker (the tracker underestimates extent slightly: ±54.6 against ±59.8 cents on the input).
- **Not done.** No real recordings, no consonants or transitions, no listening tests, and no formant interaction (the source and filter were non-interacting).

## Sources

**Read in full or in the relevant part**

- [Illinois ECE 420 PSOLA lecture notes](https://courses.grainger.illinois.edu/ece420/5_PSOLA.pdf): mark mapping, β and γ, repeated and dropped segments, window-length artifacts.
- [KAIST slides, Time Scale Modification and Pitch Shifting](https://mac.kaist.ac.kr/~juhan/gct535/Slides/13%20Time%20Scale%20Modification%20and%20Pitch%20Shifting.pdf): stretch-and-resample versus PSOLA shifting.
- [Baran, Malyska and Quatieri, "Preserving the character of perturbations in scaled pitch contours," ICASSP 2010](https://dsp-group.mit.edu/wp-content/uploads/2024/11/TomICASSP.pdf) (abstract: [MIT Lincoln Laboratory](https://www.ll.mit.edu/r-d/publications/preserving-character-perturbations-scaled-pitch-contours)).
- [Maher and Beauchamp, "An investigation of vocal vibrato for synthesis," Applied Acoustics 30, 1990](https://exponent.montana.edu/rmaher/publications/maher_applac_0290_219-245.pdf).
- [van den Berg et al., "Co-modulation of F0 and formant frequencies in singing," ICPhS 1995](https://www.coli.uni-saarland.de/Phonetics/icphs/ICPhS1995/13_ICPhS_1995_Vol_1/p13.1_230.pdf).
- [Arroabarren and Carlosena, "Vibrato in singing voice: the link between source-filter and sinusoidal models," EURASIP JASP 2004](https://asp-eurasipjournals.springeropen.com/articles/10.1155/S1110865704401127) (full text via the [Universidad Pública de Navarra repository](https://academica-e.unavarra.es/bitstreams/4a71a939-859f-4390-a8b2-ce2185370809/download)).
- [Lester-Smith, Derrick and Larson, Journal of Voice, 2021](https://pmc.ncbi.nlm.nih.gov/articles/PMC8995401).
- [US 5,973,252, pitch detection and intonation correction](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5973252) and [US 7,974,838, pitch adjusting vocals](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7974838).

**Abstract or summary only**

- [Rossignol et al., "Vibrato: detection, estimation, extraction, modification," DAFx 1999](https://dafx.de/paper-archive/details/N2Kpz2NdMSLXkSnquj5qQw).
- [Molina Villota, D'Alessandro and Perrotin, "Dynamic pitch warping for expressive vocal retuning," DAFx 2023](https://dafx.de/paper-archive/details/sWdlau7ZOhe1JxhbaCEpPg): reports that its method preserves vibrato better than Auto-Tune on large vibratos.
- [Maximum acceptable vibrato excursion as a function of vibrato rate (DTU)](https://orbit.dtu.dk/en/publications/maximum-acceptable-vibrato-excursion-as-a-function-of-vibrato-rat/).

I looked for a paper that directly reports PSOLA changing the vibrato rate and did not find one. The rate mechanisms in section 3 and the hiccup mechanism in section 4 are derived from the sourced PSOLA mechanics and checked in my own simulation.
