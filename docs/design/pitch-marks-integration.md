# Getting pitch marks and F0 into the C++ project: SPTK, REAPER and Praat

Research and test report, 2026-10-21.

> **For Claude Code.** The project already has a TD-PSOLA core. This report covers only how to feed it pitch marks (and F0 / voicing) from SPTK, REAPER or Praat. Licensing is handled by the project owner and is not covered. Every claim is tagged **Read** (I read the cloned source), **Ran** (I built and ran it here), or **Not tested**. Everything I ran used a synthetic vowel with known pitch marks, not real singing.

## Summary

- **Use SPTK's REAPER wrapper as the pitch-mark source.** It is one C++ call, returns voiced pitch marks in seconds, and gave pitch marks identical to REAPER's own command-line tool on the same signal. It also bundles WORLD, other F0 trackers and resamplers, so one dependency covers the pitch-mark path, the two-tracker octave check and the vocoder path. *Read, Ran.*
- **Use the REAPER command-line tool only as a debugging oracle,** or vendor REAPER directly if you want no SPTK. REAPER is archived, and its header's example calling sequence is stale (section 3). *Read, Ran.*
- **Use Praat as a reference and A/B check, not as an embedded dependency.** Both routes work, but it is the heaviest of the three. A headless command-line build driven by a script gave 874 marks with the same vibrato preserved in my PSOLA bench. Linking Praat's static libraries into a C++ program also works, after one setup call (`NUMmachar ()`), and returned identical marks. The cost is a build of more than 30 minutes on 2 cores and about 190 MB of static archives. *Ran.*
- **Do not smooth the marks.** REAPER's marks land on whole samples, which looks like noise, but feeding them straight into my PSOLA bench kept the vibrato rate and extent the same as with exact marks. Smoothing them cost extent. *Ran.*

## 1. What the PSOLA core needs from the analysis stage

| Need | Why |
|---|---|
| A sorted list of voiced pitch-mark times, one per glottal cycle | Grains are centred on marks. A constant offset from the true closure instant is harmless; inconsistent phase between marks is not. |
| Voiced and unvoiced regions kept apart | Praat treats a gap of more than 20 ms between adjacent marks as voiceless (`MAX_T` in `fon/Manipulation.cpp`) and, per its manual, copies voiceless audio straight across instead of pitch-shifting it. *Read.* |
| F0 contour (optional) | Useful for choosing shift ratios and for the two-tracker octave check in the earlier resynthesis report. |

REAPER and SPTK give the first two directly. Their marks in unvoiced regions are filler pulses at a fixed interval, so use the voiced flag (REAPER CLI) or the voiced-only list (SPTK).

## 2. SPTK (recommended)

**What it is.** A C++ speech-processing library, version 4.4 per its `CMakeLists.txt`; the latest commit in my clone is dated 6 October 2026. It builds a static library target named `sptk` and bundles REAPER, WORLD (DIO, Harvest, CheapTrick, D4C, synthesis), SWIPE', Snack (RAPT), SpeexDSP, libsamplerate and r8brain. *Read.*

**The call.** `sptk::PitchExtractionByReaper` in `SPTK/analysis/pitch_extraction_by_reaper.h`:

```cpp
#include "SPTK/analysis/pitch_extraction_by_reaper.h"

// x: mono samples as doubles, scaled to the int16 range (about +-32767)
sptk::PitchExtractionByReaper reaper(
    /*frame_shift=*/ static_cast<int>(sr * 0.005),   // F0 frame hop in samples
    /*sampling_rate=*/ sr,
    /*lower_f0=*/ 60.0, /*upper_f0=*/ 500.0,
    /*voicing_threshold=*/ 0.9);                     // REAPER's "unvoiced cost"
if (!reaper.IsValid()) { /* bad parameters */ }

std::vector<double> f0, epochs;                      // f0 per frame (0 = unvoiced); epochs in seconds
sptk::PitchExtractionInterface::Polarity polarity;
bool ok = reaper.Get(x, &f0, &epochs, &polarity);
```

*Read, Ran* (this exact usage, with a raw int16 file as input).

**Behaviour to know about** (all *Read* in `src/analysis/pitch_extraction_by_reaper.cc`):

