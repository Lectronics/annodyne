# Phase <N> review: <name>

- **Date closed:** YYYY-MM-DD
- **Git tag:** `phase-<N>`

## Built
Modules, public types and functions added or changed (link design notes).

## Gates
| Gate | Result | Evidence |
|---|---|---|
| Structure review of design notes | | link |
| Build + all tests + sanitizers + clang-tidy | | CI run |
| Objective audio checks (if audio changed) | | audio-verifier report |
| Human listening (if audio changed) | | `docs/reviews/<phase>-listening.md` |
| Structure audit (Phases 1, 5, 9, 12) | | structure-reviewer report |

## Deviations from the specs
Each with its ADR number.

## Objective numbers
Key measurements (pitch round-trip error, timing error, render speed, etc.) compared with the previous phase.

## Open issues carried forward
Each with an owner phase.

## Split/join and cleanup done this phase
Refactors made after the structure audit, and why.
