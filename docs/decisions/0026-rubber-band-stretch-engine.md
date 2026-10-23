# ADR-0026: Rubber Band R3 as an optional stretch engine (GPL-2.0-or-later, vendored)

- **Status:** accepted for integration (owner, 2026-10-23: "implementing it now is probably better for dependency reasons"); the choice of default engine is open until the owner has listened to the A/B renders.
- **Phase:** 5 (timing)
- **Related:** ADR-0014 (the GPL-2.0-or-later consequence is already accepted), ADR-0023 (stretched spans), `docs/design/tsm-research.md`.

## Context
`stretch_span` is a TD-PSOLA renderer for monophonic voiced material plus a grain path for the rest; it is mono-only, and the owner still hears artifacts in stretched vowels (listening, 2026-10-23). The research note (`docs/design/tsm-research.md`) reviews the alternatives; Rubber Band is a mature phase-vocoder stretcher (R2 faster, R3 finer) with an offline mode, exact output length, any channel count, and `setKeyFrameMap` for pre-planned variable stretch. Adding the dependency now settles the licence, build and layering questions before more is built on the PSOLA path.

## Decision
1. **Vendor** Rubber Band unmodified in `third_party/rubberband` (commit e4296ac, 4.0.0 line; see `ANNODYNE.md` there), built from upstream's single-file unit by `cmake/AnnoRubberBand.cmake` as `anno_rubberband` (built-in resampler, vDSP/Accelerate FFT on Apple and the built-in FFT elsewhere, no threads, `-ffp-contract=off`). The licence is GPL-2.0-or-later, consistent with ADR-0014 (no closed edition); credit is added to `THIRD_PARTY.md`.
2. **Layer rule:** only `core/synthesis/stretch` may link `anno_rubberband`, privately (AnnoLayers external allowlist); no Rubber Band header appears in a public header.
3. **API:** `StretchParams::engine` (`enum class StretchEngine : uint8_t {psola (default), rubberband}`). With `rubberband`, `stretch_span` renders the span's source samples through R3 (offline, one thread, no pitch change, channels together), cut or padded to exactly the span's output samples, **any channel count**; over the first and last 8 ms the result is blended (equal power) into an exact slope-1 copy of the source, so the span meets its neighbours on the source's own samples; a span of under 2048 source samples is resampled linearly. The whole span is rendered whatever range is asked for, so a range is a slice of the same samples.
4. **Cache:** `render_key` hashes the engine; `k_key_format_version` is 7.
5. **Selection:** the CLI scenario key `"stretch_engine": "psola" | "rubberband"` (A/B). The default stays `psola` until the owner decides.

## Consequences
- Stereo and multi-channel material can be stretched (with `rubberband`); PSOLA stays mono-only.
- **Vibrato is not preserved** by this engine (the pitch contour is re-timed like any stretcher); the vibrato regeneration of ADR-0023 amendment 3 applies to the PSOLA path only. Whether the regenerated vibrato can be applied on top of a Rubber Band render is open.
- **Cost:** a span is rendered whole for each requested range (O(span) per call), which is fine for a one-shot render and wrong for interactive block rendering of long spans; a per-span cache in `core/render` is the follow-up if the engine is adopted.
- Determinism holds per build and platform (R3 offline, one thread); across platforms the FFT backend differs, as for the other libm-dependent code.
- Not verified here: formant and pitch features (unused), R3's behaviour at slopes outside 0.25..4 (the stretch limits still apply).

## Alternatives considered
- Signalsmith Stretch (header-only, lowest friction) and SoundTouch (WSOLA) are in the research note; neither has an offline exact-length mode with a key-frame map. They remain candidates for the same `StretchEngine` switch.
- Replacing the PSOLA path now: rejected until the A/B listening.
