# Design note: Phase 2 — render pipeline skeleton (passthrough)

- **Phase:** 2
- **Status:** draft
- **Spec sections:** architecture §4.5 (engines), §4.6 (render), §5.5 (pipeline), §5.6 (change-only
  rendering); ADR-0002 (`OutputAudio`), ADR-0005 (§16 Flex-style handles); AUDIO_VERIFICATION §1-3.
- **Folders / targets:** `src/core/dsp` (layer 0, new), `src/core/synthesis/engine` and
  `src/core/synthesis/passthrough` (layer 2, new), `src/core/render` (layer 4, new),
  `src/cli/scenario` and `src/cli/commands` (layer 7), test-only `tests/support/` (checks, gain engine).

## Purpose
Prove the plumbing from edits to audio before any synthesis exists: pick the notes that need work,
ask an engine for them, blend the result into a copy of the original with crossfades, cache it, and
verify with objective checks that "no edit" and "outside the edit" are bit-identical to the source.
The only engines are `PassthroughEngine` (returns the source segment) and a **test-only** gain
engine that applies `EffectiveNote::gain`, to give the checks something real to measure.

## Scope decisions
1. **No analysis exists until Phase 3.** The renderer takes an `AnalysisResult` it is given. Tests
   and the CLI build one from explicit note spans (`make_scenario_analysis`: flat synthetic notes,
   valid per `validate`). Phase 3 swaps in real analysis without changing the renderer.
2. **Timing edits are not rendered yet.** `render_region` builds the `TimeMap` (Phase 1, with
   `source_end` = source duration) and returns `unsupported` if it is not the identity. Stretching
   audio is Phase 6; the Flex semantics are already in the map. A `PassthroughEngine` handed a
   non-identity map returns the same error (defence in depth).
3. **No `Project` type exists** (layer 6, Phase 7). `render_region(project, track_id, ...)` in the
   spec becomes `render_region(const RenderInputs&, const RenderRequest&)` with explicit inputs.
4. **No real-time block read yet.** `render_block` and the audio-thread cache path are Phase 9.
   `RenderCache` here is a mutex-guarded LRU, **not** real-time safe; this is stated in its doc.
5. **Crossfade shape default is `equal_power`** as the spec says. For two highly correlated signals
   (original vs a lightly edited copy) equal-power sums to up to +3 dB at the midpoint, and `linear`
   is the amplitude-correct choice. This is exposed as `RenderParams::crossfade_shape` and flagged
   for the Phase 4 listening session; it is not decided here.
6. **JSON.** `render --edits` and `audition` need JSON. ADR-0006 adds nlohmann/json (header-only,
   MIT, `FetchContent` like Catch2), linkable only from `cli*` and tests. `core/` never sees JSON;
   the project file format stays Phase 7.

## Public types

### core/dsp — `crossfade.h` (layer 0)
```cpp
struct CrossfadeGains { float outgoing; float incoming; };

/// Gains at position t in [0,1] (0 = all outgoing, 1 = all incoming). Pre: t in [0,1] (clamped in
/// release, asserted in debug). equal_power: sin/cos so out^2+in^2 = 1. linear: 1-t, t.
/// log: gains linear in dB from -60 dB to 0 dB (outgoing mirrored), reaching exactly 0 / 1 at the
/// ends. Pure; real-time safe.
[[nodiscard]] CrossfadeGains crossfade_gains(FadeShape shape, double t) noexcept;

/// out[i] = a[i]*g_out(t_i) + b[i]*g_in(t_i), t_i = (i + 0.5) / n. Pre: spans have equal size; `out`
/// may alias `a` or `b`. n == 0 is a no-op. Real-time safe (no allocation).
void crossfade(std::span<const float> a, std::span<const float> b, std::span<float> out,
               FadeShape shape) noexcept;
```
`FadeShape` already lives in `core/types/edit.h`. The half-sample offset keeps both ends strictly
inside (0,1): the first output sample is almost all `a`, never a duplicate of a boundary sample.