- **Input scaling.** The wrapper casts each double to `int16_t` with `static_cast`, which truncates and is undefined for out-of-range values. Scale and clip to about ±32767 first; do not pass ±1.0 floats.
- **Valid parameters.** Sample rate above 6000 Hz and at most 98000 Hz; lower F0 above 10 Hz; upper F0 above lower F0 and below half the sample rate. Anything else makes `IsValid()` false.
- **Epochs are voiced only,** in seconds. F0 is resampled to your frame hop.
- **Stateless and offline.** Each `Get` call builds a fresh tracker over the whole waveform.
- **Only REAPER fills `epochs`.** The DIO, Harvest, SWIPE' and RAPT wrappers leave it untouched (each has a `// nothing to do` branch for it), so use them for F0 cross-checks only.

**Build and link.**

```cmake
set(SPTK_BUILD_COMMANDS OFF CACHE BOOL "" FORCE)
set(SPTK_INSTALL OFF CACHE BOOL "" FORCE)
set(SPTK_INSTALL_DRAW_COMMANDS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SPTK)          # or FetchContent / ExternalProject
target_link_libraries(your_target PRIVATE sptk)
```

- **Ran:** the library-only build (`cmake -DCMAKE_BUILD_TYPE=Release -DSPTK_BUILD_COMMANDS=OFF -DSPTK_INSTALL=OFF -DSPTK_INSTALL_DRAW_COMMANDS=OFF`) took 52 s on a 2-core container. A test program compiled with `g++ -std=c++11 -I src/include prog.cc build/libsptk.a`, no other flags.
- **Read:** the `sptk` target publishes four include directories to its users (`include`, `third_party`, `third_party/REAPER`, `third_party/WORLD`). Check for header-name collisions in your project. SPTK sets its own compile flags and definitions inside its directory scope only.

**Third-party parts inside SPTK** (licence files *Read*): REAPER Apache-2.0, WORLD three-clause BSD-style, SWIPE' MIT, Snack Tcl/Tk-style, SpeexDSP BSD-style, libsamplerate BSD-style, r8brain MIT; SPTK itself Apache-2.0.

**Why it is the best fit here**

- One dependency gives the pitch-mark path (REAPER), a second tracker for the octave-ratio metric (Harvest, SWIPE' or RAPT) and the vocoder path (WORLD) for small shifts.
- Its resamplers let you run the tracker at a lower rate than your audio. REAPER's README says higher rates cost quadratically more for little gain, but see section 6 on how coarse marks get at 16 kHz.
- It is actively maintained; the standalone REAPER repository is not.

## 3. REAPER directly

**What it is.** Google's epoch tracker (GCI, voicing, F0) in C++ with CMake, Apache-2.0. The repository was archived in February 2023, and the last commit in my clone is from December 2019. *Read.*

**Build and run.** Three static libraries (`core`, `wave`, `epoch_tracker`) plus the `reaper` program.

```sh
mkdir build && cd build && cmake .. && make
./reaper -i voice.wav -p voice.pm -f voice.f0 -a      # -a = ASCII output
```

*Ran.* CMake 3.28 builds it with a deprecation warning, because the file asks for CMake 2.8 compatibility. I did not try CMake 4.x. The program needs 16-bit PCM WAV input.

**Output format** (EST track files, `-a` for text; *Ran*):

```
EST_File Track
DataType ascii
NumFrames 874
...
EST_Header_End
0.004853 1 0.000000        # time(s)  voiced(1/0)  unused
```

The F0 file has the same layout with one line per 5 ms frame: `time voiced f0` (unvoiced frames show `-1`). Unvoiced stretches in the pitch-mark file are filled with marks 10 ms apart and flagged `0`.

**Embedding as a library.** The header comment shows `et.ComputeEpochs(...)`, but that function does not exist anywhere in the code (`grep` finds it only in the comment). The calling sequence that actually works is the one in `epoch_tracker_main.cc` and in SPTK's wrapper:

```cpp
EpochTracker et;
et.set_unvoiced_cost(0.9f);
et.Init(int16_samples, n, sample_rate, min_f0, max_f0, /*highpass=*/true, /*hilbert=*/false);
et.ComputeFeatures();
et.TrackEpochs();
et.GetFilledEpochs(0.01f, &times, &voicing);            // marks in seconds + voiced flags
et.ResampleAndReturnResults(0.005f, &f0, &correlations); // F0 per 5 ms frame
```

The `Init` overload that takes the samples is marked deprecated in the header, but it is the only one that exists. The header's note about inheriting Google command-line flags is also stale: no flag library is used. *Read.* Input is 16-bit integers only. If you use both SPTK and a vendored REAPER, expect duplicate symbols unless you namespace one (SPTK's copy lives in namespace `reaper` and renames its FFT file).

