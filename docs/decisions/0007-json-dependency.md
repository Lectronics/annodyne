# ADR-0007: nlohmann/json for scenarios and the CLI

- **Date:** 2026-10-19
- **Status:** proposed (tech-stack change; needs the owner to confirm at the Phase 2 close)
- **Phase:** 2

## Context
`anno-cli render --edits` and `audition` read scenario files (AUDIO_VERIFICATION §2), and the audition
package writes `edits.json`. Hand-writing a JSON parser is more risk than a vetted library.

## Decision
Add nlohmann/json 3.11.3 (MIT, header-only) with `FetchContent` pinned to a tag, `SYSTEM` includes,
same pattern as Catch2 (with `FIND_PACKAGE_ARGS`). It may be linked only by `cli` targets
(`^cli(/[a-z0-9_]+)?$`) and tests; `core/*` never links it. The layer script's external allowlist
gets exactly that rule, with layer-rule tests for an accepted and a rejected link. The project file
format (Phase 7) is a separate decision; it may reuse this dependency in `project/`, by its own ADR.

## Alternatives considered
- Hand-written parser: avoidable bug surface.
- RapidJSON/simdjson: no need for speed here.

## Consequences
One more FetchContent download in CI.