### core/synthesis/engine — `synthesis_engine.h` (layer 2)
```cpp
class SynthesisEngine {
public:
    virtual ~SynthesisEngine() = default;
    /// Stable name, e.g. "passthrough"; part of the cache key.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    /// Bytes that change when the engine's settings change; part of the cache key. May be empty.
    [[nodiscard]] virtual std::string settings_key() const = 0;

    /// The edited audio of `note` over `out_range` (output axis; the renderer passes the note span
    /// widened by the crossfade margin, clamped to the recording). The edit is applied as if the
    /// note extended over the whole range. Returns OutputAudio whose range equals `out_range` to the
    /// sample. Errors: unsupported if it cannot realise `eff` (e.g. non-identity time_map for the
    /// passthrough engine); invalid_argument on a bad range; out_of_range from the source.
    /// Thread-safe if the engine is const-correct (all engines here are); not real-time safe.
    [[nodiscard]] virtual Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                          const EffectiveNote& eff,
                                                          const TimeMap& time_map,
                                                          OutputRange out_range) const = 0;
};
```
(Allowed interface, coding-standards §5 join rule 5.) `ErrorCode::unsupported` is new (ADR-0006,
"the request is valid but this build or engine cannot do it yet").

### core/synthesis/passthrough — `passthrough_engine.h`
`class PassthroughEngine final : public SynthesisEngine`; `name() == "passthrough"`, empty
`settings_key()`. `render_note` requires `time_map.is_identity()` and returns
`src.read(start_index, count)` placed at `out_range.start()`; the samples are bit-identical to the
source. It ignores `eff` by design: it is the "nothing to synthesise" engine.

### core/render — `render.h`, `render_cache.h`, `render_key.h` (layer 4)
```cpp
struct RenderInputs {
    AudioSource source;
    std::shared_ptr<const AnalysisResult> analysis;  // immutable snapshot
    EditList edits;                                   // value; its track_id selects the track
};

struct RenderParams {
    const SynthesisEngine* engine;        // required, not owned; outlives the call
    Seconds crossfade_length{0.010};      // each side of an edited note; 5-20 ms (spec §5.5)
    FadeShape crossfade_shape = FadeShape::equal_power;
    RenderCache* cache = nullptr;         // optional, not owned
};

struct RenderResult {
    OutputAudio buffer;                   // exactly the requested range (ADR-0002)
    SourceRange source_range;             // part of the recording that was read
    std::string cache_key;                // canonical bytes, collision-free (see below)
    std::vector<NoteId> notes_rendered;   // edited notes the engine was asked for, in source order
    bool passthrough = false;             // true if no note intersected the range: pure copy
};

[[nodiscard]] Result<RenderResult> render_region(const RenderInputs&, OutputRange region,
                                                 const RenderParams&);
```
Spec `quality: "preview"|"final"` is dropped until an engine needs it (Phase 4); adding it later is
a `RenderParams` field, not a signature change.

**Algorithm (`render_region`)**, as a list of steps over small named helpers:
1. Validate: `engine != nullptr`; `analysis->source_id == source.id()`; analysis valid; edits'
   `track_id` matches the notes (validated by `build_time_map`); `crossfade_length <= dirty margin`
   (see contract 4); region inside `[0, source duration]` (else `out_of_range`).
2. `build_time_map(notes, edits, {source_end = source duration})`; not identity → `unsupported`.
   `repaired_notes` is ignored with an identity map (no timing edits means none).
3. Convert `region` to sample indices (`Rounding::nearest` on the start and on the end; the region
   is half-open and must satisfy `start <= end`; an empty region returns an empty buffer).
4. `out` = `source.read(region)`: the original, bit for bit.
5. For each active note in source order, `eff = effective_state(note, edits)`; skip unless
   `eff.is_edited`. The note's blend zone is `[start - xf, end + xf]` in samples, clamped to the
   recording; skip it if it does not intersect the region.
6. Ask the engine for the zone ∩ region (the engine renders only what is needed; the zone is
   always larger than the part kept because the ramp lies inside it).
7. Blend: inside the note span the weight is 1 (the engine output replaces the original); in each
   margin the original fades out and the engine output fades in (or the reverse) with
   `crossfade_gains`. Samples whose weight is exactly 0 are **not touched**. Notes are blended in
   source order, so overlapping margins of neighbouring edited notes are well-defined.
8. Cache: if `params.cache`, look up `cache_key` first and return a hit; store on a miss.

Blend zone and weights, per sample `i` (zone `[z0, z1)`, note `[n0, n1)`, margin `m = round(xf*sr)`):
`t = (i - z0 + 0.5) / m` on the rising ramp `i in [z0, n0)`; the falling ramp is its mirror; `m == 0`
gives a hard splice (allowed, the click detector will flag it).

