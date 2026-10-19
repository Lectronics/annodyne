# Melodyne-Class Editor: Core Architecture Spec

*Third document in the set, after `annodyne-features.md` (what the tools do) and `annodyne-implementation-guide.md` (how to make each process good). This one defines the **code structure**: the core functions and what they return, the non-destructive edit model, how edits turn into changed audio, and the folder layout for every tool.*

Signatures are written as typed Python-style pseudocode because it is easy to read. They map one-to-one onto C++ classes (the likely language for a real-time plug-in) or onto a Python research prototype.

---

## 1. The one rule that shapes everything

**The audio is never changed. Edits are never written into the waveform.**

There are three layers, and only the last one produces sound:

| Layer | What it holds | Mutable? |
|---|---|---|
| **Source** | The original audio file, exactly as recorded | Never |
| **Analysis** | What the engine *found* in the audio: pitch curve, notes, harmonics, noise, envelopes, onsets | Only when re-analysed (new version) |
| **Edits** | A list of parameter changes, each anchored to note IDs and **source-time** positions | Yes (append, undo, reset) |
| **Render** (output) | `audio_out = render(source, analysis, edits)`, computed on demand and cached | Derived, thrown away and recomputed freely |

So the "timestamping" idea is right in spirit, but the timestamps live on the **edits**, not inside the waveform. Each edit records *where* it applies (note ID plus a time range in the original recording) and *when* it was made (a sequence number for undo). The waveform you hear is always recomputed from the untouched original plus the current edit list.

This is what makes undo, selective reset ("reset only pitch edits"), opening a project in a smaller edition, and the null test (no edits gives the original audio back) all trivial.

---

## 2. Two time axes

Every time value in the system is one of two kinds, and mixing them up is the most common bug in this kind of software.

- **Source time (`s`)**: seconds in the original recording. Analysis and edits are always anchored here. It never changes, whatever the user does.
- **Output time (`τ`)**: seconds in what the user hears after timing edits.

They are linked by a **TimeMap**, a monotonic piecewise-linear function `τ = M(s)` built from all timing edits. With no timing edits, `M(s) = s`.

- The local slope `dM/ds` is the **stretch ratio** at that point (2.0 = twice as long).
- The renderer works backwards: for each output block it asks `s = M⁻¹(τ)` to know which part of the source to read.

---

## 3. Core data types

These live in `core/types/`. Every analysis function returns one of these; every tool and macro reads them.

```python
# ---- Audio ----
class AudioSource:
    id: str                 # stable ID, e.g. content hash of the file
    sample_rate: int
    channels: int
    length_samples: int
    def read(start: int, count: int) -> AudioBuffer  # read-only access (via ARA in a plug-in)

class AudioBuffer:
    data: float32[channels, samples]
    sample_rate: int
    start_time: float       # in source time or output time; the owner says which
                            # → superseded by ADR-0002: no start_time; use PositionedAudio<Axis>

# ---- Frame-based tracks (all on a shared analysis grid) ----
class FrameGrid:
    hop_seconds: float      # e.g. 0.005 (5 ms)
    start_time: float       # source time of frame 0
    n_frames: int

class PitchTrack:
    grid: FrameGrid
    f0_cents: float[n]      # cents relative to A4 = 440 Hz (69*100 = MIDI 69); NaN when unvoiced
    confidence: float[n]    # 0..1
    voicing: float[n]       # probability voiced, 0..1
    alternates: list[float[n]]  # 2nd/3rd best paths (octave alternatives) for Note Assignment

class OnsetList:
    times: float[]          # source time
    strength: float[]
    kind: list[Literal["attack", "consonant", "pitch_change", "phoneme"]]

class HarmonicTrack:        # per note
    grid: FrameGrid
    freq_hz: float[n, K]    # frequency of harmonic k (k = 1..K), measured, not assumed
    amp: float[n, K]        # linear amplitude
    phase: float[n, K]      # radians, for phase-coherent resynthesis

class NoiseTrack:           # per note
    grid: FrameGrid
    band_energy: float[n, B]       # residual energy per band (e.g. 24-40 bands)
    frame_class: list[Literal["voiced", "sibilant", "breath", "plosive", "silence"]]

class EnvelopeTrack:        # per note
    grid: FrameGrid
    log_mag: float[n, F]    # smooth spectral envelope (formants), F frequency bins
    freqs_hz: float[F]

# ---- The central object ----
class Note:
    id: str                 # stable across re-analysis where possible
    track_id: str
    voice: int              # 0 for monophonic; 0..N for notes inside chords
    source_start: float     # seconds, source time
    source_end: float
    attack_end: float       # where the transient/consonant ends (source time)
    release_start: float
    perceived_pitch_cents: float   # the "musically weighted" pitch (blob position)
    pitch_center_cents: float      # stability-weighted centre
    pitch_drift: float[n]          # slow component, cents relative to centre
    pitch_mod: float[n]            # fast component (vibrato), cents
    amp_db: float[n]               # loudness curve over the note
    loudness_lu: float             # single loudness value (K-weighted)
    harmonics: HarmonicTrack
    noise: NoiseTrack
    envelope: EnvelopeTrack
    separation_in: Literal["hard", "soft"]   # boundary type with previous note
    confidence: float
    is_active: bool         # False = "ghost" candidate (DNA) the user can switch on

class AnalysisResult:
    version: int            # increments on every re-analysis
    source_id: str
    algorithm: Literal["melodic", "percussive", "percussive_pitched",
                       "poly_sustain", "poly_decay", "universal"]
    pitch: PitchTrack | None
    onsets: OnsetList
    notes: list[Note]
    residual: NoiseTrack | None       # energy not explained by any note (polyphonic)
    beats: BeatGrid | None
    tempo: TempoCurve | None
    key: KeyEstimate | None
    chords: ChordTrack | None
    tuning_ref_hz: float              # detected reference, e.g. 441.3
```

