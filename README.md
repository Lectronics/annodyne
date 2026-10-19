# Annodyne — Claude Code starter kit

Drop these files into the root of an empty git repository, then start Claude Code there.

## What's here

| Path | Purpose | Loaded by Claude Code |
|---|---|---|
| `CLAUDE.md` | Project rules, workflow loop, commands | Every session |
| `.claude/rules/architecture.md` | Layers, dependency rules, core contracts | When working in `src/`, `tests/`, CMake files |
| `.claude/rules/coding-standards.md` | C++ standards incl. when to split and join functions/classes | When working in `src/`, `tests/` |
| `.claude/rules/realtime-audio.md` | Audio-thread rules | When working in render, synthesis, dsp, host |
| `.claude/rules/audio-changes.md` | What to do when a change affects what users hear | When working in analysis, edits, timemap, synthesis, render, audio tests |
| `.claude/agents/structure-reviewer.md` | Independent design/structure reviewer (read-only) | On request or when Claude delegates |
| `.claude/agents/audio-verifier.md` | Runs objective audio checks, builds listening packages | On request or when Claude delegates |
| `docs/ROADMAP.md` | Phases 0–13, the 7-step cycle, gates, structure audits | Read when needed |
| `docs/AUDIO_VERIFICATION.md` | Objective checks, scenarios, audition packages, human listening protocol | Read when needed |
| `docs/specs/` | The three specification documents | Read when needed |
| `docs/templates/` | Design note, ADR, phase review, listening sheet | Used by the workflow |
| `docs/design/`, `docs/decisions/`, `docs/reviews/` | Where those filled-in documents go | — |

## Getting started

1. `git init`, copy the kit in, commit.
2. Start Claude Code in plan mode: `claude --permission-mode plan` (or press Shift+Tab until plan mode is on).
3. Use the Phase 0 starter prompt from `docs/ROADMAP.md`.
4. After each phase, read the phase review, listen to the audition package if there is one, and fill in the listening sheet before letting Claude move on.

## Your part in the loop

Claude Code plans, reviews (through the subagents), implements and measures. You:
- approve plans and answer open questions in design notes,
- accept or reject ADRs,
- listen to audition packages and fill in listening sheets,
- decide on any tolerance change.

## Defaults you may want to change first

C++20, CMake presets, Catch2, JUCE + ARA SDK confined to `src/host` and `src/ui`, `Result<T>` instead of exceptions. Change them in `CLAUDE.md` before Phase 0 starts, or later through an ADR.