**Cache key.** `cache_key` is a canonical byte string, not a hash: source id, analysis version,
engine `name` + `settings_key`, crossfade length and shape, region (sample range and sample rate),
and for each active note intersecting the zone-widened region its id, source span (bit patterns) and
a field-by-field serialisation of `EffectiveNote` *only if* `is_edited`. Undo returns to an earlier
key and hits the earlier entry. Equality is exact, so there are no hash collisions to reason about;
`std::hash<std::string>` is used only for bucketing.

```cpp
class RenderCache {
public:
    explicit RenderCache(std::size_t max_bytes);
    /// Returns the cached result, or empty. Marks the entry most recently used. Thread-safe.
    [[nodiscard]] std::optional<std::shared_ptr<const RenderResult>> get(std::string_view key);
    /// Inserts (or replaces) an entry; evicts least-recently-used entries while over budget. An
    /// entry larger than the whole budget is not stored. Thread-safe.
    void put(std::string key, std::shared_ptr<const RenderResult> value);
    [[nodiscard]] std::size_t size_bytes() const;
    [[nodiscard]] std::size_t entry_count() const;
    void clear();
};
```
Class: invariant `size_bytes() <= max_bytes`; members `mutex_`, LRU list, map, bytes, budget (5).
`get`/`put` take a lock and allocate: **not real-time safe** (Phase 9 adds a lock-free reader).

### Test-only (not in `src/`)
- `tests/support/gain_engine.h`: `GainTestEngine` multiplies the source segment by
  `to_linear_gain(eff.gain)`. Stays under `tests/` so no product code can select it; Phase 4 deletes
  it. It also checks `eff.gain` is the only edited field (else `unsupported`).
- `tests/support/audio_checks.h`: the objective checks as plain functions on `AudioBuffer`:
  `bit_identical`, `first_difference`, `find_clicks`, `sanity` (NaN/Inf count, DC offset dBFS, peak),
  `hash_audio`, `length_matches`. Each has unit tests on synthetic signals with known answers
  (sine, injected step, injected NaN). Thresholds from `tests/tolerances.h`; new engineering
  tolerances (click threshold) are added there with a justification, never loosened to pass.

### CLI and scenarios (layer 7)
- `src/cli/scenario` (`anno::cli::Scenario`): parses the scenario JSON of AUDIO_VERIFICATION §2,
  extended for Phase 2 with an explicit `notes` list (`[{"id","start_s","end_s"}]`, replaced by real
  analysis in Phase 3) and `edits` as `{category, op, param, value, target:{note_id|note_index}}`
  using the closed `EditParam` names (`to_string(EditParam)` / `parse_edit_param`, new in
  `core/types/edit.h`, pure and total). Errors are `Result` with the JSON path in the message.
  Returns `Scenario { std::string name; source path; std::vector<ScenarioNote>; std::vector<EditSpec>
  grouped as one EditList::add group per scenario edit }`.
- `anno-cli render <scenario.json|--source wav --edits edits.json> --out out.wav [--engine
  passthrough]`. Writes float32 WAV. Exit codes as `info`.
- `anno-cli audition <scenario.json> --out <dir>`: writes `original.wav`, `edited.wav`,
  `original_matched.wav` (original scaled by the RMS ratio so A/B level is fair; the scale is
  reported), `difference.wav`, `edits.json`, `report.md` (the objective checks with measured values
  and PASS/FAIL), and `INDEX.md` (AUDIO_VERIFICATION §3). Report text never judges sound quality.
- Engines the CLI can pick are wired here (layer 7): `passthrough` only. The gain test engine is not
  in `src/`, so scenarios that need it run only from the audio tests.

## Public functions added elsewhere
`std::string_view to_string(EditParam)`, `std::optional<EditParam> parse_edit_param(string_view)`,
`to_string(EditCategory/EditOp/FadeShape)` + parsers in `core/types/edit.h` (pure, real-time safe);
`ErrorCode::unsupported`.

## Effect on audio
First rendering code. With an empty edit list or only non-edited notes the output is the source,
bit-identical (rule 5). With a gain edit (test engine) only `[start - xf, end + xf]` changes. Notes
whose edits cancel (`is_edited == false`) are bit-identical. Objective checks (all `audio` label):
null, undo-null, untouched-region, click detector, sanity, determinism, length. Pitch/timing/level
round-trips arrive with the engines that change them. No audition package is a listening gate this
phase (a gain step is not a quality question), but one is built so the CLI path is exercised.

