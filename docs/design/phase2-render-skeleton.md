# Design note: Phase 2 — render pipeline skeleton (passthrough)

- **Phase:** 2
- **Status:** reviewed (round 1 findings fixed; see Review log)
- **Spec sections:** architecture §4.5 (engines), §4.6 (render), §5.5 (pipeline), §5.6 (change-only
  rendering); ADR-0002, ADR-0005 §16; ADR-0006 (render API, proposed), ADR-0007 (JSON, proposed);
  AUDIO_VERIFICATION §1-3.
- **Folders / targets:** `src/core/dsp` (layer 0, new), `src/core/synthesis/engine` and
  `src/core/synthesis/passthrough` (layer 2, new), `src/core/render` (layer 4, new),
  `src/cli/scenario` (json-free data + synthetic analysis), `src/cli/scenario_json` (parser, links
  nlohmann), `src/cli/commands` (layer 7). Test-only: `tests/support/` (checks, gain engine).

## Purpose
Prove the plumbing from edits to audio before any synthesis exists: pick the edited notes, ask an
engine for them, blend the results into a copy of the original with crossfades, cache it, and verify
with objective checks that "no edit" and "outside the edit" are bit-identical to the source. Engines:
`PassthroughEngine` (returns the source segment) and a **test-only** gain engine.

## Scope decisions
1. **No analysis until Phase 3.** The renderer takes an `AnalysisResult` it is given. Tests and the
   CLI build one from explicit note spans (`make_scenario_analysis`, flat synthetic notes valid per
   `validate`). Phase 3 replaces it.
2. **Timing edits are not rendered yet.** `render_region` builds the TimeMap (`source_end` = source
   duration) and returns `not_implemented` if it is not the identity. Because of the Flex semantics
   (ADR-0005 §16) *one* timing edit anywhere makes the whole map non-identity, so any region of such
   a project is refused until Phase 6; that is accepted. Edits that `build_time_map` ignores
   (inactive or orphaned notes, edits past the undo cursor) do not trigger it. An empty source
   (0 frames) returns an empty buffer before the map is built (`source_end` would be the "unknown"
   origin value). Phase 6 replaces `not_implemented` with a stretch path; **the cache key must then
   include `time_map.anchors()`** (comment at the key builder, and a test added then).
3. **No `Project` type** (Phase 7): `render_region(project, track_id, …)` becomes explicit inputs.
4. **No real-time path** (`render_block`, lock-free reader) until Phase 9. `RenderCache` is a
   mutex-guarded LRU, documented not real-time safe.
5. **Crossfade shape default `equal_power`** per spec. For correlated signals it overshoots by up to
   +3 dB at the midpoint, and `linear` is amplitude-correct; this is exposed as
   `RenderParams::crossfade_shape` and flagged for the Phase 4 listening. To keep the passthrough
   engine exactly "the source" regardless, the blend **skips a zone whose engine output equals the
   pristine slice bit for bit** (a memcmp), so `PassthroughEngine` + any edits == source.
6. **JSON:** ADR-0007 (nlohmann/json, `cli*` and tests only). `core/` never sees JSON.
7. **Fades:** `EffectiveNote::fade_in/out/fade_shape` and every field except `gain` are ignored by the
   renderer. Engines decide what they realise; the test engine returns `not_implemented` for any
   edited field other than `gain`.
8. **Determinism is per build.** `sin/cos` and fused multiply-add differ across platforms, so gains
   are computed in `double`, cast once, and `core/dsp` and `core/render` compile with
   `-ffp-contract=off`. Stored golden hashes are never compared across platforms.

## Public types