---

## 4. Core functions

Each function below is **pure**: it takes inputs and parameters and returns a new object. Nothing writes into the source or into another object in place. This makes everything testable and cacheable.

Notation: `→` shows the return type. Parameters list the main controls only; every function also takes a `ctx: EngineContext` (sample rate, thread pool, model registry, cancel token).

### 4.1 Signal basics — `core/dsp/`

| Function | Key parameters | Returns | Notes |
|---|---|---|---|
| `stft(buf, win, hop, n_fft)` | window type, size, hop, zero-padding | `Spectrogram` (complex) | Shared by almost everything |
| `istft(spec, win, hop)` | window, hop | `AudioBuffer` | Overlap-add with COLA normalisation |
| `resample(buf, ratio, quality)` | ratio, filter length | `AudioBuffer` | High-quality sinc/polyphase |
| `k_weight(buf)` | — | `AudioBuffer` | BS.1770 pre-filter for loudness |
| `crossfade(a, b, shape, length)` | `"equal_power" \| "linear" \| "log"` | `AudioBuffer` | Used at every edited-region boundary |
| `interp_curve(curve, times)` | `"linear" \| "cubic" \| "hold"` | `float[]` | Resample a frame track at arbitrary times |

### 4.2 Analysis — `core/analysis/<stage>/`

| Function | Key parameters | Returns |
|---|---|---|
| `detect_pitch(src, range_hz=(50, 1600), hop=0.005, model="rmvpe", smoothing="viterbi", octave_penalty=…)` | estimator, range, hop, smoothing strength | `PitchTrack` |
| `detect_onsets(src, method="superflux", vibrato_suppress=True, threshold="adaptive")` | method, sensitivity | `OnsetList` |
| `segment_notes(pitch, onsets, src, min_note_ms=60, use_phonemes=False, beat_grid=None)` | min length, cue weights | `list[Note]` (timing and pitch curve filled, other tracks empty) |
| `perceived_pitch(note, weighting="stability", gamma=…, trim_ms=(40, 60))` | weighting model, trims | `float` (cents) |
| `decompose_pitch_curve(note, drift_cutoff_hz=2.5)` | low-pass cut-off | `(center, drift[n], mod[n])` |
| `analyse_partials(src, note, max_harmonics=60, estimator="reassignment", window="f0_adaptive")` | estimator, window rule | `HarmonicTrack` |
| `split_harmonic_noise(src, note, harmonics, n_bands=32)` | band layout, classifier model | `(NoiseTrack, residual AudioBuffer)` |
| `estimate_envelope(note, harmonics, method="true_envelope", order="from_f0")` | method, order | `EnvelopeTrack` |
| `measure_loudness(src, note, exclude=["sibilant","breath"])` | excluded frame classes | `(loudness_lu, amp_db[n])` |
| `transcribe_polyphonic(src, model="onsets_frames", family="decay", min_conf=0.3)` | model, instrument family, threshold | `list[Note]` (active + ghost candidates) |
| `separate_notes(src, notes, method="score_informed_nmf", iterations=…)` | method, mask type | `dict[note_id, AudioBuffer]` + residual `AudioBuffer` |
| `track_beats(src, model="beat_this", tempo_prior=None)` | prior strength | `BeatGrid` |
| `fit_tempo_curve(beats, smoothing)` | smoothing | `TempoCurve` |
| `estimate_tuning(notes)` | — | `float` (reference Hz) |
| `estimate_key(notes_or_chroma, profiles="temperley")` | profile set | `KeyEstimate` |
| `extract_scale(notes, resolution_cents=2)` | histogram resolution | `Scale` (custom cent table) |
| `recognise_chords(src, notes=None, vocab="large")` | vocabulary | `ChordTrack` |
| `analyse(src, algorithm="auto", params)` | everything above | `AnalysisResult` — the orchestrator that runs the right chain for the chosen algorithm |

