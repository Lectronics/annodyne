# Design note: Note stretching

- **Phase:** 5 (Timing)
- **Status:** draft
- **Spec sections:** `melodyne-core-architecture.md` §2 (time axes), §4.4–4.6, §5.3–5.6; `melodyne-implementation-guide.md` B8 (resynthesis), Part B §11–12 (Time Tool, Attack Speed)
- **Folders / CMake targets:**
  - `src/core/timemap/` — region segmenting, stretch allocation (layer 3)
  - `src/core/analysis/perceived_pitch/` — vibrato model estimation (layer 1)
  - `src/core/synthesis/stretch/` — pitch-curve regeneration, region rendering, splicing (layer 2)

## Purpose

Change a note's duration (from `time` and `attack` edits) without smearing consonants, slowing the vibrato, making noise sound watery, or clicking at joins. A note is cut into regions that need different treatment, each region is stretched with the method that suits it, and the regions are spliced back together with aligned crossfades. Planning (what goes where, at what ratio) is separate from rendering (making samples), so the plan can be inspected, tested and drawn in the UI without rendering audio.

## Overview

```
Note + NoiseTrack + EffectiveNote
        │
segment_for_stretch()        → NoteSegments   (attack / sibilant / steady / release)
        │
allocate_stretch()           → StretchAllocation (ratio per region, anchors for the TimeMap)
        │
build_time_map() (existing)  → TimeMap        (uses the anchors; resolves neighbours)
        │
estimate_vibrato()           → VibratoModel   (from analysis, cached)
regenerate_pitch_curve()     → StretchedPitchCurve (drift warped, vibrato rate kept)
        │
plan_note_stretch()          → NoteStretchPlan (everything above + splice points + engine choice)
        │
render_note_stretch()        → AudioBuffer    (per region render + aligned splices)
```

---

## Public types

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `RegionKind` | enum class | — | — | value |
| `StretchRegion` | struct | — | `NoteSegments` | value |
| `NoteSegments` | class | regions contiguous, non-overlapping, cover `[source_start, source_end]` exactly | caller | value |
| `StretchAllocation` | struct | — | caller | value |
| `VibratoModel` | struct | — | analysis cache | `shared_ptr<const>` |
| `StretchedPitchCurve` | struct | — | caller | value |
| `SplicePoint` | struct | — | `NoteStretchPlan` | value |
| `NoteStretchPlan` | struct | — | render job | value |

```cpp
namespace melo::timemap {

enum class RegionKind { attack, sibilant, steady, release };

struct StretchRegion {
    RegionKind kind;
    SourceTime start;          // source time
    SourceTime end;            // source time, end > start
    double weight;             // share of the stretch this region absorbs (from params)
    double min_ratio;          // lower bound on output/source duration
    double max_ratio;          // upper bound
};

/// Contiguous regions covering one note exactly, in source-time order.
class NoteSegments {
public:
    [[nodiscard]] static Result<NoteSegments> make(NoteId note, std::vector<StretchRegion> regions);
    [[nodiscard]] std::span<const StretchRegion> regions() const noexcept;
    [[nodiscard]] NoteId note() const noexcept;
private:
    NoteId note_;
    std::vector<StretchRegion> regions_;   // invariant: contiguous, ordered, non-empty
};

struct StretchAllocation {
    NoteId note;
    std::vector<double> ratios;            // one per region, output/source duration
    std::vector<TimeAnchor> anchors;       // (SourceTime, OutputTime) pairs at region edges
    Seconds achieved_length;               // equals target unless clamped
    bool clamped;                          // true if limits stopped us reaching the target
};

} // namespace melo::timemap

namespace melo::analysis {

/// Vibrato as an analytic signal: mod(s) = extent(s) * cos(phase(s)).
struct VibratoModel {
    NoteId note;
    FrameGrid grid;                        // source time
    std::vector<double> rate_hz;           // instantaneous vibrato rate per frame
    std::vector<Cents> extent;             // instantaneous half-depth per frame
    std::vector<double> phase_rad;         // unwrapped phase per frame
    double periodicity;                    // 0..1 confidence that this is real vibrato
    bool present;                          // false → treat modulation as plain curve
};

} // namespace melo::analysis

namespace melo::synthesis {

struct StretchedPitchCurve {
    NoteId note;
    FrameGrid grid;                        // OUTPUT time, same hop as analysis
    std::vector<Cents> f0;                 // final pitch per output frame
    std::vector<Decibels> tremolo;         // amplitude modulation regenerated with vibrato
};

struct SplicePoint {
    OutputTime at;                         // centre of the crossfade, output time
    Samples read_offset;                   // alignment shift applied to the incoming side
    Seconds fade_length;
    float correlation;                     // 0..1 match quality at the chosen offset
    bool low_confidence;                   // correlation below threshold → longer fade used
};

enum class RegionEngine { verbatim, harmonic_noise, psola, phase_vocoder, noise_regen };

struct NoteStretchPlan {
    timemap::NoteSegments segments;
    timemap::StretchAllocation allocation;
    StretchedPitchCurve pitch;
    std::vector<RegionEngine> engines;     // one per region
    std::vector<SplicePoint> splices;      // one per internal region boundary + note edges
    std::uint64_t noise_seed;              // hash(note id, analysis version)
};

} // namespace melo::synthesis
```

