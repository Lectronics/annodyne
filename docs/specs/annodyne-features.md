# Melodyne 5: Feature Summary, Full Feature List and Technical Theory

*Covers Celemony Melodyne 5 (the current major version; 5.4.x as of autumn 2026).*

> **A note on the "theory" sections.** Celemony has never published Melodyne's exact algorithms; they are proprietary (DNA is patented). The theory below explains the established signal-processing principles that each function relies on, and what Celemony has publicly said about how the program works. Read it as "how this kind of thing is done", not as a description of Melodyne's source code.

---

## Part 1: Short Summary

Melodyne is a **note-based audio editor**. Rather than treating a recording as a waveform, it analyses the audio, finds the individual notes, and draws each one as an editable "blob" on a piano-roll-style grid. You can then change any note's:

- **Pitch**: centre pitch, vibrato depth, pitch drift, and transitions between notes
- **Timing**: position, length, attack speed, and timing inside the note
- **Level**: volume per note, fades, and separately the level of breaths and sibilants
- **Timbre**: formants (vocal character), and in the top edition, individual overtones

It does this for **monophonic sources** (vocals, solo instruments) in every edition, and for **polyphonic sources** (piano, guitar, full chords) via its patented **DNA (Direct Note Access)** technology in the Editor and Studio editions.

Around that core sit **automatic macros** (Correct Pitch, Quantize Time, Leveling), **scale, chord and tempo detection**, a **Chord Track**, **audio-to-MIDI export**, **multitrack note editing** and an **overtone-based Sound Editor**. It runs stand-alone or inside a DAW, with deep integration via **ARA**.

### Editions at a glance

| Edition | Who it's for | Key additions |
|---|---|---|
| **essential** | Basic vocal fixes | Main Tool, pitch/time/level macros, all non-DNA algorithms, Chord Track |
| **assistant** | Professional vocal editing | Full toolset (vibrato, drift, formants, fades, sibilants, attack, etc.), Inspectors, Audio-to-MIDI |
| **editor** | Vocals + polyphonic instruments and samples | DNA polyphonic editing, extended scale and tempo functions |
| **studio** | Multitrack professionals and sound designers | Multitrack note editing, cross-track macros, quantize to reference track, Sound Editor |

---

## Part 2: Full Feature List with Technical Theory

### 1. Audio Analysis and Note Detection (the foundation)

**What it does:** When audio is transferred into Melodyne, it is analysed and split into notes. Each note becomes a "blob" whose vertical position is pitch, horizontal position and width are time and duration, and thickness reflects amplitude. A thin line through each blob shows the actual pitch curve (the fluctuating pitch the singer actually produced).

**Technical theory:**
- **Fundamental frequency (f0) estimation.** The program must find the pitch of the signal moment by moment. Common methods are time-domain (autocorrelation, or YIN, which looks for the lag at which a waveform best repeats itself) or frequency-domain (finding the series of evenly spaced harmonics in the spectrum, e.g. a harmonic sieve or harmonic product spectrum). For the voice, the result is a continuous pitch track.
- **Onset detection and segmentation.** Note boundaries are found by looking for sudden changes in energy, spectral content (spectral flux), or a jump in pitch. The pitch track is then grouped into segments that each represent one intended note.
- **Musical pitch assignment.** Each segment gets a single "note pitch" even though the real pitch wobbles. Celemony describes this as *musically weighted*: the perceived pitch of a note is not a simple average of its pitch curve. Listeners weight the stable, sustained middle of a note more than scoops at the start or falls at the end, so the analysis tries to model that perception (see Correct Pitch macro below).
- **Analysis-resynthesis model.** After analysis, Melodyne no longer plays your audio directly; it plays a *resynthesis* driven by the analysed parameters. This is why edits can be undone at any time and why every edit is "non-destructive".

---

### 2. Detection Algorithms

Melodyne picks an algorithm automatically, or you can choose one. All editions include all algorithms except DNA.