### 4.3 Tuning — `core/tuning/`

| Function | Key parameters | Returns |
|---|---|---|
| `target_pitch(pitch_cents, grid)` | grid = chromatic / scale / chord / off | `float` (nearest target, cents) |
| `scale_degree(pitch_cents, scale)` / `from_degree(degree, scale)` | scale | `(degree, octave, offset)` / `float` |
| `solve_adaptive_ji(sounding_notes, strength, drift_anchor)` | how pure, how anchored | `dict[note_id, cents_offset]` |

### 4.4 Edits — `core/edits/`

| Function | Returns | Notes |
|---|---|---|
| `EditList.add(edit)` | new `EditList` version | Append-only; returns the new head |
| `EditList.undo()` / `redo()` | new `EditList` version | Moves a cursor; nothing deleted |
| `EditList.reset(note_ids, categories)` | new `EditList` version | Adds "reset" edits (keeps history) |
| `effective_state(note, edits)` | `EffectiveNote` | **Folds** every edit for this note onto its analysed values (section 5.3) |
| `build_time_map(notes, edits)` | `TimeMap` | Section 5.4 |
| `dirty_regions(old_edits, new_edits)` | `list[(s_start, s_end)]` | Which source ranges need re-rendering |
| `remap_after_reanalysis(edits, old_analysis, new_analysis)` | `EditList` | Re-attaches edits to new note IDs after Note Assignment changes |

### 4.5 Synthesis engines — `core/synthesis/<engine>/`

All engines implement one interface, so the renderer can choose per note:

```python
class SynthesisEngine(Protocol):
    def render_note(self,
                    src: AudioSource,
                    note: Note,                 # analysed (original) values
                    eff: EffectiveNote,         # edited values
                    time_map: TimeMap,
                    out_range: tuple[float, float]   # output time
                    ) -> AudioBuffer: ...       # the note's edited audio, in output time
                                                # → ADR-0002: returns OutputAudio
```

| Engine folder | Used for | Method |
|---|---|---|
| `harmonic_noise/` | Melodic voice (default) | Additive harmonics from warped envelope + filtered noise |
| `psola/` | Clean monophonic voice, small edits | Pitch-synchronous overlap-add |
| `phase_vocoder/` | Universal, sustained polyphony | Phase-locked or PGHI vocoder |
| `transient/` | Percussive, attacks | Transient-preserving stretch / HPSS split |
| `neural_vocoder/` | Optional voice polish | Source-filter neural vocoder conditioned on edited f0 + envelope |

### 4.6 Render — `core/render/`

| Function | Key parameters | Returns |
|---|---|---|
| `render_region(project, track_id, out_start, out_end, quality)` | `"preview" \| "final"` | `RenderResult` |
| `render_block(project, track_id, block_index)` | — | `AudioBuffer` (real-time path, reads cache) |
| `RenderCache.get(key)` / `put(key, buf)` | key = hash(source_id, analysis.version, relevant edits, engine settings) | `AudioBuffer \| None` |
| `schedule_background_render(regions, priority)` | priority near playhead | — |