**Layer note:** `NoteStretchPlan` holds `timemap` types but lives in `synthesis`, which is a lower layer than `timemap`. See Open question 1.

---

## Public functions

### 1. `segment_for_stretch` (`core/timemap/`)

```cpp
/// Split a note into stretch regions using its analysed attack end, release start
/// and the noise track's frame classes. All times are source time.
/// Returns regions covering the note exactly. Error if the note is shorter than
/// params.min_note or the analysis has no frames for it.
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<NoteSegments> segment_for_stretch(const Note& note,
                                                       const SegmentParams& params);

struct SegmentParams {
    Seconds min_region{0.010};             // regions shorter than this merge into a neighbour
    Seconds min_note{0.030};
    // weights: share of the stretch each region kind absorbs
    double w_attack = 0.0, w_sibilant = 0.25, w_steady = 1.0, w_release = 0.3;
    double sibilant_min = 0.8, sibilant_max = 1.3;
    double steady_min = 0.25, steady_max = 4.0;
    double release_min = 0.6, release_max = 2.0;
};
```

Rules:
- `attack` = `[source_start, attack_end]`. Weight 0; its ratio comes only from `attack_time_scale`.
- `sibilant` = runs of frames classed `sibilant` or `breath` outside the attack.
- `release` = `[release_start, source_end]`.
- `steady` = everything else that is voiced. A note with no steady region (all noise) gives its weight to the sibilant regions.

### 2. `allocate_stretch` (`core/timemap/`)

```cpp
/// Distribute a requested note length over the regions.
/// Each region's ratio is r_i = clamp(1 + w_i * k, min_i, max_i); k is solved so
/// that Σ len_i * r_i == target (bisection, tolerance 1 sample).
/// The attack region uses r = attack_time_scale regardless of k.
/// Returns anchors at every region edge. If the limits make the target unreachable,
/// returns the closest length with clamped = true (never an error).
/// Error only if target <= 0.
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<StretchAllocation> allocate_stretch(const NoteSegments& segments,
                                                         Seconds target_length,
                                                         double attack_time_scale);
```

The anchors feed the existing `build_time_map`, which also resolves overlaps with neighbouring notes (architecture spec §5.4 step 5). A neighbour gives up gap first, then its steady region, never its attack.

### 3. `estimate_vibrato` (`core/analysis/perceived_pitch/`)

```cpp
/// Model the note's fast pitch modulation as extent(s) * cos(phase(s)).
/// Uses the analytic signal (Hilbert transform) of Note::pitch_mod, band-limited
/// to params.rate_range. present = false if periodicity < params.min_periodicity
/// or median extent < params.min_extent.
/// Pure; thread-safe; not RT-safe. Result is cached with the analysis.
[[nodiscard]] Result<VibratoModel> estimate_vibrato(const Note& note,
                                                    const VibratoParams& params);

struct VibratoParams {
    double rate_min_hz = 3.0, rate_max_hz = 9.0;
    Cents min_extent{15.0};
    double min_periodicity = 0.5;
};
```

### 4. `regenerate_pitch_curve` (`core/synthesis/stretch/`)

```cpp
/// Build the stretched note's pitch curve on an output-time grid.
///   centre  → from EffectiveNote (unchanged by stretching)
///   drift   → time-warped:  drift_out(τ) = drift_scale * drift(M⁻¹(τ))
///   vibrato → rate kept, not warped:
///               φ_out(τ) = φ0 + ∫ 2π · rate(M⁻¹(τ)) dτ
///               mod_out(τ) = mod_scale * extent(M⁻¹(τ)) * cos(φ_out(τ))
///   tremolo → regenerated with the same phase as vibrato
/// If vibrato.present is false, mod is time-warped like drift.
/// End-phase matching: the rate is adjusted by at most params.max_rate_adjust so
/// φ_out at the release anchor matches the original phase there (± π/8),
/// which keeps the release's natural pitch movement continuous.
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<StretchedPitchCurve> regenerate_pitch_curve(
        const Note& note, const EffectiveNote& eff,
        const analysis::VibratoModel& vibrato, const TimeMap& map,
        const PitchRegenParams& params);

struct PitchRegenParams {
    double max_rate_adjust = 0.03;         // ±3 %
    bool regenerate_tremolo = true;
};
```