| Algorithm | Use | Technical theory |
|---|---|---|
| **Melodic** | Vocals, monophonic instruments | Assumes one pitch at a time. Uses an f0 tracker and a harmonic (sinusoidal) model. In Melodyne 5 it also separates the *noise* parts of the signal (sibilants, breaths) from the pitched parts. |
| **Percussive** | Drums, unpitched material | Ignores pitch; detects transients/onsets only. Notes are positioned on time; pitch editing becomes plain transposition. |
| **Percussive Pitched** (new in v5) | Pitched percussion: toms, 808s, tuned drums, kalimba | Combines transient detection with pitch estimation of the resonant tail, so drums can be tuned per hit. |
| **Polyphonic Sustain** (DNA) | Pads, organ, strings, sustained chords | Multi-pitch estimation where notes may hold for a long time. |
| **Polyphonic Decay** (DNA) | Piano, guitar, plucked/struck chords | Multi-pitch estimation that expects each note to start loud and decay. |
| **Universal** | Mixes, complex material, anything | No note separation inside chords; time-stretches and transposes the material as a whole while preserving quality. Good for whole mixes and loops. |

**Theory behind the split:** Different sources violate different assumptions. A voice is mostly harmonic with one f0; drums are mostly noise and transients; a piano chord contains many overlapping harmonic series. Each algorithm uses a model suited to its material, which is why choosing the right one makes a large difference to quality.

---

### 3. Editing Note Assignments ("Note Assignment Mode")

**What it does:** If the analysis gets something wrong (wrong octave, a note split in two, two notes merged), you can correct the detection itself: move notes between octaves, add or remove note separations, and in DNA mode activate or deactivate notes the analysis considered unlikely (shown as empty "ghost" blobs).

**Technical theory:** Pitch detectors commonly make **octave errors**, because a signal with f0 = 220 Hz also repeats every 1/110 s and has energy at 440 Hz. Polyphonic detection is a probabilistic decision about which harmonic peaks belong to which note. Note Assignment Mode lets a human override those decisions before the signal is resynthesised. Changes here alter how the audio is *interpreted*, not the sound itself.

---

### 4. The Main Tool (all editions)

**What it does:** A single context-sensitive tool. Drag a note vertically to change pitch, horizontally to move it in time, drag its edge to change length, double-click to snap it to the nearest semitone, and work with note separations. Comes with a **pitch grid** (chromatic, scale, or off) and a **time grid**.

**Technical theory:**
- **Pitch shifting with time preserved:** Changing pitch without changing speed requires separating the two. In a harmonic model, every partial's frequency is multiplied by the ratio `2^(semitones/12)`. Time-domain methods like PSOLA (pitch-synchronous overlap-add) do the same by cutting the waveform into single-period grains around each pitch pulse and overlapping them closer together (higher pitch) or further apart (lower pitch).
- **Time stretching with pitch preserved:** The opposite: grains or analysis frames are repeated or dropped (time domain), or a phase vocoder resynthesises the spectrum at a different rate while keeping phases coherent. Note-based stretching is better than whole-file stretching because it can stretch the stable part of a note while leaving the consonant/attack untouched.
- **Snap to grid:** Rounding the note's perceived pitch to the nearest equal-tempered (or scale) value: `n = round(12·log2(f/f_ref))`.

---

### 5. Pitch Tools (assistant and above)

#### 5a. Pitch Tool
**What it does:** Moves notes in pitch; double-click snaps to the grid; Alt/Option-drag for free movement. Also edits **note transitions**: the glide (portamento) between consecutive notes can be made faster or slower.

**Theory:** Adjusting the transition means reshaping the pitch curve around the note boundary, effectively interpolating the f0 trajectory between the end of one note and the start of the next over a shorter or longer time.

#### 5b. Pitch Modulation Tool
**What it does:** Scales the **vibrato and fast pitch deviation** inside a note: reduce it to flatten a wobbly note, increase it for more expression, or invert it.

**Theory:** The pitch curve of a note is split into a **centre pitch** plus a **deviation** signal. The fast part of the deviation (vibrato is typically 4–7 Hz in rate, ±20–100 cents in depth) is multiplied by a factor. 0% gives a "robotic" straight line; 100% is untouched; >100% exaggerates the vibrato.

#### 5c. Pitch Drift Tool
**What it does:** Corrects the **slow** pitch movement within a note (a note that starts in tune and sags flat, for example) without touching the vibrato.

**Theory:** The deviation signal is split again by frequency: a slow component (drift, below the vibrato rate) and a fast component (vibrato). This is essentially low-pass vs high-pass filtering of the pitch curve. Pitch Drift scales only the slow component, so the singer's vibrato survives a correction that removes sagging.

