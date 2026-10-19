# Design note: Phase 0 foundations

- **Phase:** 0 · **Status:** implemented (phase closed 2026-10-19, see docs/reviews/phase0-review.md)
- **Spec sections:** architecture §1–3, §6; ROADMAP Phase 0; `.claude/rules/architecture.md`,
  `coding-standards.md`; AUDIO_VERIFICATION §1 (tolerances)
- **Targets:** `core/types` (layer 0), `core/io` (restricted, ADR-0001), `cli` (layer 7)

## Purpose

Make the right thing the easy thing: layer rules checked by CMake, units and time axes checked by the
type system, errors returned as values, and a minimal audio path (WAV → `AudioBuffer` → `AudioSource`)
plus a CLI that later phases extend.

## 1. Repository layout created in Phase 0

```
CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy  .gitignore
cmake/  AnnoOptions.cmake      # C++20, no extensions, output dirs build/<preset>/bin
        AnnoWarnings.cmake     # anno_warnings INTERFACE target
        AnnoSanitizers.cmake   # ANNO_SANITIZER = none | address_undefined | thread
        AnnoLayers.cmake       # layer table, anno_add_library/executable, anno_check_layers()
        AnnoTesting.cmake      # Catch2 v3 (FetchContent, pinned) + anno_add_test()
src/core/types/   units.h  error.h  result.h  contract.h  audio_buffer.{h,cpp}  audio_source.{h,cpp}
src/core/io/      wav.h  wav.cpp  detail/wav_format.h
src/cli/          main.cpp                         # executable anno-cli (dispatch only)
src/cli/commands/ info.{h,cpp}                     # library anno_cli_commands
tests/tolerances.h
tests/unit/       units_test.cpp result_test.cpp audio_buffer_test.cpp audio_source_test.cpp
                  wav_test.cpp cli_info_test.cpp
tests/contract/   units_contract_test.cpp  result_contract_test.cpp
                  layer_rules/{allowed,deny_upward,deny_tool_to_render,deny_tool_to_io,
                               deny_tool_to_analysis,allow_dna_to_analysis,deny_untagged,
                               deny_transitive_leak,allow_private_transitive}/CMakeLists.txt
                  expect_contract_violation.cmake   # cmake -P wrapper for abort tests
tests/{audio,golden,torture}/README.md             # labels reserved; first tests in Phases 1–3
scripts/check_includes.py                          # include-level layer check
.github/workflows/ci.yml
```

Only folders with code get targets; future targets are added by the phase that needs them, but the
**layer table lists all of them now**.

**Test folders** (ADR-0004): `CLAUDE.md`'s `tests/{unit,contract,audio,golden,torture}` wins over
spec §6 `tests/{golden,null,roundtrip,torture}`; null and round-trip checks are `audio`-label tests
under `tests/audio/` (as AUDIO_VERIFICATION §1 already places them).

## 2. CMake targets and layer enforcement

| Target | Folder | Layer | Links |
|---|---|---|---|
| `anno_core_types` | `src/core/types` | 0 | std only |
| `anno_core_io` | `src/core/io` | 0 + R4 (who may link it) | `core_types` |
| `anno_cli_commands` | `src/cli/commands` | 7 | `core_types`, `core_io` |
| `anno_cli` → binary `anno-cli` | `src/cli` | 7 | `cli_commands` |
| `anno_warnings` | INTERFACE | — | every anno target |

Naming: target = `anno_` + folder path with `/` → `_`; alias `anno::<rest>`.
`anno_add_library(<folder> …)` and `anno_add_executable(<folder> OUTPUT_NAME … …)` set custom
properties `ANNO_LAYER`, `ANNO_FOLDER`, link `anno_warnings`, set include root `src/`.

**`cmake/AnnoLayers.cmake`** is the single source of truth: the layer table for every folder in
architecture §6 plus `core/io` (layer 0, ADR-0001) and a `cli/*` prefix rule (layer 7, covers
`cli/commands`), a per-target **external allowlist** (`core/dsp`: the FFT library; `host/*`, `ui`:
`juce_*` modules and ARA SDK targets; everyone else: none — later phases extend it through their
design note, e.g. `Threads::Threads` for render in Phase 9, ONNX Runtime for `core/models`), and
`anno_check_layers()`, called at the end of the root CMakeLists. It:

1. Collects targets by walking `BUILDSYSTEM_TARGETS` recursively over every directory under `src/`.
   **Any library or executable target without `ANNO_LAYER` fails** (no plain `add_library` escape);
   `UTILITY` targets (custom/codegen) are exempt.
2. Builds two edge sets (config genexes `$<$<CONFIG:…>:x>` → `x`; `ALIASED_TARGET` resolved):
   - **direct edges** from `LINK_LIBRARIES` (everything the target links, any visibility) —
     checked by R1, R2 (for the layer-5 target itself), R3, R4, R5;
   - **usage edges** from `INTERFACE_LINK_LIBRARIES` entries **not** wrapped in
     `$<LINK_ONLY:…>` (i.e. PUBLIC/INTERFACE deps) — the only edges R2t walks.
   A dep that is not a anno target must be on that target's external allowlist (this also rejects
   raw paths and `-l` flags); otherwise fail.