## Split / join decisions
- `crossfade_gains` (one point) and `crossfade` (a span) are separate: the renderer needs the weight
  per sample inside a larger loop; the span form is the reusable kernel (§5 split 4).
- `render_region` is an orchestrator of named steps (`validate_inputs`, `plan_blends`,
  `apply_blend`, `make_cache_key`) in `core/render/` internals; no step takes more than three
  parameters except through a `BlendPlan` struct (data clump, §5 join-classes 1).
- Interface + one engine in separate folders despite "one implementation": allowed for
  `SynthesisEngine`; the test engine is a second implementation from day one.
- `RenderCache` is separate from `render_region` (different reason to change: eviction policy vs
  rendering).

## Dependencies
`core/dsp` → `core/types`. `synthesis/engine` → `core/types`. `synthesis/passthrough` → engine,
types. `render` → types, dsp, edits, timemap, `synthesis/engine`. `cli/scenario` → types, edits;
`cli/commands` → render, passthrough, io, scenario. nlohmann/json: `cli/scenario` and tests only.
The layer table already contains every folder; `anno_external_allowed` gets one rule
(`nlohmann_json` for `cli*`), recorded in ADR-0006. No cycles.

## Thread safety / real time
`crossfade*`, `to_string`/parsers: real-time safe. Everything else is analysis-thread code:
allocates, may fail with `Result`. `render_region` is thread-safe for distinct inputs; sharing a
`RenderCache` across threads is safe (mutex). Nothing here runs on the audio thread (Phase 9).

## Errors
| Where | Code | When |
|---|---|---|
| `render_region` | `invalid_argument` | null engine, source/analysis id mismatch, invalid analysis, `crossfade_length` < 0 or > the dirty margin, region end < start, edit track mismatch |
| | `out_of_range` | region outside `[0, duration]` |
| | `unsupported` | non-identity TimeMap (Phase 6) or an engine that cannot realise `eff` |
| engines | as above | |
| `RenderCache::put` | none | oversize entries are dropped silently, documented |
| scenario parse | `invalid_argument`, `not_found`, `corrupt_data` | bad JSON path / missing file / malformed |

## Tests
- Unit: `crossfade` (energy/amplitude sums, endpoints, aliasing, shapes), `edit` name parsers
  (round-trip every enum value), `PassthroughEngine`, `RenderCache` (LRU order, budget, oversize,
  thread test under TSan), `render_region` (errors, empty region, hard splice, neighbour margins
  overlapping, cancelled-out edit), `audio_checks` on synthetic signals, scenario parser.
- Contract (property, 300 seeded cases, `tests/support/random_scenarios.h`): (1) empty edit list →
  render == source, bit-exact, for random regions; (2) random edits then full undo → bit-exact
  (undo-null); (3) gain edits on random notes: every sample outside `dirty_regions` (old=empty, new=edits,
  margin = DirtyParams default) is bit-identical; (4) `crossfade_length` accepted only up to the dirty
  margin; (5) render(region A ∪ B) == render(A) ++ render(B) sample for sample (region
  independence); (6) determinism: two renders hash equal; cache hit == miss result.
- Audio (`ctest -L audio`): null, undo-null, untouched-region, click detector (0 detections on a
  gain edit with a 10 ms crossfade, and *does* detect a 0 ms splice), sanity, determinism, length.
- CLI process tests: `render` and `audition` on a generated fixture WAV; exit codes.

## Open questions
1. Crossfade default `equal_power` vs `linear` for correlated material (decided at Phase 4 listening).
2. Scenario `notes` list is a Phase 2 stand-in; Phase 3 replaces it with `"analysis": "auto"`.
3. Whether the `RenderResult` should hold `shared_ptr<const OutputAudio>` to avoid copies on cache
   hits; Phase 2 copies on return and stores shared in the cache. Revisit with the Phase 9 block path.
4. Click detector threshold: the spec gives the method ("high-pass, jump vs local RMS") but no
   number. The first value is an engineering tolerance chosen against the synthetic cases and
   reported; a human should review it before Phase 4 relies on it.

## Review log
(pending: structure-reviewer)