#### 5d. Correct Pitch Macro (all editions)
**What it does:** Automatic tuning. Two sliders: **Pitch Center** (pulls each note's centre toward the target pitch by a percentage) and **Pitch Drift** (straightens slow drift by a percentage). Can be restricted to notes that are further out of tune. Targets the active scale or chord.

**Theory:** For each note, `new_offset = old_offset × (1 − amount)`. Because it moves the *note* rather than the instantaneous pitch (as real-time auto-tuners do), vibrato and scoops are preserved and there is no "Auto-Tune effect" unless you push drift and modulation to 100%. Melodyne 5's improved "musically weighted" analysis means the centre it corrects is closer to the pitch a listener actually perceives, so corrected notes sound in tune instead of just mathematically centred.

---

### 6. Formant Tool (assistant and above)

**What it does:** Shifts a note's **formants** (the resonances that give a voice or instrument its characteristic colour) independently of pitch. Use it to remove the "chipmunk" effect after big transpositions, to make a voice sound larger or smaller, or to vary doubled vocals. Formants follow pitch shifts automatically by default (i.e. are preserved).

**Technical theory:**
- **Source-filter model.** Speech and singing are modelled as a **source** (the vocal folds producing a harmonic buzz at f0) passed through a **filter** (the vocal tract, whose resonances are the formants F1, F2, F3...). Vowels are recognised by formant positions, not by f0.
- **Spectral envelope extraction.** The filter shape is estimated as the smooth envelope over the harmonics, typically with **linear predictive coding (LPC)** or **cepstral smoothing** (taking the log spectrum's low "quefrency" part).
- **Independent shifting.** To change pitch but keep timbre, harmonics move while the envelope stays put; each new harmonic amplitude is read from the original envelope at its new frequency. To shift formants, the envelope is stretched along the frequency axis while the harmonics stay put.

---

### 7. Amplitude Tool (assistant and above)

**What it does:** Changes the level of individual notes, or mutes them entirely (double-click).

**Theory:** In a resynthesis model, a note's gain is simply a multiplier on the amplitudes of its components. Because it acts per-note (and in DNA, per note *inside a chord*), it can do what a conventional fader or compressor cannot: rebalance one note of a chord or one syllable of a word.

---

### 8. Leveling Macro (all editions, new in v5)

**What it does:** Evens out note volumes automatically, with two controls: one reduces the difference between loud and quiet *notes* (average level), the other smooths level changes *within* notes. Described as a musically aware alternative to compression or volume automation.

**Theory:** Each note's average level (RMS or loudness in dB) is measured, and a correction gain is calculated to move it toward the target by a percentage: `gain_dB = (target − level) × amount`. Working per note avoids typical compressor side effects such as pumping, attack/release artifacts and level changes that ignore musical boundaries.

---

### 9. Fade Tool (assistant and above, new in v5)

**What it does:** Draw fade-ins and fade-outs on individual notes, even notes within polyphonic chords, e.g. to soften a harsh start or let a note tail away.

**Theory:** An amplitude envelope (a gain curve over time) is applied to the note's resynthesised components before they are summed. Since notes are separated objects, a fade on one note does not affect overlapping notes.

---

### 10. Sibilant Balance Tool and Noise Components (assistant and above, new in v5)

**What it does:** Melodyne 5's Melodic algorithm identifies **sibilants** ("s", "sh", "t", "ch") and **breaths** and shows them as hatched parts of notes. Their level can be raised, lowered or removed independently of the pitched part, making Melodyne a precise de-esser and breath controller. Sibilants are also handled more naturally during pitch and time edits (they are not pitch-shifted, and they are stretched differently from vowels).

**Technical theory:**
- **Harmonic-plus-noise (or "deterministic + stochastic") decomposition.** The signal is split into a part that fits the harmonic model (vowels, voiced sound) and a residual that doesn't (noise). Sibilants are broadband, high-frequency, aperiodic noise; voiced sounds are periodic.
- **Classification features** typically include periodicity (low for noise), spectral centroid (high for "s" sounds) and zero-crossing rate (high for noise).
- **Why it matters:** In real singing, sibilants don't change pitch when the singer sings a higher note. Pitch-shifting them (as many tools do) creates lispy artifacts. Treating them separately is both more natural-sounding and lets you control level without a frequency-dependent de-esser that might also dull the vowels.

---

### 11. Time Tool and Time Handles (assistant and above)

**What it does:** Move notes in time, change their length, and (via **time handles**) stretch only part of a note, e.g. lengthen the vowel but leave the consonant at the start as it is. Also adjusts the timing of the transition between notes.

**Theory:** Time-stretching is applied in segments with different stretch factors. A time handle sets a breakpoint inside a note; the region before it and after it each get their own stretch ratio. This avoids the smeared, unnatural consonants that whole-note stretching would cause, because transients and consonants are kept at their original speed while steady-state vowels absorb the change.

---

### 12. Attack Speed Tool (assistant and above)

**What it does:** Makes the beginning of a note sharper (more percussive) or softer (more legato).

**Theory:** The transient (the first few milliseconds of a note) is time-compressed or expanded, and/or its amplitude envelope reshaped. Shortening the attack sharpens the onset; lengthening it smooths it, affecting perceived articulation.

---

### 13. Note Separation Tool (assistant and above; basic separation in essential)

**What it does:** Add, move or remove the boundaries between notes, and set the **separation type**: hard cut, or softer types allowing a smooth pitch slide between notes. Lets you split a note to tune its parts separately, or join notes so they move together.

**Theory:** Changing separations redefines the segments the resynthesis treats as units. A "soft" separation means pitch and amplitude are interpolated across the boundary (continuous trajectory); a hard separation allows discontinuities for clean independent movement.

---

### 14. Quantize Time Macro (all editions)

**What it does:** Moves notes toward the nearest time-grid position by a percentage, tightening timing while retaining feel. In studio, can quantize to the timing of a **reference track**.

**Theory:** Like pitch correction but on the time axis: each note's **onset** (as perceived, not the raw start of energy) is moved toward the grid point by `amount × distance`, and the neighbouring material is stretched/compressed to close gaps. With reference-track quantizing, the "grid" becomes the onset list of another track instead of a regular beat.

---

### 15. Inspectors (assistant and above)

**What it does:** A panel showing the numeric values of the selected notes (pitch in cents, formant offset, amplitude in dB, timing etc.), all editable directly.

**Theory:** Not a DSP feature, but it exposes the analysis model's parameters directly, which makes precise, repeatable edits possible (e.g. "+12 cents" rather than dragging by eye).

---

### 16. Pitch Grid, Scales and Tuning

**What it does:**
- **All editions:** Snap to chromatic or to a chosen key/scale; set the reference pitch (e.g. A = 440 Hz or 432 Hz).
- **editor and studio (Extended scale functions):** **Scale detection** from audio, a large library of scales, temperaments and world tunings, **custom scales** with any number of notes per octave and arbitrary cent values, and **scale transformations** (e.g. turn a melody from major to minor by mapping scale degrees). Also **Dynamic Just Intonation**, which retunes notes in real time to pure intervals relative to the current harmony.

**Technical theory:**
- **Equal temperament** divides the octave into 12 equal ratios of `2^(1/12)`; frequencies follow `f = f_ref · 2^(n/12)`. Other temperaments (Pythagorean, meantone, well-tempered) and non-Western scales use different cent tables (100 cents = one equal-tempered semitone).
- **Just intonation** uses small whole-number ratios (5/4 for a major third, 3/2 for a fifth) which sound "beatless" because harmonics of the two notes coincide. Fixed just intonation fails when you change key, so **dynamic** just intonation recalculates the target pitches against the current chord root as the music moves.
- **Scale detection** builds a pitch-class histogram (how much time is spent on each pitch class, and at what cent offsets) and matches it against scale templates, also revealing deviations from 12-TET.
- **Scale transformation** maps notes by scale degree rather than by fixed semitone amount, so a major third becomes a minor third while other degrees stay put.

---

### 17. Chord Detection, Chord Grid and Chord Track (all editions)

**What it does:** Melodyne can detect the chords in audio (or you can type them in) and display them on a **Chord Track**. The pitch grid can switch to a **Chord Grid**, so notes snap to the tones of the current chord, and Correct Pitch can target chord tones. Lets you adapt a recording or sample to the chord progression of your song (fully for polyphonic material in editor/studio).

**Technical theory:**
- **Chroma features:** Spectral energy is folded into 12 pitch-class bins regardless of octave (a "chromagram"). Each time window's chroma vector is compared with chord templates (major, minor, 7th...), often with smoothing over time (e.g. hidden Markov models) to avoid jumpy chord changes.
- **Harmonic adaptation:** Once the old and new chords are known, each note is mapped to the nearest appropriate tone of the new chord, and DNA resynthesis moves only the notes that need to change.

---

### 18. Tempo Detection and Editing

**What it does:**
- **All editions:** Melodyne can follow the DAW tempo and stretch material to fit it.
- **editor and studio (Extended tempo functions):** Detects the **tempo of a free-played recording**, including gradual and sudden changes, maps it to a tempo curve you can edit, and lets you either keep the human tempo (and sync other parts to it) or flatten it to a constant tempo.

**Technical theory:** **Beat tracking** computes an onset-strength function, then looks for the strongest periodicity (via autocorrelation or comb filters) to get tempo, and aligns a beat grid to the onsets (beat phase). Allowing the period to vary over time gives a **tempo map**. Making material conform to a new tempo is then note-aware time-stretching between beat markers.

---

### 19. DNA – Direct Note Access (editor and studio)

**What it does:** Edits individual notes inside **polyphonic** audio: change one note in a guitar chord, remove a wrong piano note, turn a major chord into a minor one, apply all tools (pitch, formant, amplitude, fades, time) to single notes in a chord. Introduced in 2008/2009 and patented.

**Technical theory:**
- **Multi-pitch estimation.** In a chord, each note produces its own harmonic series (f0, 2·f0, 3·f0...), and these overlap. The algorithm must explain the spectrum as a sum of several harmonic series, choosing the set of f0s that best accounts for the observed peaks, using knowledge about how real instruments' harmonics are distributed and decay.
- **Shared harmonics.** Notes in consonant chords share partials (e.g. in C–G, the 3rd harmonic of C equals the 2nd harmonic of G). The energy at such a frequency has to be divided between the notes, which is one reason polyphonic separation is fundamentally harder and never perfect.
- **Component assignment and resynthesis.** Once each spectral component is assigned to a note, each note can be resynthesised and modified independently, then the modified notes are summed back together. Related academic techniques include non-negative matrix factorisation (NMF) and harmonic-model-based source separation.
- **Sustain vs decay variants** set different expectations about how a note's amplitude evolves, improving the assignment for the given instrument type.
- **Limitations:** Best on clean recordings of one polyphonic instrument; dense mixes, heavy distortion, reverb and effects reduce separation quality.

---

### 20. Audio-to-MIDI (assistant and above)

**What it does:** Exports detected notes (pitch, start, length, velocity derived from amplitude) as a MIDI file, or drag notes to the DAW. Uses: doubling a vocal with a synth, transcribing a bass line, replacing drums with samples.

**Theory:** The note list Melodyne already holds is quantised into MIDI terms: note number from `69 + 12·log2(f/440)`, timing in ticks, and velocity mapped from the note's amplitude. In DNA mode, each chord tone becomes its own MIDI note (polyphonic transcription). Pitch-curve detail can be carried as pitch bend in some workflows, though basic export is note-level.

---

### 21. Copy, Paste and Creative Editing

**What it does:** Copy and paste notes to build new melodies, create harmony lines from a lead vocal (copy, paste and transpose), reverse note order, and generally "recompose" phrases or samples.

**Theory:** Because notes are independent objects in the analysis model, copying a note copies its parameters and source data; it can then be resynthesised at any pitch/time. Generated harmonies benefit from formant control to avoid a chipmunked or identical-sounding double.

---

### 22. Multitrack Features (studio only)

**What it does:**
- **Multi-tracking:** See and manage all of a DAW's Melodyne tracks in one window.
- **Multitrack Note Editing:** Display several tracks in the Note Editor and select/edit notes across them at once, e.g. backing vocals or choirs.
- **Track-transcending macros:** Run Correct Pitch, Quantize Time and Leveling over multiple tracks.
- **Quantize to reference track:** One track adopts another's timing (tightening doubles and backing vocals).

**Theory:** Mainly workflow, but important musically: tuning and timing judgments are relative. Seeing all parts lets you tune for vertical consonance (between voices) and align onsets across parts, which matters more for ensemble sound than each part being individually "correct".

---

### 23. Sound Editor (studio only)

**What it does:** Overtone-based sound design. You can see and change the level of each **harmonic** of a track's notes, apply macros that reshape the spectrum (e.g. emphasis of even vs odd harmonics, brightness, fundamental balance), use a musically tied EQ, add envelopes, and **transfer the timbre of one sound to another**. Celemony describes it as turning audio tracks into something like a synthesizer.

**Technical theory:**
- **Additive / harmonic model.** Any periodic sound is a sum of sine waves at integer multiples of f0 (Fourier). Melodyne's analysis already knows each note's harmonics, so it can scale each harmonic number directly.
- **Harmonic-relative vs fixed-frequency EQ.** A normal EQ boosts a fixed frequency (e.g. 2 kHz), affecting a different harmonic for every note. Boosting by **harmonic number** (e.g. "all 3rd harmonics") keeps the same timbral effect across the pitch range. The Sound Editor can work this way.
- **Even/odd harmonics.** Square-like waves (clarinets, closed pipes) emphasise odd harmonics; sawtooth-like sounds have both. Changing their balance changes perceived "hollowness" vs "fullness".
- **Timbre transfer / morphing.** The spectral envelope or harmonic profile of one source is imposed on another, conceptually similar to cross-synthesis or vocoding but at the harmonic level.

---

### 24. DAW Integration and Workflow

**What it does:**
- Runs **stand-alone** or as a **VST3, AU or AAX** plug-in on macOS (Universal Binary/Apple Silicon native since v5.2) and Windows.
- **ARA (Audio Random Access)** integration in supporting DAWs (e.g. Studio One, Cubase, Logic Pro, Pro Tools, Reaper, Cakewalk/Sonar, Ableton Live 12 and others): no real-time "transfer" step; Melodyne reads the audio directly from the DAW, follows edits, tempo and chord tracks in the DAW, and stores edits with the session.
- **Non-destructive editing** with undo history and selective reset (e.g. reset only pitch edits, or only timing).
- **Project compatibility** between all editions and with Melodyne 4 projects.
- **Keyboard shortcut** search and custom shortcut sets.

**Technical theory of ARA:** A normal plug-in only receives audio in real time, block by block, during playback, so it can't see the future and must "record" the material first. ARA is an extension that lets the plug-in **request any portion of the audio file at any time** (random access) and exchange musical context with the host (tempo map, chord track, region boundaries). That's what makes whole-file analysis and note-based editing behave like a native DAW feature.

---

## Part 3: Quick Reference – Feature by Edition

| Feature | essential | assistant | editor | studio |
|---|:-:|:-:|:-:|:-:|
| Main Tool (pitch, position, length, separations) | ✓ | ✓ | ✓ | ✓ |
| Melodic, Percussive, Percussive Pitched, Universal algorithms | ✓ | ✓ | ✓ | ✓ |
| Edit note assignments | ✓ | ✓ | ✓ | ✓ |
| Correct Pitch, Quantize Time, Leveling macros | ✓ | ✓ | ✓ | ✓ |
| Musically weighted pitch analysis | ✓ | ✓ | ✓ | ✓ |
| Chord Track, chord detection, Chord Grid | ✓ | ✓ | ✓ | ✓ |
| Stand-alone, ARA, VST3, AU, AAX | ✓ | ✓ | ✓ | ✓ |
| Full toolset: Pitch Modulation, Pitch Drift, Formant, Amplitude, Fade, Sibilant, Time, Attack Speed, Note Separation | | ✓ | ✓ | ✓ |
| Inspectors | | ✓ | ✓ | ✓ |
| Audio-to-MIDI | | ✓ | ✓ | ✓ |
| DNA polyphonic editing (Polyphonic Sustain/Decay) | | | ✓ | ✓ |
| Editing notes within chords | | | ✓ | ✓ |
| Extended scale functions (detection, custom scales, transforms) | | | ✓ | ✓ |
| Extended tempo functions (tempo detection/editing) | | | ✓ | ✓ |
| Multitrack view and multitrack note editing | | | | ✓ |
| Track-transcending macros | | | | ✓ |
| Quantize to reference track | | | | ✓ |
| Sound Editor (overtone-based sound design) | | | | ✓ |

---

## Sources

- [Celemony – Melodyne editions and comparison table](https://services.celemony.com/en/melodyne/melodyne-5-editions)
- [Mix – Melodyne 5 review](https://www.mixonline.com/?p=107306)
- [Sonicstate – Celemony releases Melodyne 5](https://sonicstate.com/news/2020/05/27/celemony-releases-melodyne-5/)
- [Sound On Sound – Melodyne 4 launched with Tempo Editor & more](https://www.soundonsound.com/node/4912688)
- [Production Expert – Melodyne 5 released with improved Melodic algorithm](https://www.production-expert.com/production-expert-1/2020/5/25/celemony-melodyne-5-released-with-improved-melodic-algorithm)
- [Celemony Help Center (full reference manual per edition)](https://helpcenter.celemony.com/M5/doc/melodyneStudio5/en/M5tour_QuickStart_studio)

Technical theory sections draw on standard audio DSP literature (pitch detection, source-filter modelling, PSOLA and phase-vocoder methods, harmonic-plus-noise modelling, multi-pitch estimation, chroma-based chord recognition and beat tracking).