3. Applies the rules (fail = `FATAL_ERROR` with the message prefix `ANNO LAYER VIOLATION:`):
   - **R1** dependency layer ≤ target layer (layers 0–7 from `architecture.md`).
   - **R2 layer-5 allowlist**: `tools/*`, `macros/*`, `features/*` may link only `core_types`,
     `core_edits`, `core_tuning`, and other layer-5 targets; `features/dna` and
     `features/sound_editor` may additionally link `core_analysis_*`. Everything else fails
     (so `core_dsp`, `core_synthesis_*`, `core_render`, `core_timemap`, `core_models`, `core_io`,
     externals).
   - **R2t transitive**: R2 is applied to the closure reached through **usage edges only**
     (`usage = true`) from each dependency of a layer-5 target. So `core_edits` linking `core_dsp`
     `PUBLIC` fails, while `PRIVATE` (`$<LINK_ONLY:…>`) passes. Consequence: public headers of
     `core_edits`, `core_tuning` and `core_analysis_*` may not expose dsp/synthesis/models types
     (also caught by the include checker's R1–R2 on headers).
   - R1, R3, R4 and R5 check every direct edge.
   - All violations are collected, then reported in one `FATAL_ERROR` (one line per violation), so
     a fixture's expected `R<n>` appears regardless of check order.
   - **R3** `core_types` links no anno target.
   - **R4 io restriction** (ADR-0001): only layer 6–7 targets may link `core_io` (tests are not
     layered targets and may link it).
   - **R5** no cycles among anno targets (DFS over the link graph).
4. Writes `build/<preset>/anno_layers.json` (folder → layer, allowlists, R2/R4 sets) via
   `file(GENERATE)` for the include checker.

**Include-level check**: `scripts/check_includes.py` (Python 3, `find_package(Python3 REQUIRED
COMPONENTS Interpreter)`) reads `anno_layers.json`, maps every `#include "…"` **and** `#include <…>`
whose path starts with a `src/` folder to its target and applies R1–R4; also rejects another target's
`detail/` headers, any use of `unchecked_from_origin`/`unchecked_since_origin` outside
`core/types` and `core/timemap`, and any use of `to_serialized_seconds`/`from_serialized_seconds`
outside `core/types`, `project` and `cli/*` (§4). CTest `layer_includes`, label `contract`.

**Fixture tests** (label `contract`): each folder in `tests/contract/layer_rules/` is a tiny project
that includes `AnnoLayers.cmake` and builds a dummy graph. CTest runs `cmake -S <fixture> -B <tmp>`:
- negative fixtures (`deny_*`) pass only if output matches `PASS_REGULAR_EXPRESSION
  "ANNO LAYER VIOLATION: R<n>"` for the expected rule (not merely any failure);
- positive controls (`allowed`, `allow_dna_to_analysis`, `allow_private_transitive` — tool →
  `core_edits` → PRIVATE `core_dsp`) must configure successfully;
- `deny_transitive_leak` is the same graph with a PUBLIC link and must fail with R2.

**Warnings** (`anno_warnings`): GCC/Clang `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
-Wshadow -Werror`; MSVC `/W4 /WX /permissive- /utf-8`. `CMAKE_CXX_EXTENSIONS OFF`, `cxx_std_20`.
Catch2 is added `SYSTEM` without `anno_warnings`.

**Dependencies**: Catch2 v3 via `FetchContent_Declare(… GIT_TAG v3.7.1 FIND_PACKAGE_ARGS 3)`;
Python 3 (build-time tool only). WAV is in-house (~250 lines).

## 3. Presets (`CMakePresets.json`, version 6, `cmakeMinimumRequired` 3.25)

| Configure preset | Inherits | Settings |
|---|---|---|
| `base` (hidden) | — | generator `Ninja`, `binaryDir` `${sourceDir}/build/${presetName}`, `CMAKE_EXPORT_COMPILE_COMMANDS ON` |
| `debug-asan` | base | `Debug`, `ANNO_SANITIZER=address_undefined` (MSVC: ASan only) |
| `debug-tsan` | base | `Debug`, `ANNO_SANITIZER=thread`; `condition`: host is not Windows |
| `release` | base | `Release`, `ANNO_SANITIZER=none` |

Matching build and test presets; test presets set `output.outputOnFailure`. No `noTestsAction:
error`: `ctest -L audio` legitimately finds nothing until Phase 1–3, and ctest exits 0 then.
`AnnoSanitizers.cmake`: Clang/GCC `-fsanitize=address,undefined -fno-omit-frame-pointer
-fno-sanitize-recover=undefined` (or `thread`) on compile and link; test env
`ASAN_OPTIONS=halt_on_error=1:detect_stack_use_after_return=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
MSVC: `/fsanitize=address /Zi`, and `/RTC1` is stripped from the Debug flags (incompatible).

## 4. `core/types/units.h`

Declared in this order. `Quantity` and `TimePoint` are `constexpr`, `noexcept`, trivially
copyable, `sizeof == sizeof(Rep)` (static_asserted); conversions and `TimeRange::make` are not
constexpr (see below). `TimeRange` lives in its own `core/types/time_range.h` so `units.h` doesn't
pull in `result.h`/`<string>`.

```cpp
namespace anno {

/// A value of one unit. Construction from Rep is explicit; no implicit conversion to or from Rep or
/// between Tags. Read the raw value with value().
template <class Tag, class Rep>
class Quantity {
public:
    using rep = Rep;
    constexpr Quantity() noexcept = default;                    // zero
    constexpr explicit Quantity(Rep v) noexcept : v_{v} {}
    [[nodiscard]] constexpr Rep value() const noexcept { return v_; }
    // Same-unit: +, -, unary -, +=, -=, <=>, ==. Scaling: Quantity * Rep, Rep * Quantity,
    // Quantity / Rep. Quantity / Quantity -> Ratio (always double; no integer truncation).
    // For double Rep and Tag != RatioTag only (constrained, so no overlap with the rules above):
    // Quantity * Ratio, Ratio * Quantity, Quantity / Ratio -> Quantity
    // (e.g. Cents * drift_scale, Seconds * length_scale). Ratio * Ratio and Ratio / Ratio -> Ratio
    // are single non-template overloads.
    // Samples * Ratio deliberately does not compile: go through to_seconds/to_samples with an
    // explicit Rounding.
private:
    Rep v_{};
};

using Ratio          = Quantity<struct RatioTag,          double>;       // dimensionless multiplier (scales, stretch slope, pitch ratio)
using Seconds        = Quantity<struct SecondsTag,        double>;       // duration, axis-free
using Samples        = Quantity<struct SamplesTag,        std::int64_t>; // frame count / offset, axis-free
using Cents          = Quantity<struct CentsTag,          double>;       // pitch (absolute re MIDI 0, A4 = 6900) or interval
using Decibels       = Quantity<struct DecibelsTag,       double>;       // level: relative gain or dBFS
using LoudnessUnits  = Quantity<struct LoudnessUnitsTag,  double>;       // BS.1770 loudness: LU, or LUFS when absolute
using Hz             = Quantity<struct HzTag,             double>;       // frequency and sample rate
using LinearGain     = Quantity<struct LinearGainTag,     double>;       // amplitude factor, 1 = unity

/// A point on one time axis. Affine: point - point = Seconds, point ± Seconds = point;
/// point + point and mixing axes do not compile.
template <class Axis>
class TimePoint {
public:
    constexpr TimePoint() noexcept = default;                   // axis origin
    // TimePoint - TimePoint -> Seconds; TimePoint ± Seconds -> TimePoint; <=>, ==
    /// Escape hatch to raw seconds. Allowed only in core/types and core/timemap (checked by
    /// scripts/check_includes.py).
    [[nodiscard]] static constexpr TimePoint unchecked_from_origin(Seconds offset) noexcept;
    [[nodiscard]] constexpr Seconds unchecked_since_origin() const noexcept;
private:
    Seconds offset_{};
};
using SourceTime = TimePoint<struct SourceAxis>;   // seconds in the original recording
using OutputTime = TimePoint<struct OutputAxis>;   // seconds after timing edits

/// (core/types/time_range.h) Half-open range [start, end) on one axis. Invariant: start <= end.
template <class Axis>
class TimeRange {
public:
    constexpr TimeRange() noexcept = default;                  // empty range at the origin
    /// For ranges from untrusted values. Errors: invalid_argument if end < start.
    [[nodiscard]] static Result<TimeRange> make(TimePoint<Axis> start, TimePoint<Axis> end);
    /// For ranges that can't be invalid (start + buffer length). ANNO_EXPECTS(duration >= 0). RT safe.
    [[nodiscard]] static TimeRange from_start(TimePoint<Axis> start, Seconds duration) noexcept;
    [[nodiscard]] constexpr TimePoint<Axis> start() const noexcept;
    [[nodiscard]] constexpr TimePoint<Axis> end() const noexcept;
    [[nodiscard]] constexpr Seconds duration() const noexcept;
private:
    TimePoint<Axis> start_, end_;
};
using SourceRange = TimeRange<SourceAxis>;
using OutputRange = TimeRange<OutputAxis>;

/// Persistence of time points (edit logs, scenario JSON). Allowed only in core/types, project and
/// cli (checked by scripts/check_includes.py). Axis-typed in both directions; no precision loss.
template <class Axis> [[nodiscard]] double to_serialized_seconds(TimePoint<Axis> t) noexcept;
/// Input comes from files: errors corrupt_data if s is not finite (never aborts on bad data).
template <class Axis> [[nodiscard]] Result<TimePoint<Axis>> from_serialized_seconds(double s);

inline constexpr Cents    k_a4_cents{6900.0};
inline constexpr Hz       k_a4_standard_hz{440.0};
inline constexpr Decibels k_silence_floor_db{-200.0};       // for zero amplitude, never -inf
enum class Rounding { nearest, down, up };                   // nearest = half away from zero

// Explicit conversions. Pure, thread-safe, real-time safe. Preconditions are ANNO_EXPECTS (always on).
// Not constexpr: C++20 <cmath> (floor/ceil/round/isfinite) is not constexpr.
[[nodiscard]] Samples to_samples(Seconds d, Hz sample_rate, Rounding r) noexcept;
    // pre: d finite, sample_rate finite > 0, |d * rate| < 2^62
[[nodiscard]] Seconds to_seconds(Samples n, Hz sample_rate) noexcept;   // pre: rate finite > 0
template <class Axis>       // sample index from the axis origin (e.g. for AudioSource::read); same pre as to_samples
[[nodiscard]] Samples to_sample_index(TimePoint<Axis> t, Hz sample_rate, Rounding r) noexcept;
template <class Axis>       // same pre as to_seconds
[[nodiscard]] TimePoint<Axis> to_time_point(Samples index, Hz sample_rate) noexcept;
[[nodiscard]] Hz         to_hz(Cents pitch, Hz a4_reference) noexcept;      // pre: pitch finite, a4 > 0
[[nodiscard]] Cents      to_cents(Hz f, Hz a4_reference) noexcept;         // pre: f finite > 0, a4 > 0
[[nodiscard]] Ratio      to_ratio(Cents interval) noexcept;                // 2^(c/1200); pre: finite
[[nodiscard]] LinearGain to_linear_gain(Decibels g) noexcept;              // 10^(g/20); pre: finite
[[nodiscard]] Decibels   to_decibels(LinearGain a) noexcept;
    // pre: a not NaN; |a| <= 10^(k_silence_floor_db/20) -> k_silence_floor_db; +inf violates pre

namespace unit_literals { /* 1.5_s, 200.0_cents, -6.0_db, 440.0_hz, 512_samples, 0.5_ratio */ }
}  // namespace anno
```

Decisions:
- **Durations vs points.** `SourceTime − OutputTime`, `SourceTime + SourceTime`, `OutputTime` where
  `SourceTime` is expected: all compile errors (contract test). Cross-axis only through `TimeMap`
  (Phase 1), which is the only non-types user of the `unchecked_*` pair, enforced by the checker.
  Persistence (`project`, `cli` scenario parsing) uses the separate `*_serialized_seconds` pair.
- **Sample positions** go through `to_sample_index(SourceTime, …)`, so a caller never unpacks raw
  seconds to index audio. `Samples` itself stays axis-free (counts/offsets within a buffer).
- **Single types with two meanings, on purpose** (ADR-0003): `Cents` (absolute pitch / interval),
  `Decibels` (dBFS / gain), `LoudnessUnits` (LUFS / LU), `Hz` (frequency / sample rate). Splitting
  each into point+delta types costs much more than it saves at this stage; fields name the meaning
  (`perceived_pitch_cents`, `peak_dbfs`, `loudness_lufs`, `sample_rate`).
- **Remaining raw doubles** use name suffixes (ADR-0003 adds them to coding-standards §2):
  `_prob` (0..1 probabilities: `confidence`, `voicing`), `_rad` (phase), `_ratio` inside kernels.
  `within_note_flatten` is a `Ratio` clamped to 0..1 by its producer.
- **NaN is allowed in `Cents` values** (spec: `f0_cents` NaN when unvoiced); conversions reject NaN
  by precondition.
- **No `Rounding` default** and **explicit `a4_reference`**: boundary rounding and tuning reference
  must be chosen by the caller.

## 5. `core/types/error.h`, `result.h`, `contract.h`

```cpp
enum class ErrorCode { invalid_argument, out_of_range, io_error, unsupported_format, corrupt_data,
                       not_found, cancelled, internal };
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;
struct Error { ErrorCode code; std::string message; };   // message allocates; never built on the audio thread

/// Wraps an Error for construction, like std::unexpected.
struct Unexpected { Error error; };
[[nodiscard]] Unexpected fail(ErrorCode code, std::string message);

/// Value-or-Error, std::expected-shaped (C++20 has no std::expected). Differences from
/// std::expected, by design: value()/error() on the wrong state call contract_violation (abort)
/// instead of throwing; the error type is fixed to Error. Class is [[nodiscard]].
template <class T>
class [[nodiscard]] Result {
public:
    template <class U = T>          // constrained: not Result/Unexpected, constructible, non-narrowing
    Result(U&& value);              // implicit, so `return value;` works
    Result(Unexpected u);           // implicit, so `return fail(…);` works
    [[nodiscard]] bool has_value() const noexcept;
    explicit operator bool() const noexcept;
    [[nodiscard]] T& value() &;  [[nodiscard]] const T& value() const&;  [[nodiscard]] T value() &&;
    [[nodiscard]] const Error& error() const&;  [[nodiscard]] Error error() &&;
    T& operator*() &; const T& operator*() const&; T operator*() &&;
    T* operator->(); const T* operator->() const;
    template <class U> [[nodiscard]] T value_or(U&& fallback) const&;
    template <class U> [[nodiscard]] T value_or(U&& fallback) &&;
    template <class F> [[nodiscard]] auto and_then(F&& f) &;   // also const&, &&: F(T) must return Result<U> (static_assert)
    template <class F> [[nodiscard]] auto transform(F&& f) &;  // also const&, &&: F(T) -> U gives Result<U>; void F gives Result<void>
private:
    std::variant<T, Error> state_;
};
template <> class [[nodiscard]] Result<void>;  // default = success; Result(Unexpected); has_value, error, and_then, transform
```

- `static_assert`s: `T` is not `Error`, `Unexpected`, a reference, or `void` (except the specialisation).
- Migration to `std::expected<T, Error>` later = replace `fail()`/`Unexpected` with `std::unexpected`
  and the abort with the library's behaviour; call sites using `return x;`, `return fail(…)`,
  `has_value`, `value`, `error`, `and_then`, `transform` keep compiling.
- `contract.h` (not `assert.h`, which would shadow the C header): `anno::detail::contract_violation(
  const char* expr, std::source_location)` prints and `std::abort()`s in all builds;
  `ANNO_EXPECTS(cond)` always on (cheap public preconditions), `ANNO_ASSERT(cond)` debug only. The
  only macros in the project.
- Exceptions: none thrown by anno code. `std::bad_alloc` from the standard library is the one
  remaining possibility and is not handled (documented).

## 6. `AudioBuffer`, `PositionedAudio`, `AudioSource` (`core/types`)

```cpp
/// Planar float32 audio. Invariant: channel_count() >= 1, sample_rate() finite > 0, every channel
/// has frame_count() samples. Value type (copy = deep copy). No time position (ADR-0002).
class AudioBuffer {
public:
    /// Zero-filled buffer. Errors: invalid_argument if channels < 1, frames < 0, rate not finite > 0;
    /// out_of_range if channels * frames overflows or exceeds vector::max_size(). Not RT safe.
    [[nodiscard]] static Result<AudioBuffer> create(int channels, Samples frames, Hz sample_rate);
    [[nodiscard]] int channel_count() const noexcept;
    [[nodiscard]] Samples frame_count() const noexcept;
    [[nodiscard]] Hz sample_rate() const noexcept;
    [[nodiscard]] Seconds duration() const noexcept;
    [[nodiscard]] std::span<const float> channel(int index) const;   // ANNO_EXPECTS(0 <= index < channel_count())
    [[nodiscard]] std::span<float> channel(int index);
private:
    std::vector<float> samples_;   // channel-major: ch * frames + i
    int channel_count_; Samples frame_count_; Hz sample_rate_;
};

/// Audio placed on one time axis (ADR-0002: replaces spec §3 AudioBuffer.start_time).
template <class Axis>
struct PositionedAudio { AudioBuffer audio; TimePoint<Axis> start; };
using SourceAudio = PositionedAudio<SourceAxis>;
using OutputAudio = PositionedAudio<OutputAxis>;

struct SourceId { std::string value; auto operator<=>(const SourceId&) const = default; };

/// Read-only handle to an original recording. Cheap to copy; all copies share one immutable
/// backend (shared_ptr<const Impl>), so the Phase 9 ARA backend can replace the in-memory one
/// without an API change. No default constructor. Invariant: the audio never changes, and impl_ is
/// never null — only copy operations are declared, so a "move" copies the shared_ptr and the
/// moved-from object stays valid.
class AudioSource {
public:
    [[nodiscard]] static AudioSource from_buffer(AudioBuffer audio);   // in-memory backend; id = content hash
    [[nodiscard]] const SourceId& id() const noexcept;
    [[nodiscard]] Hz sample_rate() const noexcept;
    [[nodiscard]] int channel_count() const noexcept;
    [[nodiscard]] Samples frame_count() const noexcept;
    /// Copy of frames [start, start + count), where start is a source-axis sample index
    /// (to_sample_index(SourceTime, …)). Errors: out_of_range if start < 0, count < 0 or
    /// start + count > frame_count(). Pure; thread-safe; not RT safe (allocates).
    [[nodiscard]] Result<AudioBuffer> read(Samples start, Samples count) const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};
```

- Spec §3 name mapping, stated once here and used everywhere: `length_samples` → `frame_count`,
  `channels` → `channel_count` (also in `WavInfo`).
- No zero-copy `channel()` on `AudioSource` (an ARA backend can't provide it); `read` only, as spec §3.
- `SourceId` = FNV-1a-64 over sample rate, channel count and little-endian float32 sample bytes,
  printed as hex. Tests compare ids only for equality/inequality, never the format, so the hash can
  be replaced later.

## 7. `core/io/wav.h` (ADR-0001, accepted)

```cpp
enum class WavSampleFormat { pcm16, pcm24, pcm32, float32 };
struct WavInfo { Hz sample_rate; int channel_count; Samples frame_count; WavSampleFormat format; };

[[nodiscard]] Result<WavInfo>     read_wav_info(const std::filesystem::path& path);
[[nodiscard]] Result<AudioBuffer> read_wav(const std::filesystem::path& path);  // any WavSampleFormat → float32
[[nodiscard]] Result<void>        write_wav(const std::filesystem::path& path, const AudioBuffer& audio); // float32 only
```

Errors: `not_found` (no file); `io_error` (open/read/write failure); `unsupported_format` (not
RIFF/WAVE; compressed formats; PCM8; float64; RF64); `corrupt_data` (truncated or inconsistent
chunks); `invalid_argument` from `write_wav` if the data would exceed the 4 GiB RIFF limit. Handles
`WAVE_FORMAT_EXTENSIBLE`, skips unknown chunks, honours odd-size padding. PCM → float divides by
2^(bits−1): exact for PCM16/24, **rounded** for PCM32 (24-bit mantissa). Float32 write → read is
bit-identical. Thread-safe for distinct paths; not RT safe.

## 8. `anno-cli info <file>`

`anno_cli_commands` (`src/cli/commands/info.h`):
`[[nodiscard]] Result<InfoReport> make_info_report(const std::filesystem::path&)` and
`[[nodiscard]] std::string format_info(const InfoReport&)` (split: compute vs print, split-function
#4). `InfoReport` = `WavInfo` + `Seconds duration` + `SourceId` + `std::vector<Decibels> peak_dbfs`
(per channel). `main.cpp` (executable `anno-cli`) only dispatches: exit 0 ok, 1 error (message to
stderr), 2 usage.

## 9. Tests and `tests/tolerances.h`

`anno_add_test(NAME … SOURCES … LABELS …)` → Catch2 executable + `catch_discover_tests` with labels
`unit`, `contract`, `audio`, `golden`, `torture`.

`tests/tolerances.h` (`namespace anno::tolerance`), two blocks:
- **Human-gated (AUDIO_VERIFICATION §1; change only via ADR)** — every row of the table: pitch
  round-trip `Cents{3}`, vibrato rate `Hz{0.2}` and extent `Ratio{0.10}`, mod/drift scaling
  `Ratio{0.10}`, timing `Seconds{0.002}`, length `Samples{1}`, level `Decibels{0.3}`, formant
  round-trip `Ratio{0.03}`, formant preservation `Ratio{0.05}`, noise centroid `Ratio{0.05}`, click
  detections `0`, NaN/Inf `0`, DC `Decibels{-60}` (dBFS).
- **Engineering** — `k_timemap_roundtrip = Seconds{1e-9}` (ROADMAP Phase 1); unit conversions:
  `k_cents_conversion_abs = Cents{1e-9}`, `k_seconds_conversion_abs = Seconds{1e-12}`,
  `k_hz_conversion_rel = Ratio{1e-12}`, `k_gain_conversion_rel = Ratio{1e-12}` (each with a
  justification comment).

Tests written first:
- **unit/units**: arithmetic; `Samples / Samples` returns fractional `Ratio`; `Cents * Ratio`,
  `Seconds * Ratio`, `Ratio * Ratio`; `to_samples` per `Rounding` incl. negatives and .5 cases
  (integers, exact); round trips; `to_hz(6900¢, 440)` ≈ 440 and `to_cents(880, 440)` ≈ 8100 within
  the `tolerances.h` conversion tolerances (no float `==`); `to_linear_gain(−6.0206 dB)` ≈ 0.5;
  `to_decibels(0) == k_silence_floor_db` (exact by definition); `to_sample_index`/`to_time_point`;
  `TimeRange::make` ok/error, `from_start`, default; `Ratio * Ratio`, `Ratio / Ratio`;
  serialized-seconds round trip bit-exact and `corrupt_data` on NaN/inf; literals.
- **contract/units**: `requires`-based static_asserts that `double → Seconds`, `Seconds →
  SourceTime`, `SourceTime − OutputTime`, `SourceTime + SourceTime`, `Cents + Hz`, `Ratio + Seconds`
  don't compile; `Samples * Ratio` doesn't compile; trivially copyable; sizes. Precondition aborts:
  a helper binary takes a case name (`to_samples_nan`, `to_cents_zero_hz`, `result_value_on_error`,
  …); each CTest runs `cmake -P tests/contract/expect_contract_violation.cmake`, which calls the
  helper with `execute_process`, requires a non-zero result (SIGABRT on POSIX, 3 on Windows) and
  output matching `contract violation`, and exits 0 — so a signal death doesn't fail the test.
  On `_WIN32`, `contract_violation` first calls `_set_abort_behavior(0, _WRITE_ABORT_MSG |
  _CALL_REPORTFAULT)` and `_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG)` so the Debug CRT
  shows no modal dialog (CI would hang); exit code stays 3.
- **unit/result + contract/result**: value/error paths, `fail()`, `and_then`/`transform` (incl. void
  F), `Result<void>`, a move-only `T` through every member, no narrowing constructor
  (static_assert), `error() &&` move.
- **unit/audio_buffer, audio_source**: create errors incl. overflow; zero fill; channel spans;
  `read` bounds/errors; copies share id; moved-from `AudioSource` still readable; equal content →
  equal id, different → different;
  `static_assert` that `AudioSource` exposes no mutable access and no default constructor.
- **unit/wav**: float32 write→read bit-exact; hand-built PCM16/24/32 fixtures decode to known
  floats; extensible header; unknown chunk skipped; truncated / non-RIFF / ADPCM / PCM8 → expected
  codes; missing file → `not_found`.
- **unit/cli_info**: `make_info_report` fields on a generated WAV; `anno-cli` run via CTest: exit 0,
  exit 1 on missing file, exit 2 with no args.
- **contract/layer rules**: the fixtures in §2 + `layer_includes`.

## 10. Tooling and CI

- `.clang-format`: LLVM base, `IndentWidth 4`, `ColumnLimit 100`, `PointerAlignment Left`,
  `IncludeBlocks Regroup`.
- `.clang-tidy`: `bugprone-*`, `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `readability-*`
  minus a short commented list (e.g. `modernize-use-trailing-return-type`,
  `readability-magic-numbers` in tests); `readability-identifier-naming` encodes the naming table
  (types `CamelCase`, functions/variables `lower_case`, private members suffix `_`, constants prefix
  `k_`); `cppcoreguidelines-special-member-functions.AllowMissingMoveFunctions: true` (for the
  deliberate copy-only `AudioSource`); `WarningsAsErrors: '*'`.