Shortening falls out of the same formula: fewer cycles fit, so whole cycles are dropped, and end-phase matching makes the last cycle finish cleanly.

### 5. `find_splice_offset` and `splice` (`core/synthesis/stretch/`)

```cpp
/// Find the read offset for the incoming segment that best matches the outgoing
/// one, by normalised cross-correlation over the fade length.
/// Search range: ± half a pitch period when voiced, ± params.unvoiced_search otherwise.
/// Returns the offset and its correlation (0..1).
/// Pure; thread-safe; RT-safe if scratch is preallocated (not required here).
[[nodiscard]] SpliceOffset find_splice_offset(std::span<const float> outgoing_tail,
                                              std::span<const float> incoming_head,
                                              Hz local_f0,
                                              const SpliceParams& params);

/// Equal-power crossfade of outgoing into incoming, writing into dest.
/// Lengths must match fade_length; error otherwise.
[[nodiscard]] Result<void> splice(std::span<const float> outgoing,
                                  std::span<const float> incoming,
                                  std::span<float> dest,
                                  dsp::FadeShape shape);

struct SpliceParams {
    double fade_periods = 2.0;             // fade = fade_periods pitch periods...
    Seconds fade_min{0.005}, fade_max{0.020};   // ...clamped to 5–20 ms
    Seconds unvoiced_search{0.002};
    float low_correlation = 0.5f;          // below this: double the fade length, flag it
};
```

The alignment offset moves the **read position** inside the incoming region. It never changes output length, so the length check stays exact.

### 6. `plan_note_stretch` (`core/synthesis/stretch/`)

```cpp
/// Combine segmenting, allocation, pitch regeneration, engine choice and splice
/// planning into one inspectable plan. No audio is produced.
/// Engine choice per region:
///   attack   → verbatim if the note has no pitch or formant edit, else
///              harmonic_noise (or phase_vocoder for poly) at the attack ratio
///   sibilant → noise_regen
///   steady   → harmonic_noise (default); psola if ratio ∈ [0.8, 1.25], the region
///              is voiced with confidence ≥ 0.8, and the Phase 4 ADR made psola
///              preferred; phase_vocoder (PGHI) for poly / universal algorithms
///   release  → same engine as steady
/// Returns an error only if a sub-step returns one.
/// Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<NoteStretchPlan> plan_note_stretch(const Note& note,
                                                        const EffectiveNote& eff,
                                                        const TimeMap& map,
                                                        const AnalysisResult& analysis,
                                                        const StretchPlanParams& params);
```

### 7. `render_note_stretch` (`core/synthesis/stretch/`)

```cpp
/// Render a planned note into output time. Each region is rendered by its engine
/// with a margin of one fade length on each side, then the regions are joined with
/// find_splice_offset + splice. Noise uses plan.noise_seed, so output is deterministic.
/// Returns exactly round(achieved_length * sample_rate) samples.
/// Not RT-safe (runs on render threads; result goes to RenderCache).
[[nodiscard]] Result<AudioBuffer> render_note_stretch(const NoteStretchPlan& plan,
                                                      const AudioSource& src,
                                                      const AnalysisResult& analysis,
                                                      const StretchRenderParams& params);
```

Called from the existing `SynthesisEngine::render_note` path whenever the note's `TimeMap` slope is not 1 anywhere inside it.

---

## How each region is rendered

| Region | Engine | What happens to the samples |
|---|---|---|
| attack (no pitch/formant edit) | `verbatim` | Original samples copied, with slope = `attack_time_scale` only if ≠ 1 (then rendered as below) |
| attack (edited) | `harmonic_noise` / `phase_vocoder` | Re-synthesised at ratio = `attack_time_scale` |
| sibilant / breath | `noise_regen` | Band energies time-warped through `M⁻¹`; fresh noise generated through them with `noise_seed`; never pitch-shifted |
| steady (voice) | `harmonic_noise` | Harmonic frequencies = `f0_out(τ) × (f_k / f_1)` from analysis at `M⁻¹(τ)` (keeps inharmonicity); amplitudes from the envelope at `M⁻¹(τ)`, formant-warped by the edit; phase integrated from frequency; noise layer as above |
| steady (small ratio, clean) | `psola` | Grains around pitch marks from the source, repeated or dropped; grain spacing from `f0_out` |
| steady / release (poly, universal) | `phase_vocoder` (PGHI) | STFT re-synthesis at hop ratio = local slope; phase reset at region anchors |
| release | same as steady | Lower weight, so it changes length less |

## Effect on audio

