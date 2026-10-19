# Audio Verification Protocol

People will listen closely to every correction this software makes. Code review and unit tests can't tell you whether a pitch shift warbles or a consonant smears. This protocol has two halves:

- **Objective checks**: automated, run by Claude Code and CI on every change that affects audio. They catch structural mistakes (wrong pitch, clicks, changed audio where nothing was edited).
- **Human listening**: a person auditions before/after files at each audio milestone. Only a human can sign off that something sounds right. Claude Code prepares the material and never claims audible quality itself.

---

## 1. Objective checks (`tests/audio/`, label `audio`)

Each check is a Catch2 test that renders a scenario and measures the output. Starting tolerances are below; they live in `tests/tolerances.h` and change only with a human's agreement.

| Check | What it proves | How | Starting tolerance |
|---|---|---|---|
| **Null test** | Empty edit list returns the source | Render, compare samples | Bit-exact |
| **Undo null test** | Edits followed by full undo return the source | Apply N random edits, undo all, render | Bit-exact |
| **Untouched regions** | Edits don't leak into other notes | Compare samples outside `dirty_regions()` + crossfade zones | Bit-exact |
| **Pitch round-trip** | The requested pitch is what comes out | Render, re-run `detect_pitch` + `perceived_pitch` on the edited note | ±3 cents of requested |
| **Vibrato preservation** | Shifting a note keeps its vibrato | Compare vibrato rate/extent before vs after a pure pitch offset | Rate ±0.2 Hz, extent ±10% |
| **Modulation/drift scaling** | Scale controls do what they say | Measure modulation/drift after `mod_scale`/`drift_scale` edits | ±10% of requested |
| **Timing round-trip** | Notes start where requested | Re-detect onsets after time edits | ±2 ms |
| **Length check** | Output duration matches the TimeMap | Compare output length to `TimeMap` prediction | ±1 sample |
| **Level round-trip** | Gain edits produce the requested loudness | Re-measure note loudness | ±0.3 dB |
| **Formant round-trip** | Formant offset moves the envelope by the requested amount | Compare envelope peak positions before/after | ±3% frequency |
| **Formant preservation** | Pitch shift alone doesn't move formants | Envelope peaks after a ±7 semitone shift | ±5% frequency |
| **Noise not pitch-shifted** | Sibilants keep their spectrum | Spectral centroid of sibilant frames before/after a pitch shift | ±5% |
| **Click detector** | No discontinuities at crossfades and note boundaries | High-pass the output; flag samples where the jump exceeds a threshold relative to local RMS | 0 detections |
| **Sanity** | No broken samples | NaN/Inf scan, DC offset, peak level | 0 NaN/Inf; DC < −60 dBFS; peaks reported |
| **Determinism** | Same input, same output | Render twice, hash both | Identical hashes |
| **Performance** | Real-time playback is possible | Time `render_block` from cache; time background render per second of audio | Within budget set in Phase 9 |

**Rules for these checks**

- A failing check is fixed in the code, not by changing the tolerance. Loosening a tolerance requires a human decision recorded in an ADR.
- Every bug found by listening becomes a new scenario with an objective check where possible.

---

## 2. Scenarios

A scenario is a small JSON file in `tests/audio/scenarios/` that names a source file and a list of edits. The same file drives the automated checks and the audition package, so humans and tests always look at the same thing.

```json
{
  "name": "pitch_up_2st_vibrato",
  "source": "tests/golden/vocals/female_vibrato_a4.wav",
  "algorithm": "melodic",
  "edits": [
    { "category": "pitch", "op": "add", "param": "center_offset_cents",
      "value": 200, "target": { "note_index": 3 } }
  ],
  "expect": { "pitch_cents_delta": 200, "vibrato_preserved": true }
}
```

Scenario set to grow over the phases (one folder per tool):

- **Pitch**: ±1, ±2, ±7, ±12 semitones; on notes with wide vibrato, scoops and falls; low male and high soprano voices.
- **Correct Pitch**: 50% and 100% centre; drift 0/50/100%; tolerance on/off.
- **Modulation/drift**: 0%, 50%, 150%, −100% (inverted).
- **Timing**: move ±50 ms; stretch 0.5×, 1.5×, 2×; time handles; quantize to grid.
- **Formant**: ±200 cents alone; formant follow during ±12 st shifts.
- **Level**: ±6 dB gain, fades, Leveling 50/100%, sibilant −12 dB.
- **Torture**: fast melisma, breathy voice, vocal fry, sibilant-heavy lyrics, reverb tail, silence edges, very short notes (< 80 ms), clipping input.
- **Polyphonic (Phase 11+)**: change one note of a piano triad; major→minor on guitar; mute one note; shared-harmonic intervals (octaves, fifths).

---

## 3. Audition packages (for humans)

`melo-cli audition <scenario.json> --out audition/<phase>-<topic>/<scenario>/` writes:

```
original.wav            # the source region
edited.wav              # the render
original_matched.wav    # original loudness-matched to edited (fair A/B)
difference.wav          # edited − original, for spotting leaks (only meaningful without timing edits)
edits.json              # exactly what was applied
report.md               # objective check results, measured vs requested values, pass/fail
```

Plus one `INDEX.md` per package folder listing the scenarios, what to listen for in each, and links to the files.

Generate packages:
- at the end of every phase that changes audio,
- for any change to synthesis, render, edits or time-map code,
- whenever a human asks to hear something.

---

## 4. Human listening protocol

**Setup:** good headphones or monitors, a quiet room, same playback level for A and B (use `original_matched.wav`). Listen to each pair at least twice, once without looking at the report.

**For each scenario, rate 1–5 and note timestamps of any problem:**

| Listen for | What it sounds like | Usual cause |
|---|---|---|
| Phasiness | Hollow, distant, "underwater" | Phase vocoder without phase locking |
| Warble / flutter | Fast wobble that wasn't in the singer's voice | Pitch tracking errors, bad partial tracking |
| Metallic / robotic | Buzzy, synthetic edge | Harmonic model without enough noise, envelope too smooth |
| Chipmunk / giant | Timbre changes with pitch | Formants not preserved |
| Clicks / pops | Short ticks at note edges | Missing or too-short crossfades |
| Smeared consonants | "s", "t", "k" soft or doubled | Transients stretched |
| Lispy sibilants | "s" pitched or whistly | Noise part being pitch-shifted |
| Flams / doubling | Attack heard twice | Timing overlap at boundaries |
| Level jumps | Sudden loudness change at an edit | Gain without ramps, leveling errors |
| Wrong note feel | Technically on pitch but sounds off | Perceived-pitch weighting wrong |
| Unnatural vibrato | Too regular, wrong speed after edit | Modulation decomposition wrong |
| Bleed (polyphonic) | Edited note leaves a ghost, or neighbours change | Separation errors |

Fill in `docs/templates/LISTENING_SHEET.md` and save it as `docs/reviews/<phase>-listening.md`.

**Pass criteria for a phase:** no scenario rated below 3, no clicks, and no problem rated "obvious" on a moderate edit (±2 semitones, ±50 ms, ±6 dB). Larger edits (±12 st, 2× stretch) may show artifacts but should be noted, not hidden.

---

## 5. What Claude Code must do

- Run the objective checks and report the numbers.
- Build the audition package and point the human to `INDEX.md`.
- Never describe audio as sounding good, natural or transparent. Say "objective checks pass; awaiting human listening".
- Turn every human-reported problem into a scenario and, where possible, an objective check, before fixing it.
