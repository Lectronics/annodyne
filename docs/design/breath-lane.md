# Design note: Breath detection, separation and the Breath Lane

- **Phase:** detection in 4–5 (needs harmonic/noise frame classes and the segment timeline); processing in 6; lane UI in 10
- **Status:** draft
- **Requires:** ADR-0008, `silence-and-gaps.md` (breaths live inside gap segments)
- **Related:** `note-stretching.md` (shared splice and shaped-noise utilities)
- **Folders / CMake targets:**
  - `src/core/types/` — `BreathEvent`, `BreathTrack`, `BreathChain` (layer 0)
  - `src/core/dsp/dynamics/` — `Compressor`, `Limiter`, `HighPass` (layer 0, RT-safe, reusable)
  - `src/core/analysis/breaths/` — breath detection (layer 1)
  - `src/core/synthesis/breath/` — per-event render with the chain (layer 2)
  - `src/features/breaths/` — breath lane model, breath macros (layer 5)
  - `src/ui/breath_lane/` — the lane at the bottom of the note editor (layer 7)

## Purpose

Find the breaths in a vocal, separate them from the notes, and give them their own editor: a **Breath Lane** along the bottom of the note editor. There, each breath is a clip that can be turned down, muted, faded or trimmed, and all breaths share a processing chain (high-pass, compressor, limiter, lane gain). Typical jobs:

- Turn all breaths down 8 dB without touching the singing.
- Even out breaths so a loud gasp and a quiet sip sit at a similar level (compressor + limiter, or the "Breath level" macro).
- Mute the odd distracting breath; keep the natural ones.

## Concepts

| Term | Meaning |
|---|---|
| **Breath event** | One detected inhale or exhale, inside a gap segment. Rendered by the breath lane, not by the gap. |
| **Attached breath** | Breathy noise inside a note (aspirated onset, breathy release). Lives in the note's noise layer. **Out of scope for v1**; see Open question 1. |
| **Breath Lane** | The editor strip showing breath events on the same output time axis as the note editor. |
| **Breath chain** | Track-level processing applied to every breath event: HPF → compressor → limiter → lane gain. |

Everything stays non-destructive. Detection results are analysis; clip and chain settings are edits; the processed breaths are rendered and cached like any other region.

---

## 1. Detection

### 1.1 Pipeline

```
noise layer frame classes (B6) ─┐
noise floor (silence-and-gaps)  ├─► frame scores ─► event grouping ─► boundary refinement ─► BreathEvent[]
gap segments (timeline)         ┘        (classifier)     (hysteresis)      (envelope vs floor)
```

1. **Frame scores** (10 ms hop) inside gaps and in the 300 ms before each phrase. Features:
   - periodicity / voicing probability (breaths are unvoiced)
   - level above the local noise floor (breaths are above room noise)
   - spectral centroid and slope (breaths: broadband, centroid typically lower than sibilants; sibilants concentrate energy high, roughly 4–10 kHz)
   - spectral flatness (breaths are noise-like)
   - envelope smoothness (breaths rise and fall over tens of ms; clicks and lip smacks are impulsive)
   v1 uses a small logistic or gradient-boosted classifier on these features, trained on labelled vocals. The interface allows a neural frame classifier later (Phase 13).