## 4. Praat

**What it is.** Mostly C++ with some C (needs C99 and C++17); the latest commit in my clone is dated 13 October 2026. *Read.*

**The functions you would care about** (all *Read*):

| Job | C++ function or script command | File |
|---|---|---|
| Pitch marks | `autoPointProcess Sound_to_PointProcess_periodic_cc (Sound, fmin, fmax)`; script: `To PointProcess (periodic, cc)...` | `fon/Sound_to_PointProcess.h`, `fon/praat_Sound.cpp` |
| Pitch marks, peak-based | `Sound_to_PointProcess_periodic_peaks (Sound, fmin, fmax, includeMaxima, includeMinima)` | same |
| PSOLA resynthesis | `Sound_to_Manipulation (Sound, timeStep, pitchFloor, pitchCeiling)`, `Manipulation_to_Sound (Manipulation, Manipulation_OVERLAPADD)`; script: `Get resynthesis (overlap-add)` (alias `(PSOLA)`) | `fon/Manipulation.h`, `fon/praat_uvafon_init.cpp` |
| Low-level PSOLA | `Sound_Point_Pitch_Duration_to_Sound (Sound, PointProcess pulses, PitchTier, DurationTier, maxT)` | `fon/Manipulation.h` |

**Three ways to use it**

1. **Command line, driven by a script (the realistic route).** The Makefile can build a headless executable: `make PRAAT_GRAPHICS=barren PRAAT_AUDIO=none` produces `praat_barren` with no GTK or audio libraries. The build compiles about 620 C++ and 1,080 C files and took more than 30 minutes on this 2-core container. Usage from `sys/praat.cpp`: `praat [--run] [OPTION]... SCRIPT-FILE-NAME [SCRIPT-ARGUMENT]...`, with `--no-pref-files` to avoid reading or writing preference files. A script that prints the marks to standard output:

   ```praat
   form Pitch marks
   	sentence in_file
   	real fmin 60
   	real fmax 500
   endform
   sound = Read from file: in_file$
   pp = To PointProcess (periodic, cc): fmin, fmax
   n = Get number of points
   writeInfoLine: "count ", n
   for i from 1 to n
   	t = Get time from index: i
   	appendInfoLine: fixed$ (t, 6)
   endfor
   ```

   invoked as `praat_barren --run --no-pref-files marks.praat voice.wav 60 500 > marks.txt`. *Ran: 874 marks in 0.14 s for the 4 s test vowel.*

   **Gotcha:** my first version of this script wrote the marks with `writeFileLine` and `appendFileLine`. Praat refused with "not allowed without --FULL-TRUST". Print to standard output and redirect, as above, or pass `--FULL-TRUST` if you want the script to write files (I did not try that flag).
