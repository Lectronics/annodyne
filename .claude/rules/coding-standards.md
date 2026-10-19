---
paths:
  - "src/**"
  - "tests/**"
---

# C++ coding standards

Goal: a codebase a new developer (or a fresh Claude session) can understand one module at a time, where audio behaviour is predictable and every change is testable. When a rule here conflicts with getting the sound right, raise it; don't silently break the rule.

## 1. Language and tooling

- C++20. No compiler extensions. Warnings as errors (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow` or MSVC `/W4 /WX`).
- `clang-format` (repo `.clang-format`) on every changed file. `clang-tidy` (repo `.clang-tidy`) clean on every changed file.
- Debug builds run with ASan + UBSan; the threading tests run under TSan.
- No `new`/`delete`, no owning raw pointers, no C arrays, no `using namespace` in headers, no macros except include guards / `#pragma once` and assertion wrappers.

## 2. Naming

| Thing | Style | Example |
|---|---|---|
| Types, enums, concepts | `PascalCase` | `PitchTrack`, `EditCategory` |
| Functions, variables | `snake_case` | `detect_pitch`, `hop_s` |
| Members | `snake_case_` (trailing underscore) for private; plain for public struct fields | `cursor_`, `notes` |
| Constants | `k_snake_case` | `k_max_harmonics` |
| Namespaces | `melo::<module>` | `melo::analysis::pitch` |
| Files | `snake_case.h/.cpp`, named after the main type or function | `pitch_track.h` |

**Units go in the type or the name, always.** Prefer strong types from `core/types/units.h`: `SourceTime`, `OutputTime`, `Seconds`, `Samples`, `Cents`, `Decibels`, `Hz`, `Ratio`, `LinearGain`, `LoudnessUnits`, and `TimeRange<Axis>` (`SourceRange`, `OutputRange`) from `core/types/time_range.h` (ADR-0003). Where a raw number is unavoidable (inside a kernel), suffix the name: `_s`, `_samples`, `_cents`, `_db`, `_lu`, `_hz`, `_ratio`, `_prob` (0..1 probability), `_rad` (phase). A function that takes or returns time must say which axis.

## 3. Types and ownership

- **Plain `struct`** for data with no invariants (track arrays, parameter sets, results). Public fields, no getters/setters.
- **`class`** only when there is an invariant to protect (e.g. `EditList` cursor ≤ size, `TimeMap` monotonic). Make the invariant explicit in a comment and check it in debug.
- Value semantics by default. Large immutable shared data (analysis snapshots, source audio) as `std::shared_ptr<const T>`. Exclusive ownership as `std::unique_ptr<T>`. Non-owning access as references, `std::span`, or `std::string_view`.
- `const` everything that can be. Analysis and edits are immutable once created; "modifying" returns a new object or a new version.
- Use `enum class` for every closed set of options. Never pass `bool` flags that change behaviour; use an enum (`Fade::Shape::equal_power`, not `bool equal_power`).
- Audio buffers are `float`. Time and phase accumulation use `double`. Pitch is computed in cents (`double`), converted to Hz only at the synthesis boundary.

## 4. Functions

- **Inputs by `const&` or `std::span<const T>`, outputs by return value.** No output parameters, except `std::span<float>` destination buffers on the real-time path.
- More than 3 parameters, or any parameter with a sensible default → bundle into a `…Params` struct with default member initialisers. Example: `detect_pitch(const AudioSource&, const PitchParams&)`.
- Fallible functions return `melo::Result<T>`. Errors are an `enum class` code plus a message. Exceptions are not used for control flow and never cross the audio thread.
- Prefer pure free functions for algorithms (analysis, DSP, tuning maths). They are trivially testable and need no mocks.
- Mark functions `[[nodiscard]]` when ignoring the result is a bug (all `Result<T>` returns, all analysis and render functions).
- Every public function has a doc comment with: what it does, units and time axis of inputs/outputs, preconditions, what it returns (including error cases), thread safety and whether it is real-time safe.

```cpp
/// Estimate the perceived ("musically weighted") pitch of a note.
/// Input pitch curve is in cents on the note's FrameGrid (SourceTime).
/// Returns cents relative to A4 = 6900. Error if the note has < 3 voiced frames.
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<Cents> perceived_pitch(const Note& note, const PerceivedPitchParams& params);
```

## 5. When to split and when to join

Default to **cohesion**: things that change together live together; things that change for different reasons live apart. Use these tests in order. If a decision changes a public API, note the reason in the design note or an ADR.

### Split a function when any of these is true
1. **Its name needs "and"** (`analyse_and_render`) or its doc comment describes two jobs.
2. **It mixes levels**: orchestration (calling steps, handling errors) in the same body as low-level maths. Keep the orchestrator readable as a list of steps; move the maths into named kernels.
3. **A block has its own explanatory comment heading** ("// estimate vibrato rate"). That block is a function waiting for a name.
4. **A part is independently testable or reusable**, or you want to test it with a synthetic signal on its own.
5. **Real-time and non-real-time work are mixed.** Precomputation (allocating, analysing) must be separate from the per-block process function.
6. **A parameter only matters to one branch**, or a flag selects between two behaviours → two functions (or a strategy).
7. **Size smell**: roughly > 40 lines of logic, > 3 levels of nesting, or > 5 local state variables carried through the whole body. These are prompts to look, not automatic splits.

### Keep together (or join back) when any of these is true
1. **The pieces only make sense together** and splitting would force passing 5+ arguments or a bag of shared state between them. Prefer one clear function over three that must be read side by side.
2. **The helper is a one-line wrapper** that adds no meaning beyond its body.
3. **The pieces always change together** in every commit that touches them.
4. **It is a hot inner loop.** Keep the loop body in one place so the compiler can vectorise it; extract the loop as a whole kernel, not fragments of it.
5. **Splitting would hide the algorithm**: a 60-line published algorithm (e.g. a Viterbi pass) is clearer as one function that mirrors the paper, with comments citing the paper's steps.

### Split a class when any of these is true
1. **Two groups of members are used by disjoint sets of methods** (low cohesion) → two classes.
2. **Different lifetimes or threads**: anything touched by the audio thread is its own class with its own preallocated state, separate from UI/analysis-thread state.
3. **Different reasons to change**: e.g. caching policy vs rendering maths, file format vs in-memory model.
4. **You need to fake one part in tests** but not the other.
5. **Size smell**: > ~7 data members or > ~15 public methods. Look for the hidden second responsibility.

### Join classes (or remove one) when any of these is true
1. **Data clump**: two or three types are always created together and passed together → one struct.
2. **Middle man**: a class mostly forwards calls to another → remove it and call the target directly.
3. **Mutual intimacy**: two classes keep reaching into each other's internals → merge them, or move the shared data into one of them.
4. **Static-only class** → free functions in a namespace.
5. **Speculative interface**: an abstract base with one implementation and no second one planned in the current or next phase → delete the interface, keep the concrete class. Interfaces are allowed now for `SynthesisEngine`, `Tool`, `Macro`, and pitch/onset/transcription estimators (multiple implementations are planned in the specs).

### Files and modules
- One main type or one closely related function family per header. A header that grows past ~400 lines is a split prompt.
- Create a new subfolder/target when 3+ files share a concept and depend on each other more than on the rest of their module.
- **Rule of three for abstraction**: duplicating code twice is acceptable; the third copy gets extracted. Do not generalise for imagined future uses.

## 6. Numerics and determinism

- Compare floats with explicit tolerances from `tests/tolerances.h`, never `==` (except bit-exact passthrough tests, which compare exactly on purpose).
- Flush denormals on audio threads. Guard divisions and logs (`log(0)`, `0/0`); never emit NaN or Inf — assert in debug, clamp and report in release.
- Same inputs and same parameters → bit-identical output on the same build. No unseeded randomness; noise generators take an explicit seed.
- Phase accumulators in `double`, wrapped every block.

## 7. Comments and documentation

- Comments explain **why** and cite sources for algorithms (paper + section, e.g. "Röbel & Rodet 2005, True Envelope §3"). Do not narrate what the code obviously does.
- Every algorithm module has a short `README.md` in its folder: purpose, method, references, known limitations, tuning parameters.
- Magic numbers become named constants with units and a source or a reason.

## 8. Tests

- Every public function has unit tests. Every core contract (section "core contracts" in `architecture.md`) has a contract test.
- Use **synthetic signals with known answers** first (sine with known f0 and vibrato, chirps, harmonic tones with a known envelope, noise bursts), then golden real recordings.
- Test names describe behaviour: `perceived_pitch_ignores_attack_scoop`.
- Tolerances live in one file, `tests/tolerances.h`, with comments justifying each value.
- A bug fix comes with a regression test that fails before the fix.

## 9. Self-review checklist (run before saying a change is done)

- [ ] Layer rules respected; no new link from a lower to a higher layer.
- [ ] Every new public function: params struct if > 3 args, `Result<T>` if fallible, `[[nodiscard]]`, doc comment with units, time axis, thread/RT safety.
- [ ] No unit-less pitch, gain or time values crossing a function boundary.
- [ ] Source/Output time never mixed without `TimeMap`.
- [ ] Split/join rules applied; any function > 40 lines or class > 7 members has a reason.
- [ ] Tests written first, all passing, sanitizers clean, clang-tidy clean.
- [ ] If rendered audio changed: `audio-verifier` run and audition package produced.
- [ ] Design note / ADR updated if a public type, return value or folder changed.