### core/dsp — `crossfade.h`
```cpp
struct CrossfadeGains { float outgoing; float incoming; };

/// Gains at t in (0,1) (0 = all outgoing). Computed in double, cast once.
///  equal_power: outgoing = cos(t*pi/2), incoming = sin(t*pi/2).
///  linear:      outgoing = 1-t, incoming = t.
///  log:         incoming = 10^(-60 (1-t) / 20), outgoing = 10^(-60 t / 20): linear in dB from -60 dB
///               at the far end to 0 dB. It never reaches zero (a step of up to 0.001 remains at the
///               far end); use it only where that is acceptable.
/// Pre: 0 <= t <= 1 (asserted in debug, clamped in release). Pure; real-time safe.
[[nodiscard]] CrossfadeGains crossfade_gains(FadeShape shape, double t) noexcept;

/// A slice of a longer ramp: out[i] = a[i]*g.outgoing + b[i]*g.incoming with
/// t_i = (ramp_offset + i + 0.5) / ramp_length (the half-sample offset keeps t strictly inside
/// (0,1)). Pre: spans have equal size; ramp_length >= 1; ramp_offset >= 0;
/// ramp_offset + size <= ramp_length. `out` may alias `a` or `b`. Real-time safe, no allocation.
/// The whole-span fade is ramp_offset = 0, ramp_length = size.
void crossfade(std::span<const float> a, std::span<const float> b, std::span<float> out,
               FadeShape shape, std::int64_t ramp_offset, std::int64_t ramp_length) noexcept;
```
A ramp of length 0 (hard splice) never reaches this code: the renderer copies instead (no division
by zero).

### core/synthesis/engine — `synthesis_engine.h`
```cpp
class SynthesisEngine {
public:
    virtual ~SynthesisEngine() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;   // e.g. "passthrough"
    [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;   // bump when output changes
    [[nodiscard]] virtual std::string settings_key() const = 0;         // bytes; may be empty

    /// The edited audio of `note` over `out_range` (output axis). The renderer always passes the
    /// **whole blend zone** of the note (note span widened by the crossfade length, clamped to the
    /// recording) and slices the result itself, so engines never need sub-range consistency. The
    /// edit is applied as if the note extended over the whole range. Returns OutputAudio whose range
    /// equals `out_range` to the sample, with the source's channel count and sample rate (the
    /// renderer returns `internal` on a mismatch).
    /// Errors: not_implemented if it cannot realise `eff` (e.g. non-identity time_map);
    /// invalid_argument on a bad range; out_of_range from the source.
    /// Const and thread-safe; not real-time safe.
    [[nodiscard]] virtual Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                          const EffectiveNote& eff,
                                                          const TimeMap& time_map,
                                                          OutputRange out_range) const = 0;
};
```
(Interface with one product implementation is allowed for `SynthesisEngine`; the test engine is the
second implementation from day one. The spec folder `core/synthesis/interface/` is named `engine/`
here, ADR-0006.)

### core/synthesis/passthrough — `PassthroughEngine final`
`name() == "passthrough"`, `version() == 1`, empty `settings_key()`. Requires
`time_map.is_identity()`; returns `src.read(range)` placed at `out_range.start()`; ignores `eff`.

### core/render — `render.h`, `render_cache.h`, `render_key.h` (layer 4)
```cpp
inline constexpr Seconds k_max_crossfade{0.020};  // in core/edits/dirty_regions.h (see below)

struct RenderInputs {
    AudioSource source;
    std::shared_ptr<const AnalysisResult> analysis;  // must not be null
    std::shared_ptr<const EditList> edits;           // must not be null; edits->track_id() = track
};

struct RenderParams {
    Seconds crossfade_length{0.010};       // 0 <= xf <= k_max_crossfade; 0 = hard splice (tests)
    FadeShape crossfade_shape = FadeShape::equal_power;
    RenderCache* cache = nullptr;          // optional, not owned
};

struct RenderResult {
    OutputAudio buffer;                    // the request snapped to whole samples (see below)
    SourceRange source_range;              // with an identity map: the same samples on the source axis
    std::string cache_id;                  // short hex of the key; for reports, not unique
    std::vector<NoteId> notes_rendered;    // edited notes with a zone in the region, source order
    bool passthrough = false;              // exactly notes_rendered.empty()
};

[[nodiscard]] Result<std::shared_ptr<const RenderResult>>
render_region(const RenderInputs& inputs, OutputRange region, const SynthesisEngine& engine,
              const RenderParams& params);
```
`quality` is dropped until an engine needs it (a `RenderParams` field later).

