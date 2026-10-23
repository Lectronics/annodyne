# PSOLA vibrato test bench

Scripts behind the numbers in `psola-vibrato-hiccups.md` (section 8 of that report describes the setup and its limits).

Requirements: Python 3, `numpy`, `scipy`. No other packages. Run any script from any folder; each one finds `psola_sim.py` next to it.

```
python3 exp1_time_mapping.py
```

## Files

| File | What it does | Report section |
|---|---|---|
| `psola_sim.py` | The bench: synthetic vowel with vibrato (`make_vowel`), time-map generators (`synth_identity`, `synth_index_driven`, `synth_additive_hz`, `synth_from_f0_target`), TD-PSOLA overlap-add (`psola_ola`) and the cross-fading variant (`psola_ola_blend`), and the measurements (`fit_vibrato`, `yin_f0`, `am_of_harmonic`). | all |
| `exp1_time_mapping.py` | Vibrato rate and extent after shifts of −5, −2, +2, +5, +12 semitones, with a duration-preserving map versus a one-to-one mark map. | 3 |
| `exp2_extent_and_am.py` | The +12 semitone one-to-one case with a wider fit band; extent after a ratio shift versus an additive-Hz shift; AM of harmonics 2–4 against a same-tract control at the new pitch. | 3, 5 |
| `exp3_am_vs_pitch_and_bandwidth.py` | AM mismatch (PSOLA versus control) for source F0 110 and 233 Hz and first-formant bandwidth 80, 150 and 250 Hz. | 5 |
| `exp4_repeat_rate_component.py` | Size of the sinusoidal component at the grain repeat or drop rate, nearest-grain versus cross-faded grains. | 4 |
| `exp5_junction_hiccups.py` | Cycle-to-cycle waveform change at repeat and drop junctions, nearest-grain versus cross-faded grains, with and without 0.2 ms of random pitch-mark error. | 4 |
| `exp6_pitch_correction.py` | Pitch correction of a flat, drifting note: naive instant ratio versus shifting only the note-level part and keeping the vibrato; then setting a new vibrato rate on purpose. | 6 |

Run times are roughly 3 to 30 seconds each on a laptop-class CPU.

Two output quirks to know about:

- In `exp1_time_mapping.py` the last row (+12 semitones, one-to-one map) reads 9.969 Hz because the default vibrato fit band stops at 9 Hz. `exp2_extent_and_am.py` refits it with a 3–15 Hz band and gets 11.000 Hz, which is the value in the report.
- In `exp6_pitch_correction.py` the "std of residual" column is just the standard deviation of the output contour, so it equals the vibrato size when the vibrato is kept (about 42 cents for ±60) and zero when it is flattened.

## Using your own audio

The bench uses exact pitch marks from the synthesizer. For a recording you need your own marks: pass a sorted array of pitch-mark times in seconds (ideally glottal closure instants) as `marks`, and the audio as `x` (mono, sample rate `FS` = 44100 in `psola_sim.py`; change `FS` if yours differs). Then:

```python
sm, src = synth_identity(marks, lambda s: beta)      # duration-preserving shift by ratio beta
y  = psola_ola(x, marks, sm, src)                    # nearest-grain copy (standard)
y2 = psola_ola_blend(x, marks, sm)                   # cross-fade neighbouring grains
tg, c, _ = f0_cents_from_marks(sm)                   # output F0 contour in cents
rate, extent, resid = fit_vibrato(tg, c)             # vibrato rate (Hz) and extent (+- cents)
```

Mark errors, missing marks and unvoiced segments are not handled; the bench assumes a clean sustained voiced note. Treat the thresholds in the report as starting values and re-measure them on your material.