```python
class RenderResult:
    buffer: AudioBuffer         # output time
    out_range: tuple[float, float]      # → ADR-0002: dropped; derived from buffer (OutputAudio)
    source_range: tuple[float, float]   # what part of the original was read
    cache_key: str
    notes_rendered: list[str]   # which notes were actually re-synthesised
    passthrough: bool           # True if nothing edited here (original audio copied)
```

---

## 5. How edits change the sound

### 5.1 The edit record

```python
class Edit:
    id: str
    seq: int                    # global sequence number → undo order
    created_at: datetime        # wall-clock timestamp (history display, collaboration)
    group_id: str | None        # one user gesture or macro run = one group = one undo step
    origin: str                 # "main_tool", "correct_pitch", "leveling", ...
    target: EditTarget
    category: Literal["pitch", "pitch_mod", "pitch_drift", "transition", "formant",
                      "amplitude", "fade", "sibilant", "time", "attack",
                      "separation", "assignment", "sound"]
    param: str                  # e.g. "center_offset_cents", "mod_scale"
    op: Literal["set", "add", "scale"]
    value: float | Curve        # a number, or a curve over the note (e.g. a fade)

class EditTarget:
    note_ids: list[str]         # usually the target
    source_range: tuple[float, float] | None   # for range edits (e.g. a time handle)
    track_id: str
```

Why `op` matters: `"add"` and `"scale"` edits **compose** (two +20-cent drags give +40), while `"set"` overrides everything before it in the same category. Macros normally write `"set"` values computed from the *analysed* state, so re-running a macro replaces its previous result instead of stacking.

### 5.2 Edit categories map to tools

| Category | Params | Written by |
|---|---|---|
| `pitch` | `center_offset_cents` | Main Tool, Pitch Tool, Correct Pitch, Chord/Scale snap, copy-paste transpose |
| `pitch_mod` | `mod_scale` (1 = original, 0 = flat, -1 = inverted) | Pitch Modulation Tool |
| `pitch_drift` | `drift_scale` | Pitch Drift Tool, Correct Pitch |
| `transition` | `in_time_scale`, `out_time_scale` | Pitch Tool (transitions) |
| `formant` | `formant_offset_cents` | Formant Tool |
| `amplitude` | `gain_db`, `within_note_flatten` | Amplitude Tool, Leveling |
| `fade` | `fade_in_s`, `fade_out_s`, `shape` | Fade Tool |
| `sibilant` | `noise_gain_db` (per frame class) | Sibilant Balance Tool |
| `time` | `onset_shift_s`, `length_scale`, `handles` | Main Tool, Time Tool, Quantize Time, tempo edits |
| `attack` | `attack_time_scale` | Attack Speed Tool |
| `separation` | add/remove/move boundary, `type` | Note Separation Tool |
| `assignment` | octave move, activate/deactivate, merge/split | Note Assignment Mode (triggers re-analysis) |
| `sound` | `harmonic_gains[k]`, envelope morph | Sound Editor |

### 5.3 Folding edits into an effective note

`effective_state(note, edits)` walks the edits for that note in `seq` order (up to the undo cursor) and produces:

```python
class EffectiveNote:
    note_id: str
    center_cents: float          # = analysed centre + Σ pitch offsets
    mod_scale: float             # product of scales (or last "set")
    drift_scale: float
    transition_in_scale: float
    transition_out_scale: float
    formant_offset_cents: float
    gain_db: float
    within_note_flatten: float   # 0..1
    fade_in_s: float; fade_out_s: float; fade_shape: str
    noise_gain_db: dict[str, float]   # per frame class: sibilant, breath, ...
    harmonic_gains_db: float[K]
    attack_time_scale: float
    is_edited: bool              # False → renderer passes original audio through
```

From that, the **edited curves** the synthesis engine needs are computed per frame `t` of the note:

**Pitch**

```
f0_cents'(t) = center_cents
             + drift_scale × drift(t)
             + mod_scale   × mod(t)
             + transition_shape(t)          # reshaped glide near the note edges
pitch_ratio(t) = 2 ^ ((f0_cents'(t) − f0_cents(t)) / 1200)
```

Shifting the centre moves the whole curve, so vibrato and scoops come along untouched. Correct Pitch writes `center_offset = (target − perceived) × amount` and `drift_scale = 1 − drift_amount`.

**Formants (spectral envelope)**