- `.github/workflows/ci.yml`: matrix ubuntu-24.04 (clang), macos-14 (Apple clang), windows-2022
  (MSVC, `ilammy/msvc-dev-cmd` for Ninja); each: install Ninja → `debug-asan` configure/build/test →
  `release` configure/build/test. Extra jobs: `debug-tsan` on Linux and macOS; `clang-format
  --dry-run -Werror`; `run-clang-tidy` on Linux. Layer fixtures run inside ctest on every OS.

## Effect on audio

No rendering yet. Float32 WAV write → read is bit-exact (tested). PCM16/24 → float is exact; PCM32 →
float rounds to float32 precision. The null-test infrastructure starts in Phase 3.

## Split / join decisions

- `Quantity` + `TimePoint` (two templates): durations and axis points have different algebra
  (split-class #3).
- `AudioBuffer` vs `AudioSource`: mutable value used everywhere vs immutable original with identity
  and a swappable backend (split-class #3, #4).
- `PositionedAudio<Axis>` as a separate struct instead of a field on `AudioBuffer`: most buffers
  (scratch, engine I/O) have no meaningful position (split-class #3).
- `make_info_report` / `format_info`; `read_wav_info` / `read_wav` (split-function #4).
- `cli_commands` library vs `anno-cli` executable: commands are testable and layer-checked (split #4).
- No `AudioSource` interface yet: one implementation until Phase 9 (join #5); the pImpl handle keeps
  that change local.
- `Error` is one struct, not a hierarchy (join #5).

## Dependencies

`core_types`: std only. `core_io`: `core_types`, `<filesystem>`. `cli_commands`: `core_types`,
`core_io`. `anno-cli`: `cli_commands`. Tests: Catch2 v3.7.1. Build tools: CMake ≥ 3.25, Ninja,
Python 3.

## ADRs written in Phase 0

- **ADR-0001: `core/io` target for audio file I/O.** Adds `src/core/io` (WAV I/O) under `core/`
  (CLAUDE rule 2: only core touches samples) at layer 0 for its own links, but linkable **only by
  layers 6–7 and tests** (rule R4), so analysis/synthesis/render can't do file I/O (spec §4 purity,
  realtime-audio rules). Also records `cli/*` at layer 7. Amends `architecture.md` layer table (both
  rows), `CLAUDE.md` layout summary, spec §6.
- **ADR-0002: `AudioBuffer` has no `start_time`; positioned audio is `PositionedAudio<Axis>`.**
  Spec §3 leaves the axis to "the owner", which is an untyped time crossing boundaries (CLAUDE rule 3).
  Replacements: `SynthesisEngine::render_note` → `OutputAudio`; `separate_notes` → per-note
  `SourceAudio` map + residual `SourceAudio`; `split_harmonic_noise` residual → `SourceAudio`;
  `istft` → `AudioBuffer` (position is the caller's, frames are relative); `crossfade(a, b, …)` →
  operates on `AudioBuffer`s aligned by the caller; `RenderResult.buffer` → `OutputAudio` and the
  spec's `out_range` field is **dropped** (derived by `output_range(const OutputAudio&)`, so the two
  can't disagree); `source_range: SourceRange` stays; `AudioSource::read` → `AudioBuffer` (position
  is the request). Adds a pointer to this ADR in spec §3 and §4.
- **ADR-0003: units vocabulary.** Adds `Ratio`, `LinearGain`, `LoudnessUnits`, `TimeRange<Axis>`,
  `to_sample_index`, the `*_serialized_seconds` pair and where `unchecked_*` / serialized may be
  used; documents the single-type-two-meanings choice (§4). Edits `coding-standards.md` §2 in the
  same commit: strong-type list gains `Ratio`, `LinearGain`, `LoudnessUnits`, `TimeRange`; suffixes
  gain `_prob`, `_rad`.
- **ADR-0004: test folders.** `CLAUDE.md`'s `tests/{unit,contract,audio,golden,torture}` wins over
  spec §6 `tests/{golden,null,roundtrip,torture}`; null and round-trip checks are `audio`-label
  tests under `tests/audio/`. Amends spec §6.

## Decisions from you (2026-10-19)

1. Toolchain: `brew install ninja llvm` (clang-format/clang-tidy from LLVM; build stays on Apple clang).
2. CI: create a **private** GitHub repo with `gh repo create --private`, push, and get CI green in Phase 0.
3. ADR-0001…0004: **accepted**; written as accepted with their doc amendments; `core/io` built in Phase 0.

## Verification (Phase 0 gate)

1. `cmake --preset debug-asan && cmake --build --preset debug-asan && ctest --preset debug-asan` —
   all green, no sanitizer reports. Same for `debug-tsan` and `release`.
2. `ctest --preset debug-asan -L contract` runs every layer fixture: negatives pass only on their
   exact `ANNO LAYER VIOLATION: R<n>` message; positive controls configure.
3. Manual proof in the real tree: add `target_link_libraries(anno_core_types PRIVATE anno_core_io)`
   → configure fails with `R3`; revert. Add a plain `add_library(x …)` under `src/` → fails as untagged.
4. `./build/release/bin/anno-cli info <generated.wav>` prints rate, channels, frames, duration, id,
   peak dBFS.
5. `clang-format --dry-run -Werror` and `run-clang-tidy -p build/debug-asan` clean.
6. CI green on Linux, macOS, Windows on the private GitHub repo (`gh run watch`).
7. Phase review written; ROADMAP "Current phase" → Phase 1; tag `phase-0`.

## Review log

**Round 1 — structure-reviewer: CHANGES REQUIRED** (1 blocking, 16 should-fix, 6 notes). Resolutions:

| # | Sev | Finding | Resolution |
|---|---|---|---|
| 1 | blocking | Layer 5 was a denylist; `core_io` not denied | R2 is now an allowlist (§2); fixtures per branch |
| 2 | should | Transitive leaks, untagged targets, genex/alias/raw entries, cycles | R2t, untagged = fail, normalisation + external allowlist, R5 (§2) |
| 3 | should | Angle includes unchecked; Python undeclared | Both include forms scanned; `find_package(Python3)` (§2, Dependencies) |
| 4 | should | `WILL_FAIL` passes on any failure | `PASS_REGULAR_EXPRESSION` per rule + positive controls (§2) |
| 5 | should | CLI executable unlayered/untestable; bad manual proof | `anno_cli_commands` lib + `anno_add_executable`; proof uses `core_types → core_io` |
| 6 | should | `core_io` at layer 0 lets analysis/render do I/O | R4: layers 6–7 and tests only; ADR-0001 amends docs; gated on acceptance |
| 7 | should | ADR-0002 lacked replacement for positioned buffers | `PositionedAudio<Axis>` + replacement list in ADR-0002 |
| 8 | should | `from_origin` escape hatch makes axis mixing easy | Renamed `unchecked_*`, checker-restricted; `to_sample_index`, `TimeRange` added |
| 9 | should | LU wrongly deferred; dimensionless values untyped | `LoudnessUnits`, `Ratio`, `LinearGain` added; `_prob`/`_rad` suffixes (ADR-0003) |
| 10 | should | Conversion preconditions / NaN / UB unspecified | Preconditions stated, `ANNO_EXPECTS`, tested via abort helper |
| 11 | should | Result not actually std::expected-compatible; API gaps | `Unexpected`/`fail()`, constrained ctor, `value() &&` by value, `value_or &&`, `error() &&`, rules for F |
| 12 | should | `assert.h` shadows C header | Renamed `contract.h` |
| 13 | should | `create` overflow / throwing vector | Overflow + `max_size` check → `out_of_range`; `bad_alloc` documented |
| 14 | should | Zero-copy `channel()` impossible for ARA | Removed; pImpl handle; no default ctor |
| 15 | should | frame/channel count named three ways | `frame_count`/`channel_count` everywhere |
| 16 | should | `noTestsAction: error` breaks `-L audio` | Removed, reason documented (§3) |
| 17 | should | MSVC `/RTC1` vs ASan; Ninja env | `/RTC1` stripped, `/Zi`; msvc-dev-cmd in CI |
| 18 | note | `Samples / Samples` truncates | Quantity / Quantity → `Ratio` (double) |
| 19 | note | Dual-meaning Cents/dB/Hz | Kept deliberately, documented in ADR-0003, meaning in field names |
| 20 | note | PCM32 exactness claim; 4 GiB; unsupported list; stray text | Fixed (§7, Effect on audio) |
| 21 | note | Missing tolerances; relative tol for cents | All §1 rows + engineering block; absolute cents tolerance |
| 22 | note | Declaration order; "opaque" `SourceId` | Reordered; tests compare ids by equality only |
| 23 | note | Missing close steps; test-folder conflict | Execution step 6; ADR-0004 settles test folders |

**Round 2 — structure-reviewer: CHANGES REQUIRED** (1 blocking, 7 should-fix, 4 notes; all 23
round-1 findings confirmed resolved in the body). Resolutions:

| # | Sev | Finding | Resolution |
|---|---|---|---|
| 1 | blocking | R2t followed `$<LINK_ONLY>` edges, rejecting the PRIVATE links it prescribes | Edges keep a `usage` flag; R2t follows usage edges only; `allow_private_transitive` positive fixture; header implication stated (§2) |
| 2 | should | `core_io` had no numeric layer; `cli/commands` not in table | `core_io` layer 0 + R4; `cli/*` layer 7 (§2, ADR-0001) |
| 3 | should | `abort()` + pass regex fails on POSIX | `cmake -P` wrapper with `execute_process` (§9) |
| 4 | should | `TimeRange` struct claimed an invariant | Class with `make() -> Result`, accessors (§4) |
| 5 | should | `Ratio` couldn't scale quantities | `Quantity × Ratio`, `/ Ratio`, `Ratio × Ratio`; `Samples × Ratio` rejected (§4) |
| 6 | should | `constexpr` conversions use non-constexpr `<cmath>` | Conversions not constexpr; pre for `to_sample_index`/`to_time_point` stated |
| 7 | should | `unchecked_*` restriction blocks persistence | `*_serialized_seconds` pair allowed in types/project/cli (§4, ADR-0003) |
| 8 | should | `RenderResult.out_range` duplicates `OutputAudio.start` | `out_range` dropped, `output_range()` derives it (ADR-0002) |
| 9 | note | Moved-from pImpl `AudioSource` null | Copy-only ops; moved-from stays valid; tested |
| 10 | note | ADR-0002/0003 doc edits unscheduled; ADR-0001 bundled two decisions | All amendments in step 2; test folders split to ADR-0004 |
| 11 | note | Utility targets fail untagged rule; allowlist growth | `UTILITY` exempt; phases extend allowlist via design notes |
| 12 | note | Float `==` in conversion tests | Tolerance comparisons; `Ratio` relative tolerances |

**Round 3 — structure-reviewer: APPROVED WITH FIXES** (0 blocking, 5 should-fix, 4 notes; all 12
round-2 fixes confirmed in the body). Resolutions:

| # | Sev | Finding | Resolution |
|---|---|---|---|
| 1 | should | `LINK_LIBRARIES` lists PRIVATE deps unwrapped, so R2t would still see them | Two edge sets: direct (`LINK_LIBRARIES`) for R1–R5; usage (non-`LINK_ONLY` `INTERFACE_LINK_LIBRARIES`) for R2t (§2) |
| 2 | should | `Ratio` operator overlap → ambiguity/redefinition | `× Ratio`/`/ Ratio` constrained to `Tag != RatioTag`; single `Ratio×Ratio`, `Ratio/Ratio` overloads; tested |
| 3 | should | `from_serialized_seconds` aborts on bad file data | Returns `Result`, `corrupt_data` on non-finite |
| 4 | should | MSVC Debug `abort()` dialog hangs CI | `_set_abort_behavior` + `_CrtSetReportMode` on `_WIN32` |
| 5 | should | `TimeRange` only via `Result`; no default; stale constexpr claim | `from_start` (RT safe), default = empty at origin, claim narrowed, own header |
| 6 | note | clang-tidy special-member check vs copy-only `AudioSource` | `AllowMissingMoveFunctions: true` |
| 7 | note | Multi-rule fixtures depend on check order | Collect all violations, single `FATAL_ERROR` |
| 8 | note | Stale status line and row 23 | Updated |
| 9 | note | ADR list out of order | Reordered |

No blocking findings remain; per ROADMAP step 2 the gate is passed.
