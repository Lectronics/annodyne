# Onset-detection test bench

Scripts behind the numbers in `onset-detection-research.md` (section 9). Synthetic signals with known onset times, scored at a ±25 ms tolerance.

Requirements: Python 3 with `numpy` and `scipy`; a C compiler; a C++17 compiler; aubio built from source (CMake, no optional dependencies needed). `gen.py` imports the vowel synthesizer from the earlier PSOLA test bench (`psola_vibrato_sim/psola_sim.py`); edit the `sys.path.insert` line near the top if you keep it somewhere else.

## Order

```
python3 gen.py sig                      # writes sig/*.wav, sig/*.f32 and sig/truth.json (needs the folder: mkdir sig)
# build aubio, then:
gcc -O2 -I<aubio>/src -I<aubio-build>/src aubio_onsets.c -L<aubio-build>/src -laubio \
    -Wl,-rpath,<aubio-build>/src -lm -o aubio_onsets
python3 run_all.py                      # runs every method on every signal -> detections.json
python3 score.py 0.025                  # TP/FP/FN table at +-25 ms, plus the consonant/vowel table
python3 sweep.py                        # pooled F at default vs best threshold per method
python3 level.py                        # same detectors with the input level lowered 0..-40 dB
g++ -std=c++17 -O2 cpp/sf_cli.cpp -o cpp/sf_cli      # the C++ SuperFlux (header-only: cpp/superflux.hpp)
```

## Files

| File | What it is |
|---|---|
| `gen.py` | The test signals: A sharp attacks with vibrato, A0 the same without vibrato, B 80 ms soft attacks, C legato pitch changes, D one note with vibrato and tremolo (one true onset), E consonant-vowel syllables (plosives and fricatives), F sharp attacks plus white noise at -30 dB, G fast notes (8 per second). |
| `aubio_onsets.c` | Small C driver around `aubio_onset_*`: reads raw float32, prints onset times in seconds. |
| `run_all.py`, `score.py`, `sweep.py`, `level.py` | Run, score, threshold sweep, level test. `sweep.py` and `level.py` reuse the scoring code in `run_all.py`. |
| `superflux_ref_patched.py` | Lines 1-680 of the CPJKU/SuperFlux `SuperFlux.py` (BSD 2-clause, notice kept in the file) with three compatibility edits: `np.float` to `float`, the `scipy.ndimage` import path, and `//` at the block-filter line (it relied on Python 2 integer division; without the fix the spectrogram comes out all zeros under Python 3). |
| `cpp/superflux.hpp`, `cpp/sf_cli.cpp` | Dependency-free C++17 SuperFlux and a command-line wrapper. Offline and online modes. Checked against the Python reference: identical detections on 15 of 16 signal/mode pairs, and the activation matches to about 5e-6 when both read the same 16-bit samples. |

## Limits

One vowel synthesizer, white-noise consonants, no real recordings. Ground truth is the start of each amplitude ramp. Thresholds in `sweep.py` are tuned on the same signals they are scored on.