```
warp = 2 ^ (formant_offset_cents / 1200)
env'(t, f) = env(t, f / warp)          # stretch the envelope along frequency
harmonic_amp'(t, k) = env'(t, k × f0'(t))   # re-read each harmonic at its new frequency
```

With `formant_offset = 0`, the envelope is unchanged and the voice keeps its timbre after any pitch shift.

**Level**

```
amp_db'(t) = amp_db(t)
           + gain_db
           − within_note_flatten × (amp_db(t) − mean(amp_db))   # Leveling, within-note
           + fade_db(t)                                          # from fade in/out curves
noise_db'(t) = noise_db(t) + noise_gain_db[frame_class(t)]       # Sibilant tool
harmonic_amp'(t, k) += harmonic_gains_db[k]                      # Sound Editor
```

**Noise part:** never pitch-shifted. It keeps its original band energies (plus its gain and the formant warp), and is time-mapped with the same TimeMap, but sibilant regions use a stretch ratio closer to 1.

### 5.4 The TimeMap

`build_time_map(notes, edits)` turns timing edits into anchor points `(s_i, τ_i)`:

1. Start from the identity map with anchors at every note's `source_start`, `attack_end`, `release_start` and `source_end`.
2. Apply `onset_shift_s` and `length_scale` per note, moving its anchors in output time.
3. Insert user **time handles** as extra anchors.
4. Protect consonants and attacks: the segment `[source_start, attack_end]` keeps slope 1 (scaled only by `attack_time_scale`); vowels absorb the change.
5. Resolve gaps and overlaps between neighbours by stretching the neighbour's boundary region (never leave silence or double audio).
6. Enforce monotonic order (`τ` always increasing) and return the piecewise-linear map plus its inverse.

The map is per track. Tempo edits and Quantize Time are simply many anchor moves written as `time` edits.

### 5.5 The render pipeline (what happens when the user drags a note)

```
User drags note N up 2 semitones
        │
tools/main_tool  ──►  Edit(category="pitch", op="add", value=+200, target=[N])
        │
EditList.add()   ──►  new edit-list version (seq = 1043, group = drag gesture)
        │
dirty_regions()  ──►  [N.source_start − 50 ms, N.source_end + 50 ms]
        │
RenderCache      ──►  invalidates blocks overlapping that region
        │
render_region()  ──►  for each note in region:
                        eff = effective_state(note, edits)
                        if not eff.is_edited:  copy original audio (passthrough)
                        else:  engine.render_note(src, note, eff, time_map, range)
                      ──►  crossfade edited notes into neighbours (equal-power, 5–20 ms)
                      ──►  RenderResult (cached)
        │
Host playback reads render_block() from the cache
```

### 5.6 Change-only rendering (keeps unedited audio identical)

- **Monophonic:** outside edited notes and their crossfade zones, the output **is the original audio**, copied sample for sample. Only edited notes are re-synthesised and crossfaded in. Undo everything and the output equals the input exactly (the null test).
- **Polyphonic (DNA):** the renderer never rebuilds the whole chord from separated parts. Instead:

```
out = original
    − Σ separated_original[n]   for every edited note n
    + Σ render_note(n)          for every edited note n
```

Unedited notes and the residual (reverb, noise) stay exactly as recorded.

- **Timing edits** are the exception: once the TimeMap is not the identity in a region, that region must be re-synthesised (or at least re-stretched), because the samples move.

### 5.7 Re-analysis without losing edits

Note Assignment edits change the analysis itself (an octave fix, a split, an activated ghost note). That produces a new `AnalysisResult.version`. `remap_after_reanalysis()` then reattaches existing edits:

- Notes whose source span barely changed keep their ID, so their edits carry over.
- A split note copies its parent's edits to both children.
- Merged notes take the edits of the longer parent.
- Edits that no longer match anything are kept but flagged as orphaned, so nothing is silently lost.

### 5.8 What gets saved

```
project/
  project.json        # tracks, source IDs, analysis settings, edition
  analysis/<source_id>/v<version>.bin   # cached analysis (can be recomputed)
  edits/<track_id>.jsonl                # append-only edit log, one Edit per line
  render_cache/                         # optional, disposable
```

The edit log is the only irreplaceable part besides the original audio. Analysis and renders can always be rebuilt from it. With ARA, this document is stored inside the DAW session.