2. **Event grouping:** hysteresis on the score (enter at `enter_threshold`, leave at `exit_threshold`), merge events closer than `merge_gap`, drop events shorter than `min_length`, split events longer than `max_length` at their quietest point.
3. **Boundary refinement:** extend each event's edges outward while the level stays above floor + `edge_margin`, so the breath's soft start and end are included. Clamp to the gap (never into a note's preroll guard).
4. **Kind:** inhale if followed by a note within `inhale_window`, exhale if it follows a note's tail directly, else unknown.

### 1.2 Function

```cpp
/// Detect breath events inside gap segments. All times are source time.
/// Returns events sorted by start, non-overlapping, each fully inside one gap.
/// Error if the timeline and analysis versions differ.
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<std::vector<BreathEvent>> detect_breaths(const AudioSource& src,
                                                              const AnalysisResult& analysis,
                                                              const NoiseFloor& floor,
                                                              const BreathDetectParams& params);

struct BreathDetectParams {
    double enter_threshold = 0.6, exit_threshold = 0.4;   // classifier score
    Seconds min_length{0.080}, max_length{1.200};
    Seconds merge_gap{0.060};
    Decibels edge_margin{3.0};
    Seconds inhale_window{0.400};
    double sensitivity = 0.5;                             // user slider; shifts both thresholds
};
```

**Ordering with the timeline:** `build_segment_timeline` needs breaths to classify gap regions, and `detect_breaths` needs gaps. Resolved in `analyse()` by running: notes → noise floor → provisional timeline (no breaths) → `detect_breaths` → final timeline. Both steps are pure, so this is just call order in the orchestrator.

---

## 2. Separation

v1 separates in **time**, not in frequency: a breath event owns the original samples inside its boundaries. Because events are confined to gaps, no harmonic/noise unmixing is needed, and an unedited breath is bit-identical to the source.

- **Gap render** (`render_gap`) skips breath regions.
- **Breath render** (`render_breath_event`) produces each event, with short fades at its edges *only when the event is edited*, and pastes it into output time.
- **Muted or turned-down breaths leave no hole:** the region they occupied is filled at the gap's own level. If the gap is untouched, the "hole" is filled with shaped room tone at the local floor level, faded in and out, so muting a breath sounds like silence in that room, not a dropout. If the gap is gated or muted, the fill follows the gap's gain.
- **Passthrough rule:** if neither the gap nor any breath in it is edited, and the breath chain is bypassed, the whole gap is copied from the source in one piece. No splitting, no fades, no summing. This keeps the null test bit-exact.

---

## 3. Public types

```cpp
namespace anno {

struct BreathId { std::uint64_t value; };

enum class BreathKind { inhale, exhale, unknown };

struct BreathEvent {
    BreathId id;
    SegmentId gap;                 // the gap that contains it
    SourceTime start, end;
    BreathKind kind;
    std::optional<NoteId> next_note;    // phrase it leads into (for inhales)
    Decibels peak;                 // dBFS
    LoudnessUnits loudness;        // K-weighted, over the event
    Hz centroid;
    double confidence;             // 0..1
};

// ---------- core/types/dynamics_params.h (generic, reused beyond breaths) ----------
struct HighPassParams { bool enabled = false; Hz cutoff{100.0}; int order = 2; };

struct CompressorParams {
    bool enabled = false;
    Decibels threshold{-30.0};
    double ratio = 3.0;
    Decibels knee{6.0};
    Seconds attack{0.005}, release{0.080};
    Decibels makeup{0.0};
    enum class Detector { peak, rms } detector = Detector::rms;
};

struct LimiterParams {
    bool enabled = false;
    Decibels ceiling{-24.0};            // breath default; breaths rarely need to be loud
    Seconds lookahead{0.003};
    Seconds release{0.050};
    bool true_peak = false;             // 4× oversampled detection
};

// ---------- core/types/breaths.h ----------
/// Track-level processing settings for all breaths. Plain data (edits fold into it).
struct BreathChain {
    HighPassParams hpf;
    CompressorParams compressor;
    LimiterParams limiter;
    Decibels lane_gain{0.0};
    bool lane_mute = false;
    bool lane_solo = false;              // monitoring only; not saved into renders
    [[nodiscard]] bool is_bypassed() const noexcept;   // all off, gain 0, not muted
};

struct EffectiveBreath {
    BreathId id;
    Decibels clip_gain{0.0};
    bool mute = false;
    Seconds fade_in{0.0}, fade_out{0.0};
    Seconds trim_start{0.0}, trim_end{0.0};     // shrink inside original bounds only
    bool is_edited = false;
};

} // namespace anno
```

`AnalysisResult` gains `std::vector<BreathEvent> breaths`.

---

## 4. Processing chain

Order per event:

```
original samples ─► trim ─► clip gain + clip fades ─► HPF ─► compressor ─► limiter ─► lane gain ─► output
```

- **Clip gain before dynamics**, like clip gain in a DAW: turning one breath down means the compressor treats it as quieter. The "Breath level" macro works through clip gain, so it shapes what the compressor sees.
- **Lane gain last:** the final "all breaths up/down" fader, independent of dynamics.
- **Limiter** sets a hard ceiling so no breath can be louder than a chosen level (default −24 dBFS when enabled).

### 4.1 Dynamics classes (`core/dsp/dynamics/`)

Reusable, real-time-safe processors. They are written for real-time use even though the breath lane renders offline, because Phase 12 and future features will reuse them live.

```cpp
namespace anno {

/// Feed-forward compressor. Gain computed in the log domain with a soft knee,
/// smoothed with separate attack/release time constants.
/// RT-SAFE after prepare(): process() does not allocate, lock or throw.
class Compressor {
public:
    void prepare(double sample_rate, int max_block);          // allocates
    void set_params(const CompressorParams& p) noexcept;
    void reset() noexcept;                                    // clears envelope state
    // RT-SAFE
    void process(std::span<const float> in, std::span<float> out,
                 std::span<float> gain_reduction_db) noexcept; // GR for metering, may be empty
private:
    double sample_rate_ = 0.0;
    double env_db_ = 0.0;
    double attack_coef_ = 0.0, release_coef_ = 0.0;
    CompressorParams params_;
};

/// Lookahead brick-wall limiter: sliding-window peak over the lookahead, gain
/// smoothed so it reaches its target before the peak arrives.
/// Latency = lookahead (reported). Optional 4× oversampled true-peak detection.
/// RT-SAFE after prepare().
class Limiter {
public:
    void prepare(double sample_rate, int max_block);
    void set_params(const LimiterParams& p) noexcept;
    void reset() noexcept;
    [[nodiscard]] Samples latency() const noexcept;
    // RT-SAFE
    void process(std::span<const float> in, std::span<float> out,
                 std::span<float> gain_reduction_db) noexcept;
private:
    std::vector<float> delay_;          // sized in prepare()
    std::vector<float> peak_window_;    // monotonic deque storage, sized in prepare()
    double sample_rate_ = 0.0;
    double gain_ = 1.0, release_coef_ = 0.0;
    LimiterParams params_;
};

/// Butterworth high-pass as cascaded biquads. RT-SAFE after prepare().
class HighPass { /* prepare, set_params, reset, process */ };

} // namespace anno
```

### 4.2 Per-event processing (how state works non-destructively)

Compressors and limiters have memory; a non-destructive renderer renders regions independently and caches them. To keep results deterministic and cacheable:

- **Each event is processed on its own**, with processor state reset at the event start.
- The processor is **primed with a pre-roll** of the event's first `max(attack, lookahead)` samples processed and discarded, so the first breath sample is not hit by a cold envelope. Limiter latency is compensated by reading `latency()` extra samples at the end and dropping the first `latency()` output samples.
- Breaths are separated by notes and silence much longer than typical release times (tens of ms), so carrying state from one breath to the next would make little audible difference, and reset-per-event makes every render independent of what was rendered before.
- Cache key: `hash(event id, source id, analysis version, effective breath, chain params)`.

```cpp
/// Render one breath event into output time with its clip settings and the chain.
/// Returns an empty buffer if muted (the caller fills the region per §2).
/// If the event is unedited and chain.is_bypassed(): returns the source samples
/// exactly. Deterministic. Not RT-safe; runs on render threads.
[[nodiscard]] Result<AudioBuffer> render_breath_event(const BreathEvent& ev,
                                                      const EffectiveBreath& eff,
                                                      const BreathChain& chain,
                                                      const TimeMap& map,
                                                      const AudioSource& src,
                                                      BreathRenderScratch& scratch);
```

`BreathRenderScratch` owns prepared `HighPass`, `Compressor` and `Limiter` instances per render thread, so rendering allocates nothing per event after warm-up.

**Metering:** the lane shows input level, output level and gain reduction. These come from the cached render (the `gain_reduction_db` span written during `process`), not from the audio thread.

---

## 5. Edits

| Category | Target | Params | Written by |
|---|---|---|---|
| `breath` | `BreathId` | `clip_gain_db`, `mute`, `fade_in_s`, `fade_out_s`, `trim_start_s`, `trim_end_s` | Breath lane clip gestures, breath macros |
| `breath_chain` | track | every `BreathChain` field (`hpf.*`, `compressor.*`, `limiter.*`, `lane_gain_db`, `lane_mute`) | Lane header controls |
| `breath_assignment` | gap or `BreathId` | `mark_breath(range)`, `unmark(id)`, `split(id, at)`, `merge(a, b)` | User corrections in the lane |

`breath_assignment` edits re-run only the affected gap's classification (`build_segment_timeline` for that gap), not the whole analysis. Existing edits on surviving breath IDs carry over (same remapping rules as notes: keep, split copies, merge takes the longer, orphans are flagged).

`effective_breath(ev, edits)` and `effective_breath_chain(edits)` live in `core/edits/`, folded like `effective_state`.

---

## 6. Macros (`features/breaths/`)

```cpp
/// Set each selected breath's clip gain so its loudness sits at a fixed distance
/// below the phrase it leads into (or follows, for exhales), clamped to limits.
/// gain = (phrase_loudness + params.offset) − breath_loudness, clamped to
/// [params.min_gain, params.max_gain]. Breaths with no adjacent phrase use the
/// track's median phrase loudness. One undo group.
class BreathLevel final : public Macro { /* run(...) → std::vector<Edit> */ };
struct BreathLevelParams {
    Decibels offset{-18.0};
    Decibels min_gain{-30.0}, max_gain{+3.0};
};

/// Mute breaths quieter than a threshold or shorter than a length (sips, noise).
class MuteSmallBreaths final : public Macro { /* … */ };

/// Mute selected breaths and, optionally, mark them cuttable so a following
/// StripSilence tighten/remove can close the space they occupied.
class RemoveBreaths final : public Macro { /* … */ };
```

Interaction with `StripSilence` (`silence-and-gaps.md`): tighten and remove never cut a breath region unless that breath is muted **and** marked cuttable. Gate mode never applies gap gain to breath regions; breaths are controlled only from the lane.

---

## 7. Breath Lane UI (`ui/breath_lane/`)

Follows the UI rule: it reads analysis, effective state and cached renders, and turns gestures into edits through `features/breaths`. It never processes audio.

**Layout**
- A horizontal strip docked at the bottom of the note editor, sharing its **output time axis**, zoom and scroll. Resizable height; collapsible to a thin bar showing breath markers only.
- **Lane header (left):** HPF on/cutoff, compressor (threshold, ratio, attack, release, makeup, knee), limiter (ceiling, release, true-peak), lane gain fader, mute, solo; input/output meters and a gain-reduction meter. Detection sensitivity slider and a "re-detect" button.
- **Clips:** each breath event drawn as a waveform (breaths are noise, so a waveform reads better than a pitch blob). The processed waveform is drawn solid; the unprocessed one as a faint outline behind it so the effect of the chain is visible. Muted clips are hatched.
- **Note editor tie-in:** breath positions are marked with thin ticks on the note editor's timeline; selecting a phrase can optionally select its breaths too.

**Gestures**

| Gesture | Result |
|---|---|
| Drag clip vertically | `clip_gain_db` |
| Drag top corners | `fade_in_s` / `fade_out_s` |
| Drag clip edges | `trim_start_s` / `trim_end_s` (inside the detected bounds only) |
| Double-click clip | Mute toggle |
| Alt-drag across the lane | Mark a new breath region (`breath_assignment`) |
| Right-click clip | Split / merge / "not a breath" |
| Click-select + keyboard | Gain nudges ±1 dB, mute, delete-as-mute |

**Monitoring:** Solo plays breaths only (processed); a "notes only" button mutes the lane for comparison. Solo and the comparison buttons are monitoring states and never enter the edit list or the rendered output.

---

## 8. Render integration

For each gap in a render region:

```
gap output = render_gap(gap)                         // silence, noise, tail; breath regions skipped
           + Σ render_breath_event(ev) for ev in gap // placed at to_output(ev.start)
           + fill(ev) for muted or turned-down ev    // room tone or gated level (§2)
```

- Breath events always render with slope 1 (they are never time-stretched). If a layout change moves a breath, it moves whole.
- Splices between a breath and its surrounding gap use the shared equal-power splice from `note-stretching.md` §5, only when one side is edited.
- Passthrough rule (§2) applies first; if it holds, the gap is copied whole.

## 9. Effect on audio

- Only breath regions change, unless a breath is muted or reduced, in which case the fill also changes that region.
- Notes, tails, silence and noise regions are untouched by any breath edit.
- With all breath edits removed and the chain bypassed, output is bit-identical to the source.
- Processing is deterministic per event; the same edits always give the same samples.

## 10. Split / join decisions

- **Dynamics in `core/dsp/dynamics/`, not inside the breath module** — they are general processors with more than one future user (split class rule 3; split function rule 4).
- **`Compressor` and `Limiter` are separate classes** — different state, latency and parameters; users enable them independently (split class rule 1).
- **No `DynamicsProcessor` base class** — the chain calls them in a fixed order; a common interface would have no polymorphic caller (join rule 5).
- **`BreathChain` is one plain struct holding the generic parameter structs** — all chain settings change together in the lane header and are folded together (join rule 1, data clump). The parameter structs themselves are generic (`core/types/dynamics_params.h`) so `core/dsp` never depends on anything breath-specific.
- **Detection separate from the timeline builder** — different models and tuning; both stay pure, and the orchestrator handles call order (split function rule 4).
- **One `BreathLevel` macro, not per-kind variants** — inhale/exhale differ only in which phrase is used (keep-together rule 3).

## 11. Tests

**Unit**
- `detect_breaths` on labelled vocals: event F1 ≥ target set in Phase 5 (onset/offset within ±30 ms); sibilants and lip smacks not classed as breaths on the synthetic set (noise bursts with sibilant-like and breath-like spectra; impulsive clicks).
- `Compressor`: static curve matches threshold/ratio/knee within ±0.2 dB on stepped sine levels; attack/release times within ±10 % on a level step.
- `Limiter`: output never exceeds ceiling on random signals (sample peak; true peak when enabled, measured with 4× oversampling); latency equals reported value.
- `HighPass`: −3 dB at cutoff ±2 %.
- RT tests: no allocation inside `process()` for all three (allocation-counting hook).

**Contract**
- No breath edits + bypassed chain → gap rendered bit-identical.
- Same event rendered twice (in different orders and threads) → identical hash.
- Muting a breath changes samples only inside its bounds plus fade margins.

**Audio scenarios** (`tests/audio/scenarios/breaths/`)
- `breaths_lane_minus8db` — breath loudness −8 ± 0.3 dB; notes bit-identical.
- `breaths_compress_limit` — loud and quiet breaths: spread of breath loudness reduced; no breath above ceiling.
- `breath_level_minus18` — each breath 18 ± 1 dB below its phrase (within clamps).
- `mute_breath_room_tone_fill` — filled region spectrum within ±3 dB of the floor spectrum; click detector on fill edges.
- `strip_silence_keeps_breaths` — remove mode leaves all unmuted breath regions intact.

**Listening focus:** breaths that sound clipped at the start, pumping from the compressor, a muted breath that leaves an audible hole, sibilants wrongly treated as breaths, unnatural phrasing after breath removal.

## 12. Open questions

1. **Attached breaths (v2):** breathy onsets and releases inside notes. Needs frequency-domain separation (the note's noise layer frames classed `breath` become a separate stream). Would let the lane control breathiness inside notes too. Decide after the Phase 6 listening gate.
2. **Continuous bus mode:** an option to run the chain over a continuous breath stream (state carried between events), for users who expect DAW-bus behaviour. Costs cache independence; needs pre-roll of at least the release time per region.
3. **Mouth-noise lane:** clicks and lip smacks are already detected as gap `noise` regions. A second lane (or a filter in this one) could manage them the same way.
4. **Moving breaths:** should a breath be movable in time inside its gap (e.g. to sit closer to the next phrase)? Technically simple (layout already supports it); musically useful for comping.
5. **Default limiter ceiling** and **default "Breath level" offset**: tune with the listening sheet.

## Review log

*(structure-reviewer findings go here)*