- Changes rendered output only for notes whose `TimeMap` slope ≠ 1 somewhere inside them, plus the crossfade margins of their neighbours.
- **Stays bit-identical:** every sample outside `dirty_regions()`; attack regions of notes with no pitch/formant edit and `attack_time_scale = 1` (before the splice fade); any note whose stretch is undone.
- Total note length equals `achieved_length` to the sample.
- Vibrato rate and depth are preserved; pitch drift, envelope and noise are time-warped.

## Split / join decisions

- **Planning split from rendering** (`plan_note_stretch` vs `render_note_stretch`): different reasons to change, and the plan is testable without audio (coding-standards §5, split a class rule 3, split a function rule 4).
- **`segment_for_stretch` and `allocate_stretch` kept separate**: segmenting depends on analysis, allocation is pure maths; each is tested with synthetic inputs (split rule 4).
- **`find_splice_offset` and `splice` separate**: the offset search is reusable by PSOLA grain placement (split rule 4).
- **`regenerate_pitch_curve` kept as one function** (~60 lines): drift warp, vibrato phase integration and end-phase matching only make sense together and share all their state (keep-together rules 1 and 5).
- **No new `StretchEngine` interface**: region engines reuse the existing `SynthesisEngine` implementations; a second interface would have one caller (join rule 5).

## Dependencies

- `core/timemap`: `core/types`, `core/edits`.
- `core/synthesis/stretch`: `core/types`, `core/dsp`, `core/synthesis/harmonic_noise`, `core/synthesis/psola`, `core/synthesis/phase_vocoder`, `core/analysis/perceived_pitch` (for `VibratoModel`). Needs the `TimeMap` *type* (Open question 1).

## Tests

**Unit (synthetic signals)**
- `segment_for_stretch`: regions contiguous and cover the note exactly; short regions merge; all-noise note has no steady region.
- `allocate_stretch` (property test, random lengths and targets): Σ region lengths × ratios = target ± 1 sample; ratios within limits; attack ratio = `attack_time_scale`; `clamped` set exactly when the target is unreachable; target = original length gives all ratios = 1.
- `estimate_vibrato`: synthetic 5.5 Hz, ±50 cent vibrato → rate 5.5 ± 0.1 Hz, extent 50 ± 3 cents; straight tone → `present = false`.
- `regenerate_pitch_curve`: 2× and 0.5× stretch of synthetic vibrato → rate 5.5 ± 0.2 Hz, extent ± 10 %; drift duration doubles; end phase within ± π/8 of the original.
- `find_splice_offset`: two shifted copies of a harmonic tone → finds the shift ± 1 sample, correlation > 0.95.

**Contract**
- Ratio-1 plan renders the note bit-identical to the source.
- Output sample count = `round(achieved_length × sample_rate)`.
- Unedited attack samples bit-identical (outside the splice fade).
- Same plan rendered twice → identical hash (noise seed works).

**Audio scenarios** (`tests/audio/scenarios/time/`)
- `stretch_vowel_1_5x`, `stretch_vowel_2x`, `shorten_vowel_0_5x` on a vibrato note — checks: vibrato rate, length, click detector, next-note onset ± 2 ms.
- `stretch_with_t_onset_2x` — attack region unchanged, no doubled consonant.
- `stretch_sibilant_word_1_5x` ("yes", "miss") — sibilant centroid ± 5 %, sibilant length ratio ≤ 1.3.
- `stretch_and_pitch_up_2st` — combined edit, all round-trips.
- `stretch_note_into_neighbour` — neighbour's attack unmoved, gap absorbed.
- `poly_stretch_chord_note_1_5x` (Phase 11) — unedited notes bit-identical.

**Listening focus:** consonant doubling or smearing, slow or fast vibrato after stretching, watery breath noise, buzz on long stretches, clicks at region joins.

## Open questions

1. **`TimeMap` type location.** `synthesis` (layer 2) needs the `TimeMap` type, but `core/timemap` is layer 3. Proposal: move the `TimeMap` *data type* (anchors, forward/inverse evaluation) into `core/types`, and keep `build_time_map` in `core/timemap`. This also matches the existing `SynthesisEngine::render_note` signature, which already takes a `TimeMap`. Needs an ADR before implementation.
2. **Default steady engine.** `harmonic_noise` vs `psola` for small ratios on clean voice. Decided by the Phase 4 listening gate; this note assumes `harmonic_noise` until then.
3. **Release weight.** 0.3 is a starting guess. Tune with the Phase 5 listening sheet; record the result in an ADR.
4. **Tremolo regeneration.** On by default. Confirm by listening that it doesn't add an artificial pulse on notes with irregular amplitude.

## Review log

*(structure-reviewer findings go here)*