---

## 6. Folder layout

Rule of the layout: **`core/` is the only place that touches audio. Tools never process audio; they only read the analysis and write `Edit` objects.** That keeps every tool small and makes undo, reset and edition limits uniform.

```
anno/
├── core/
│   ├── types/                 # AudioSource, Note, tracks, AnalysisResult, Edit, EffectiveNote
│   ├── dsp/                   # stft/istft, windows, filters, resampling, crossfades, k-weighting
│   ├── io/                    # WAV read/write; linkable only by project/host/ui/cli + tests (ADR-0001)
│   ├── analysis/
│   │   ├── pitch/             # detect_pitch: pYIN, neural estimators, Viterbi smoothing, refinement
│   │   ├── onsets/            # detect_onsets: SuperFlux, ComplexFlux, CNN
│   │   ├── segmentation/      # segment_notes: HMM note tracker, phoneme cues
│   │   ├── perceived_pitch/   # perceived_pitch, decompose_pitch_curve
│   │   ├── partials/          # analyse_partials: QIFFT, reassignment, harmonic sieve tracking
│   │   ├── harmonic_noise/    # split_harmonic_noise, frame classifier (voiced/sibilant/breath)
│   │   ├── envelope/          # estimate_envelope: True Envelope, TE-LPC, CheapTrick
│   │   ├── loudness/          # measure_loudness (per note, BS.1770-based)
│   │   ├── polyphonic/        # transcribe_polyphonic: neural transcriber + DSP refinement
│   │   ├── separation/        # separate_notes: score-informed NMF, masks, residual
│   │   ├── rhythm/            # track_beats, fit_tempo_curve
│   │   ├── harmony/           # estimate_tuning, estimate_key, extract_scale, recognise_chords
│   │   └── orchestrator/      # analyse(): picks the chain per algorithm, caching, progress
│   ├── tuning/                # scales, temperaments, target_pitch, scale degrees, adaptive JI solver
│   ├── edits/                 # Edit, EditList (undo/redo/reset), effective_state, remapping
│   ├── timemap/               # TimeMap build, inverse, anchors, consonant protection
│   ├── synthesis/
│   │   ├── interface/         # SynthesisEngine protocol, engine selection rules
│   │   ├── harmonic_noise/    # additive + filtered-noise resynthesis
│   │   ├── psola/
│   │   ├── phase_vocoder/     # phase-locked + PGHI
│   │   ├── transient/         # transient-preserving / HPSS stretch
│   │   └── neural_vocoder/    # optional, behind a feature flag
│   ├── render/                # render_region, render_block, RenderCache, background scheduler
│   └── models/                # ML model loading, ONNX/CoreML runtimes, weight registry
│
├── tools/                     # one folder per interactive tool; each exports a Tool class
│   ├── main_tool/             # pitch drag, time drag, length, snap
│   ├── pitch/                 # pitch + transitions
│   ├── pitch_modulation/
│   ├── pitch_drift/
│   ├── formant/
│   ├── amplitude/
│   ├── fade/
│   ├── sibilant/
│   ├── time/                  # time + time handles
│   ├── attack_speed/
│   ├── note_separation/
│   └── note_assignment/       # octave moves, ghost notes, splits (writes "assignment" edits)
│
├── macros/                    # batch operations; each returns a group of Edits
│   ├── correct_pitch/
│   ├── quantize_time/         # incl. quantize to reference track (DTW alignment)
│   └── leveling/
│
├── features/                  # larger functions that combine analysis, tuning and edits
│   ├── scales/                # scale library, custom scales, scale detection, transformations
│   ├── chords/                # chord track, chord grid, snapping to chord tones
│   ├── tempo/                 # tempo detection UI model, keep/flatten tempo
│   ├── dna/                   # polyphonic editing workflows built on analysis/polyphonic + separation
│   ├── sound_editor/          # harmonic gains, spectral macros, timbre transfer
│   ├── midi_export/           # Audio-to-MIDI (notes, pitch bend, velocity)
│   ├── clipboard/             # copy/paste notes, harmony generation
│   └── multitrack/            # cross-track selection, cross-track macros, vertical intonation
│
├── project/                   # document model, save/load, edit log, versioning, edition limits
├── host/
│   ├── ara/                   # ARA 2 document controller, playback renderer, audio access
│   ├── plugin/                # VST3 / AU / AAX wrappers (e.g. via JUCE)
│   └── standalone/            # stand-alone app shell, audio I/O
├── ui/                        # note editor, blobs, inspectors, grids, tool palette
└── tests/                     # folders = CTest labels (ADR-0004)
    ├── unit/                  # per-function tests
    ├── contract/              # core-contract invariants, layer-rule fixtures
    ├── audio/                 # objective audio checks: null, undo-null, untouched, round-trips
    ├── golden/                # reference audio + expected analysis (pitch, notes)
    └── torture/               # vibrato, sibilants, ±12 st, chords, reverb
```