2. **Link Praat's code into your program.** This works but needs care. The build makes one static archive per source directory (14 archives, about 193 MB in total here; for example `sys/libsys.a`, `kar/libkar.a`, `melder/libmelder.a`, `external/gsl/libgsl.a`, `external/clapack/libclapack.a`). I compiled a test program against Praat's headers and linked it with all 14 archives inside `-Wl,--start-group ... -Wl,--end-group` (the archives depend on each other), using `-std=c++17 -DUNIX -Dlinux -D_FILE_OFFSET_BITS=64 -DNO_GRAPHICS -pthread` and an `-I` for each `src/` subdirectory. The executable was 21.5 MB at `-O1`. *Ran.*

   ```cpp
   #include "Sound_to_PointProcess.h"
   #include "NUMmachar.h"

   NUMmachar ();   // must run once first; see below
   try {
       autoSound s = Sound_create (1, 0.0, n / sr, n, 1.0 / sr, 0.5 / sr);
       for (integer i = 1; i <= n; i ++) s -> z [1] [i] = pcm [i - 1] / 32768.0;
       autoPointProcess pp = Sound_to_PointProcess_periodic_cc (s.get (), 60.0, 500.0);
       // pp -> nt marks, times in pp -> t [1 .. pp -> nt]
   } catch (MelderError) {
       Melder_flushError ();   // Praat reports errors as exceptions
   }
   ```

   **Gotcha:** without the `NUMmachar ()` call my first build crashed with a segmentation fault. A debugger backtrace showed it in `NUMminimize_brent`, which reads `NUMfpp -> eps`, and `NUMfpp` (Praat's machine-constants table) is a null pointer until `NUMmachar ()` fills it. Praat's own start-up code normally does this. With the call, the program returned 874 marks, identical to the command-line run (maximum difference 0 s). I only tested this one function. Other Praat functions may need further start-up that I haven't found, so test each one you use.

   The functions take Praat's `Sound` and return `autoPointProcess`, so you convert at the boundary, and there is no installable library target. Check licence headers per file: `Manipulation.cpp`, `Sound_to_PointProcess.{h,cpp}` and `PointProcess.cpp` say "version 2 or later", while `Sound_to_Pitch.cpp` says "version 3 or later", and `Manipulation.cpp` includes `Sound_to_Pitch.h`. `Sound_to_PointProcess_periodic_cc` calls `Sound_to_Pitch` (it appears in the backtrace above), so the pitch-mark call itself pulls in a version-3-or-later file. *Read, Ran.*
3. **Read it as a reference.** Praat's manual describes its overlap-add as: generate target pulses from the pitch tier; treat gaps of more than 20 ms between original pulses as voiceless and copy those segments; match each target pulse to its nearest source pulse; copy a bell-windowed piece of source around it, with window half-lengths set by the smaller of the neighbouring periods. That nearest-source-pulse rule is the one behind the repeat-and-drop hiccups in the earlier vibrato report. *Read (manual and source).*

## 5. Results on the synthetic vowel

The signal: 4 s sustained vowel, 220 Hz centre, 5.5 Hz vibrato of ±60 cents, impulse train through four formants (the bench from the earlier report). Marks from the REAPER command-line tool at 44.1 kHz unless noted; the Praat row is the same clean signal. *Ran.*

| Case | Marks found | Voiced | Offset to nearest true closure instant | Per-period error vs true |
|---|---|---|---|---|
| Clean, 44.1 kHz | 874 (true: 879) | 100% | median 0.34 ms, std 0.01 ms | rms 3.7 cents, max 9.8 |
| Clean, 44.1 kHz, **Praat** (`periodic, cc`, 60–500 Hz) | 874 | n/a (marks only) | median 1.20 ms, std 0.041 ms | rms 3.6 cents, max 5.7 |
| Clean, 16 kHz | 874 | 100% | median 0.37 ms, std 0.02 ms | rms 9.9 cents, max 24.4 |
| Jitter 0.5%, shimmer 3%, noise at −25 dB, 44.1 kHz | 874 | 100% | median 0.34 ms, std 0.01 ms | rms 4.3 cents, max 11.3 |

- The constant 0.34 ms offset exists because my pulses are excited before the formant filters. PSOLA only needs a consistent offset.
- **Marks are whole samples.** For example 0.004853 s × 44100 = 214.0 and 0.009365 s × 44100 = 413.0. The per-period error above is that rounding: one sample is about 0.5% of a 220 Hz period at 44.1 kHz, and about 1.4% at 16 kHz.
- **Praat's marks are not whole samples.** They are fractional sample positions, which probably explains why its worst-case period error (5.7 cents) is lower than REAPER's (9.8); I did not test that. Its offset from the true instant is larger (1.20 ms against 0.34 ms) and has a bigger spread (0.041 ms against 0.01 ms), so it is marking a different point in the cycle. A constant offset is harmless for PSOLA, and the spread is small next to a 4.5 ms period.
- **Run time** on this 2-core container: 1.40 s for 4 s of audio at 44.1 kHz for REAPER, 0.24 s at 16 kHz; Praat took 0.12–0.14 s at 44.1 kHz, both through the command-line script and through the linked test program.
- **SPTK equals REAPER:** the SPTK wrapper returned 874 epochs, identical to the command-line tool's on the same signal (maximum difference 0.000000000 s), polarity positive, 802 F0 frames at a 220-sample hop.

**Feeding REAPER's marks into the PSOLA bench** (shifts of +50, −50 and +300 cents; vibrato fitted to the output contour):

| Marks | Rate | Extent (from synthesis marks) | Extent (YIN on the audio, +50 c) |
|---|---|---|---|
| True marks | 5.500 Hz | ±59.7 cents | ±54.8 |
| REAPER marks as written | 5.500 Hz | ±59.7 cents | ±54.7 |
| REAPER marks after a 5-point line fit | 5.500 Hz | ±58.2 cents | ±54.7 |
| Praat marks as written | 5.500 Hz | ±59.5 cents | not measured |

The YIN tracker underestimates extent by about 5 cents on this input regardless of marks. Conclusion: use REAPER's or Praat's marks unchanged; the 5-point fit removed 1.5 cents of real vibrato.

## 6. Practical notes for singing

- **F0 range.** REAPER's built-in search range is 40–500 Hz (`kMinF0Search`, `kMaxF0Search`). Raise the upper bound for high voices and test it. I tested only 220 Hz. *Read; Not tested above 220 Hz.*
- **Material it was built for.** The README says REAPER targets studio-quality speech, is sensitive to phase distortion from filtering, and offers a Hilbert-transform option (`-t`) when marks look wrong. I did not test real singing, rough or breathy voices, or consonant transitions. *Read.*
- **Sample rate.** The README warns that rates below 16 kHz make the results coarser, and my 16 kHz run shows it (9.9 cents rms per period against 3.7). If you run the tracker at a low rate for speed, consider snapping each mark to the nearest waveform feature at full rate. *Not tested.*
- **Voicing and unvoiced regions.** Treat gaps above 20 ms between voiced marks as voiceless, as Praat does, and copy that audio instead of shifting it. *Read (Praat).*
- **Octave errors.** Run a second F0 tracker from the same SPTK library (Harvest, SWIPE' or RAPT) and compare, as in the earlier report's octave-ratio check. *Not tested.*

## 7. Decision table

| | SPTK | REAPER (vendored or CLI) | Praat |
|---|---|---|---|
| Pitch marks as a C++ call | Yes, `PitchExtractionByReaper::Get` (Ran) | Yes, `EpochTracker` methods (Ran via CLI and via SPTK) | Yes, `Sound_to_PointProcess_periodic_cc`, on Praat's own classes, after `NUMmachar ()` (Ran; `_periodic_peaks` not run) |
| Build effort here | 52 s, library only | under a minute | over 30 minutes on 2 cores for about 1,700 files; 14 static archives, about 193 MB |
| Maintenance | Active (commit 6 Oct 2026) | Archived (last commit 2019) | Active (commit 13 Oct 2026) |
| Extras | WORLD, Harvest, SWIPE', RAPT, resamplers | none | Reference overlap-add code; scripting |
| Main risk | Wrapper casts input to int16 | Stale header docs; CMake 2.8 | Not designed as an embeddable library: no install target, undocumented start-up calls, mixed licence headers |

## Sources

All from the cloned repositories at the dates above, not from web pages: `github.com/sp-nitech/SPTK` (README, `CMakeLists.txt`, `include/SPTK/analysis/`, `src/analysis/pitch_extraction_by_reaper.cc`, `third_party/*/LICENSE`), `github.com/google/REAPER` (README, `epoch_tracker/epoch_tracker.h`, `epoch_tracker_main.cc`, `CMakeLists.txt`), `github.com/praat/praat` (`Makefile`, `fon/Manipulation.{h,cpp}`, `fon/Sound_to_PointProcess.h`, `sys/praat.cpp`, `fon/praat_Sound.cpp`, `fon/praat_uvafon_init.cpp`, `dwsys/NUMmachar.{h,cpp}`, `dwsys/NUM2.cpp`). The archive status of REAPER (February 2023) comes from its GitHub page.