**Crossfade length vs dirty margin (B5).** `k_max_crossfade` (20 ms, the spec's upper bound) lives in
`core/edits/dirty_regions.h` next to `DirtyParams`, with
`static_assert(k_max_crossfade.value() + 0.001 <= DirtyParams{}.crossfade_margin.value())`. Render
rejects `crossfade_length` outside `[0, k_max_crossfade]`. Callers of `dirty_regions` must use
`crossfade_margin >= k_max_crossfade + 1 sample` (documented on `DirtyParams`); the two fields are
the same concept at two layers (guard band vs actual ramp), so the names stay different and the doc
says so.

**Snapping (B6).** The region is converted with `Rounding::nearest` at both ends; the returned
`buffer.start` is `to_time_point<OutputAxis>(start_index, sr)`, so `output_range(buffer)` is the
request snapped to whole samples (differs from the request by at most half a sample). An empty
region returns an empty buffer.

**Algorithm.** Named internal steps (`validate_inputs`, `plan_runs`, `render_zone`, `blend_run`,
`make_cache_key`), data passed in a `RenderPlan` struct:
1. Validate: non-null analysis/edits; `analysis->source_id == source.id()`; `validate(*analysis)`;
   edit track vs notes (checked by `build_time_map`); `0 <= xf <= k_max_crossfade`; region inside
   `[0, duration]` (`out_of_range`), region end >= start. Empty source → empty buffer.
2. `build_time_map`; non-identity → `not_implemented`.
3. Snap the region to samples. Compute `eff` for every active note; the **edited** ones
   (`eff.is_edited`) become candidates. Notes are sorted and non-overlapping (Phase 1).
4. **Runs.** Let `m = round(xf * sr)` and, for note k, span `[n0,n1)` in samples (nearest) and zone
   `[n0-m, n1+m)` clamped to the recording. For consecutive edited notes k, k+1 with gap
   `g = n0(k+1) - n1(k)` samples:
   - `g <= m`: **merged**. The engines crossfade into each other, centred on the gap midpoint
     `c = n1(k) + g/2`, ramp length `L = 2m - g` (>= m), over `[c - L/2, c + L/2)`. Both engines
     cover that range because each zone extends `m` past its note (`c + L/2 = n1(k) + m`).
   - `m < g <= 2m`: **separate**, but each note's outer ramp to the original is shortened to
     `r = g/2` on the facing side so the two ramps meet at the midpoint and never overlap.
   - `g > 2m`: separate, full ramps `r = m`.
   An *edited run* is a maximal sequence of merged notes. Ramps to the original exist only at the
   outer edges of a run, of length `r` as above (`m` on a free side). Ramps are defined on
   **unclamped sample coordinates** and truncated at sample 0 / the end, so a note 3 ms from the
   start has a shorter visible ramp, never a rescaled one. `m == 0`: all ramps are hard splices.
5. If a cache is given: build the key (below) and look it up; **a hit returns here**, before any
   engine call.
6. Pristine `P = source.read(region)`; `out = P` (copy). For each run intersecting the region: ask
   the engine for the **whole zone of every note in the run** (never a sub-range), check channels and
   sample rate, and **skip the note if its zone equals the pristine slice bit for bit**.
7. Blend each run from the **pristine** slice, never from the partially blended `out`:
   - inside a note's span, away from any inter-note ramp: `out = engine` (a copy, no multiply);
   - inter-note ramp (merged): `crossfade(engine_k, engine_k+1, …)`;
   - outer edge ramp: `crossfade(pristine, engine, …)` rising, mirrored falling;
   - samples outside every zone are not written at all (bit-exact, rule 5). Multichannel: per
     channel, same weights.
8. Store in the cache (if any) and return.

**Cache key (B4).** A canonical byte string, every variable-length field **length-prefixed** (no
`"ab"+"c"` vs `"a"+"bc"` ambiguity): format version, source id, analysis `version`, engine `name`,
`version`, `settings_key`, crossfade length (bits) and shape, snapped region (sample range, sample
rate), and for each *edited* note whose zone intersects the region: id, span (bit patterns), a
field-by-field serialisation of its `EffectiveNote`, and a **128-bit content fingerprint of the
whole `Note`** (two independent 64-bit FNV-1a streams; the analysis `version` alone is not
content-unique in tests and scenarios). Everything but that fingerprint compares exactly; the
fingerprint's collision probability is ~2^-128 and is stated as such in the header. Undo returns to
an earlier key and hits the earlier entry.
```cpp
class RenderCache {
public:
    explicit RenderCache(std::size_t max_bytes);
    /// Null on a miss. Marks the entry most recently used. Thread-safe; allocates; not RT-safe.
    [[nodiscard]] std::shared_ptr<const RenderResult> get(std::string_view key);
    /// Inserts or replaces; evicts least-recently-used entries while over budget. An entry larger
    /// than the whole budget is dropped silently. Thread-safe.
    void put(std::string key, std::shared_ptr<const RenderResult> value);
    /// Bytes counted: audio samples + the key string + notes_rendered ids of every entry.
    [[nodiscard]] std::size_t size_bytes() const;
    [[nodiscard]] std::size_t entry_count() const;
    void clear();
};
```
Class invariant `size_bytes() <= max_bytes` (5 members: mutex, LRU list, index, bytes, budget).
Hits return the shared pointer: no audio copy (S8).

### Error codes
`ErrorCode::not_implemented` (ADR-0006): "valid request that this build or engine cannot do yet".
Distinct from `unsupported_format` (a file format). Added to `to_string(ErrorCode)` and its test.

### core/types additions (`edit.h`)
`to_string` and `parse_*` for `EditCategory`, `EditOp`, `EditParam`, `FadeShape` (pure, total,
real-time safe; round-trip tested for every enumerator).

### Test-only (not in `src/`)
- `tests/support/gain_engine.h`: `GainTestEngine` multiplies the zone by `to_linear_gain(eff.gain)`;
  `not_implemented` if any other field is edited. Deleted in Phase 4.
- `tests/support/audio_checks.h`: `bit_identical`, `first_difference`, `find_clicks`, `sanity`
  (NaN/Inf count, DC dBFS, peak), `hash_audio`, `length_matches`, unit-tested on synthetic signals.
  The click threshold is a new *initial* engineering tolerance in `tests/tolerances.h`, recorded in
  the report as "pending human review". Two permanent calibration tests pin it: a 10 ms crossfade is
  clean and a 0 ms splice is detected.
- `tests/CMakeLists.txt` gains `add_subdirectory(audio)` with the `audio` label.

### CLI and scenarios
- `cli/scenario` (json-free): `Scenario`, `ScenarioNote {id, start_s, end_s}`,
  `make_scenario_analysis`. Tests may link it (layer-7 targets are free for tests; no JSON leaks).
- `cli/scenario_json`: `parse_scenario(path)`. Fields per AUDIO_VERIFICATION §2 plus `notes`. Rules:
  unknown top-level keys are errors (with the JSON path); `expect` and `algorithm` are accepted,
  ignored by the checks, and echoed in `report.md`; `source` resolves relative to the scenario file,
  falling back to the repo root; edits use the `parse_*` names and target `{note_id}` or
  `{note_index}`. Each scenario edit is one `EditList::add` group.
- `anno-cli render <scenario.json> --out out.wav` and
  `render --source a.wav --notes notes.json --edits e.json --out out.wav`; `--engine passthrough`.
- `anno-cli audition <scenario.json> --out <dir>`: `original.wav`, `edited.wav`,
  `original_matched.wav` (original scaled by the RMS ratio; the ratio is reported), `difference.wav`,
  `edits.json`, `report.md` (objective checks with measured values and PASS/FAIL, never a quality
  judgement), `INDEX.md`.
- Only `passthrough` is selectable in the CLI.

## Dependencies and CMake (B2)
`dsp` → types. `synthesis/engine` → types. `synthesis/passthrough` → engine, types. `render` → types,
dsp, edits, timemap, engine, plus `Threads::Threads` (mutex). `cli/scenario` → types, edits.
`cli/scenario_json` → scenario, `nlohmann_json::nlohmann_json`. `cli/commands` → render, passthrough,
io, scenario, scenario_json. Exact `anno_external_allowed` edits:
- `core/render` may link `Threads::Threads`;
- `^cli(/[a-z0-9_]+)?$` may link `nlohmann_json::nlohmann_json`.
Layer-rule tests added: `core/render` linking nlohmann is rejected; `cli/scenario_json` linking it is
accepted. `scripts/check_includes.py` is checked against the new include paths. `core/dsp` and
`core/render` get `-ffp-contract=off`.

## Effect on audio
First rendering code. Empty edit list or non-edited notes: output == source, bit-identical. With a
gain edit (test engine) only each edited note's zone `[start - xf, end + xf]` changes; the weight-1
interior is a copy of the engine output, not `x * 1.0f`. Objective checks (`audio` label): null,
undo-null, untouched-region, click detector, sanity, determinism, length. Round-trips arrive with the
engines that change pitch/timing/level. No listening gate this phase (a gain step is not a quality
question); a package is still built to exercise the CLI.

## Split / join decisions
- `crossfade_gains` + sliced `crossfade` kernel: the renderer calls the kernel, so the ramp formula
  exists once (§5 split 4, rule of three).
- `render_region` is an orchestrator of the named steps above; `RenderPlan` bundles run/zone data
  (data clump, join-classes 1).
- `RenderCache` separate from the renderer: different reason to change (eviction vs rendering).
- `cli/scenario` split from `cli/scenario_json` so nothing but the parser depends on JSON.

## Thread safety / real time
`crossfade*`, `to_string`/`parse_*`: real-time safe. Everything else analysis-thread: allocates, may
fail with `Result`. `render_region` is thread-safe for distinct inputs; a shared `RenderCache` is
safe (mutex). Nothing runs on the audio thread.

## Errors
| Where | Code | When |
|---|---|---|
| `render_region` | `invalid_argument` | null analysis/edits, source/analysis id mismatch, invalid analysis, `crossfade_length` outside `[0, k_max_crossfade]`, region end < start, track mismatch |
| | `out_of_range` | region outside `[0, duration]` |
| | `not_implemented` | non-identity TimeMap, or an engine that cannot realise `eff` |
| | `internal` | engine returned the wrong range, channel count or sample rate |
| `RenderCache::put` | none | oversize entries dropped silently |
| scenario parse | `invalid_argument`, `not_found`, `corrupt_data` | bad field (with JSON path) / missing file / malformed JSON |

## Tests
- Unit: `crossfade` (sums per shape, slices of a ramp concatenate to the whole ramp, aliasing, log
  formula), name parsers (round-trip), `PassthroughEngine`, `RenderCache` (LRU order, budget,
  oversize, TSan thread test), `render_region` (every error, empty region/empty source, snapped
  start, hard splice, **blend runs: gap 0, gap < m, m < gap <= 2m, gap > 2m, note within m of sample
  0 and of the end**, cancelled-out edit, timing edit on an inactive/orphaned note is not an error,
  a timing edit refused for a far region, `passthrough == notes_rendered.empty()`), key tests (two
  analyses differing only in `pitch_center` give different keys; engine version/settings change the
  key; length-prefix ambiguity), `audio_checks` on synthetic signals, scenario parsers.
- Contract (300 seeded cases): (1) empty edits → render == source, random regions; (2) random edits
  then full undo → bit-exact; (3) gain edits: every sample outside `dirty_regions` (margin default,
  xf at `k_max_crossfade`) is bit-identical; (4) `PassthroughEngine` over random edits == source;
  (5) `render(A ∪ B) == render(A) ++ render(B)` for **aligned** regions; (6) determinism (hash) and
  cache hit == miss; (7) an engine returning a different channel count is `internal`.
- Audio (`ctest -L audio`): null, undo-null, untouched, click (clean at 10 ms, detects a 0 ms
  splice), sanity, determinism, length.
- CLI process tests: `render` and `audition` on a generated fixture; exit codes.

## Open questions (for a human, non-blocking)
1. Crossfade default `equal_power` vs `linear` (decide at Phase 4 listening).
2. Click-detector threshold (initial value, review before Phase 4 relies on it).
3. `notes` list in scenarios is a Phase 2 stand-in until Phase 3 analysis.

## Review log
Round 1 (structure-reviewer): CHANGES REQUIRED, 6 blocking, 11 should-fix. All addressed above:
B1 ADR-0006/0007 written; B2 CMake edits listed; B3 runs and ramp rules; B4 key reorder, length
prefixes, note fingerprint, engine `version()`; B5 `k_max_crossfade`; B6 snapping; S1 bit-exact skip;
S2 sliced kernel; S3 `cli/scenario` split; S4 scenario rules; S5 `not_implemented`; S6 log formula;
S7 whole-zone engine calls; S8 shared_ptr results, `cache_id`, `shared_ptr<const EditList>`; S9
timing-edit consequences; S10 click threshold pending review; S11 per-build determinism and
`-ffp-contract=off`.