### 6.1 What every tool folder contains

Each folder under `tools/` and `macros/` follows the same shape:

```
tools/formant/
  tool.py            # FormantTool class: reacts to gestures, emits Edit objects
  params.py          # its parameters, ranges, defaults, edition availability
  preview.py         # cheap visual feedback while dragging (no render needed)
  tests/
```

```python
class Tool(Protocol):
    category: str                           # which edit category it writes
    def on_gesture(self, gesture: Gesture,
                   selection: list[Note],
                   analysis: AnalysisResult,
                   edits: EditList) -> list[Edit]: ...

class Macro(Protocol):
    def run(self, selection: list[Note],
            analysis: AnalysisResult,
            edits: EditList,
            params: dict) -> list[Edit]: ...     # one group, so one undo step
```

Example, the whole of Correct Pitch in this model:

```python
def run(selection, analysis, edits, params):
    out = []
    for note in selection:
        target = target_pitch(note.perceived_pitch_cents, params.grid)
        offset = target - note.perceived_pitch_cents
        if abs(offset) < params.tolerance_cents:
            continue
        out.append(Edit(category="pitch", op="set",
                        param="center_offset_cents",
                        value=offset * params.center_amount, target=[note.id]))
        out.append(Edit(category="pitch_drift", op="set",
                        param="drift_scale",
                        value=1 - params.drift_amount, target=[note.id]))
    return out
```

No audio is touched; the renderer does the rest.

---

## 7. Tool → core function map

| Tool / feature | Reads (analysis) | Writes (edit categories) | Render path |
|---|---|---|---|
| Main Tool | perceived pitch, note timing | `pitch`, `time` | engine per algorithm + TimeMap |
| Pitch Tool | pitch curve, transitions | `pitch`, `transition` | pitch curve rebuild |
| Pitch Modulation | `pitch_mod` | `pitch_mod` | pitch curve rebuild |
| Pitch Drift | `pitch_drift` | `pitch_drift` | pitch curve rebuild |
| Formant | envelope | `formant` | envelope warp |
| Amplitude | `amp_db`, loudness | `amplitude` | gain stage |
| Fade | `amp_db` | `fade` | gain stage |
| Sibilant | noise track, frame classes | `sibilant` | noise gain |
| Time / handles | onsets, attack/release points | `time` | TimeMap |
| Attack Speed | attack region | `attack` | TimeMap (attack segment) |
| Note Separation | segmentation | `separation` | re-segment, then normal render |
| Note Assignment | pitch alternates, ghost notes | `assignment` | re-analysis + edit remap |
| Correct Pitch | perceived pitch, scale/chords | `pitch`, `pitch_drift` | pitch curve rebuild |
| Quantize Time | onsets, beat grid / reference track | `time` | TimeMap |
| Leveling | loudness, `amp_db` | `amplitude` | gain stage |
| Scales / Dynamic JI | notes, key, scale | `pitch` | pitch curve rebuild |
| Chords | chord track | `pitch` (DNA notes) | per-note render + change-only remix |
| Tempo | beats, tempo curve | `time` | TimeMap |
| DNA | polyphonic notes, separated signals | any | change-only polyphonic remix |
| Audio-to-MIDI | notes, pitch curves, loudness | — (export only) | none |
| Copy/paste | notes | new virtual notes + `pitch`, `time` | renders copied source segment |
| Sound Editor | harmonic tracks, envelope | `sound` | harmonic-noise engine |
| Multitrack | all tracks | any, across tracks | per track |

Copy/paste is the one tool that adds notes: a pasted note is a **virtual note** that references the source span of the original note plus its own edits, so even duplication stays non-destructive.
