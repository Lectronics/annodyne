# Design note: <module or feature>

- **Phase:** <N>
- **Status:** draft | reviewed | implemented
- **Spec sections:** <links to docs/specs sections this implements>
- **Folder / CMake target:** `src/...`  (layer <N>)

## Purpose
One paragraph: what this does and why it exists.

## Public types

For each type:

| Type | struct/class | Invariant (if class) | Owner | Shared how |
|---|---|---|---|---|
| `Example` | struct | — | caller | value |

Fields with units:

```cpp
struct Example {
    SourceTime start;      // source time, seconds
    Cents pitch;           // relative to A4 = 6900
};
```

## Public functions

For each function:

```cpp
/// <doc comment: what, units, time axis, preconditions, returns, thread/RT safety>
[[nodiscard]] Result<ReturnType> name(const Input& in, const NameParams& params);
```

| | |
|---|---|
| Inputs | types, units, time axis |
| Parameters | `NameParams` fields with defaults and ranges |
| Returns | exactly what comes back; new object or new version |
| Errors | each `Error` code and when it occurs |
| Pure? | yes/no; what it mutates if not |
| Thread safety | |
| Real-time safe | yes/no |
| Complexity / cost | rough per-second-of-audio cost |

## Effect on audio
- Does this change rendered output? If yes: which edits/categories, which regions, which engines.
- How the waveform changes (formula or step list), and what stays bit-identical.
- Objective checks that cover it, and new scenarios needed.

## Split / join decisions
For each non-obvious choice (one class vs two, one function vs several), name the rule from `coding-standards.md` §5 and the reason.

## Dependencies
Targets this links to (must respect layer rules). External libraries.

## Tests
- Unit: …
- Contract: …
- Audio scenarios: …

## Open questions
Anything the specs don't settle. Ask before implementing.

## Review log
<structure-reviewer findings and how each was resolved>
