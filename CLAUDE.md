# Melo — note-based audio editor (C++)

A Melodyne-class editor: analyse audio into notes, let users edit pitch, timing, level and timbre per note, and render the result **non-destructively**. People will listen closely to every correction, so audible quality is a hard requirement, not a polish step.

## Source of truth

Read the relevant spec before designing or changing anything. Do not import them here; open them when needed.

- `docs/specs/melodyne-features.md` — what each tool does
- `docs/specs/melodyne-implementation-guide.md` — which algorithms to use and how to make them good
- `docs/specs/melodyne-core-architecture.md` — data types, core functions, edit model, render pipeline, folder layout
- `docs/ROADMAP.md` — phases, gates and the current phase
- `docs/AUDIO_VERIFICATION.md` — objective audio checks and the human listening protocol
- `docs/decisions/` — accepted architecture decisions (ADRs). These override the specs where they differ.

Coding standards live in `.claude/rules/` and load automatically when you work on matching files.

## Non-negotiable rules

1. **The source audio is never modified.** Edits are records in an `EditList`; audio out = `render(source, analysis, edits)`.
2. **Only `src/core/` touches audio samples.** Tools, macros and features read analysis and return `Edit` objects. They never link against `synthesis`, `render` or `dsp`.
3. **Two time axes, never mixed.** `SourceTime` (original recording) and `OutputTime` (after timing edits), linked only through `TimeMap`. Use the strong types; never pass raw `double` seconds across module boundaries.
4. **Units are in the type or the name.** Cents, dB/LU, Hz, seconds, samples. No unitless pitch or gain values.
5. **No edits, same audio.** A render with an empty edit list must be bit-identical to the source. Samples outside edited regions must be bit-identical too.
6. **The audio thread never allocates, locks, logs, throws or does I/O.**
7. **You cannot hear.** Never state that something "sounds good". Report objective check results and produce an audition package for a human (see `docs/AUDIO_VERIFICATION.md`).

## How to work

Every non-trivial change follows this loop. Do not skip steps to save time.

1. **Plan first.** For any new module, public type or public function: write or update a design note in `docs/design/` from `docs/templates/DESIGN_NOTE.md` before writing code. It must state inputs, outputs, return types, ownership, time axis, units, thread/real-time safety, and error cases.
2. **Get the structure reviewed.** Ask the `structure-reviewer` subagent to review the design note and headers against the specs and coding standards. Fix its blocking findings before implementing.
3. **Contracts before bodies.** Write the header and the tests first (they should compile and fail), then implement.
4. **Small slices.** One function or one class per step; build and run its tests before moving on.
5. **Verify.** Build in Debug with sanitizers, run all tests, run clang-tidy on changed files. For anything that changes rendered audio, ask the `audio-verifier` subagent to run the audio checks and build the audition package.
6. **Record decisions.** Any change to a public type, return value, edit category, folder or dependency rule gets an ADR in `docs/decisions/` from `docs/templates/ADR.md`.
7. **Close the phase** with `docs/templates/PHASE_REVIEW.md`. A phase that changes audio is not done until a human has filled in the listening sheet.

If a spec looks wrong or two specs conflict, stop and ask rather than guessing. Record the answer in an ADR.

## Commands

```bash
cmake --preset debug-asan          # configure (Debug + ASan/UBSan)
cmake --build --preset debug-asan  # build
ctest --preset debug-asan          # all tests
ctest --preset debug-asan -L audio # audio checks only
cmake --preset release && cmake --build --preset release
./build/release/bin/melo-cli audition <scenario.json> --out audition/<name>   # listening package
run-clang-tidy -p build/debug-asan <changed files>
clang-format -i <changed files>
```

These presets and the CLI are created in Phase 0. If a command fails because something does not exist yet, check the current phase in `docs/ROADMAP.md`.

## Layout (summary — full version in the architecture spec)

```
src/core/{types,dsp,analysis,tuning,edits,timemap,synthesis,render,models}
src/tools/  src/macros/  src/features/  src/project/
src/host/{ara,plugin,standalone}  src/ui/  src/cli/
tests/{unit,contract,audio,golden,torture}   audition/   docs/
```

Dependency direction is enforced by CMake targets: lower layers never link higher ones. See `.claude/rules/architecture.md`.

## Tech stack

C++20, CMake ≥ 3.25 with presets, Catch2 v3, clang-format, clang-tidy. JUCE and the ARA SDK only inside `src/host/` and `src/ui/`; `src/core/` has no JUCE dependency. Errors via `melo::Result<T>` (an `expected`-style type). Change any of this only through an ADR.
