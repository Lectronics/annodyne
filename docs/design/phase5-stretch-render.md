# Design note: Time-stretch rendering (Phase 5, slice 1)

- **Phase:** 5 (Timing)
- **Status:** implemented, as built. Superseded as the default renderer by Rubber Band (ADR-0026 amendment 1); the PSOLA path described here is retained as the alternative. Was draft, revision 2 (structure review round 1 answered, see the review log)
- **Spec sections:** `annodyne-core-architecture.md` §2 (time axes), §4.5 (`SynthesisEngine`), §4.6, §5.3–5.5; ADR-0006 (render API), ADR-0008 (TimeMap spans), ADR-0016, ADR-0017 (frame classes), ADR-0018 (stretch research items), ADR-0021 (engine choice), ADR-0023 (this slice, proposed).
- **Folders / CMake targets:** `src/core/synthesis/grain/` (new, layer 2), `src/core/synthesis/stretch/` (new, layer 2), `src/core/synthesis/psola/` (refactor onto `grain`, output unchanged), `src/core/synthesis/engine/` (`only_timing_edited`), `src/core/timemap/` (whole-sample anchors), `src/core/render/` (stretched spans).
- **Relation to `note-stretching.md`:** that note is the full plan (segments, allocation, vibrato regeneration, splices). This note is its first slice. See "What this changes".

## Revision 4 (owner, 2026-10-22): read this first

ADR-0023 amendment 1 changes four things in the text below. (a) The result of `render_unedited` is the **base clip**: timing edits belong to it, not to the source. (b) Start and end are **silent anchors**; `keep_length` is the default for `render_region` once the layout path lands (the `shift` policy of rule 5 stays as an option, and is used until then). (c) Shifts are quantised to a **shift grid** (1/512 note, normalised to whole samples), not to a single sample. (d) The cache key hashes only the spans a block reads and `dirty_regions` returns the intervals whose spans changed, so rule 7 ("a stretch invalidates every block after it") no longer holds for the layout path. Spans are whole numbers on both axes (N source samples to M output samples).

## Purpose

A `TimeMap` warp with slope != 1 is `not_implemented` today in `read_through_map`, the three engines and `render_region`, although `build_time_map` already produces such maps (length, attack time and handle edits). This slice renders them: audio played through the map, slope-1 stretches copied bit for bit, stretched stretches re-synthesised from grains of the source so pitch and timbre stay while the duration changes.

## Where stretching happens (the finding that shaped this revision)

`build_time_map` stretches **everything between two anchors**: the edited note, the unedited notes and the gaps around it, and the tail up to the end of the recording (ADR-0008 §4's shift-first layout is a later slice and reduces this). So a stretched span is not an "edited note". The renderer therefore changes as follows (ADR-0023):

1. **The pristine base.** `render_region` builds the unedited signal over the region from `read_through_map` today. It becomes `render_unedited(source, analysis, map, range)`: slope-1 warps and inserts exactly as `read_through_map` gives them (bit for bit), and every slope != 1 warp rendered by `stretch_span` (below). "No edits, same audio" holds because a stretch with no pitch/level edit is the unedited signal over that map; the unedited-note and gap parts of a stretched span need no engine, only the stretch.
2. **Edited notes** are still rendered by the engine passed to `render_region`, as today, and blended over the base. `render_zone` compares the engine zone with the base (not with `read_through_map`) to set `identical`.
3. **A note with a pitch/formant/level edit whose zone (the note's output span widened by the crossfade margin) meets a slope != 1 span** returns `not_implemented` with the note id in the message ("stretch combined with a pitch edit, slice 3"). `make_plan` checks this before any rendering, so a failure is clean and names the note. A note whose only edits are timing edits is **not** an edited note for the blend: it is part of the base. (`make_plan` runs this check, on the zone, before any rendering; `render_zone`'s `identical` comparison keeps using `read_through_map` over the zone, valid because the zone is slope-1 by this check. `make_plan` today lists every `eff.is_edited` note; a timing-only note is dropped from the list when the map renders it. Pitch edits on notes in a slope-1 part of a map that also has stretches elsewhere work as today: the engine only sees the zone.)
4. **Mixed tracks.** A track with a pitch-edited note in a slope-1 part and a stretch elsewhere renders (rules 1 and 3). One engine renders edited notes (`render_region` takes one); stretch is not an engine choice but part of the base. The routing engine that combines pitch and stretch on one note is slice 3 with `tools/time`.
5. **Tail policy: shift, do not squeeze.** With the recording's end as a fixed anchor (today's default) one length edit compresses the whole rest of the recording by a tiny slope (about 0.9999), which would run every later unedited note and gap through the stretcher: pointless for a sub-percent change, degrading, and O(recording) per block. `TimeMapParams::tail` is a new enum `{keep_length, shift}` (default `keep_length`: every existing test and caller unchanged). `render_region` uses `shift`: the audio after the last edited anchor is a slope-1 warp shifted by the accumulated change, the output is longer or shorter than the source (the plan already takes its length from the map). Stretching is then confined to the spans between edited anchors: the edited note's body, plus what a handle dragged across neighbours forces (Flex, ADR-0005 §16; the shift-first layout of ADR-0008 §4 reduces that further in the gap work). Rule 5 therefore holds where it can: everything outside the stretched spans is a whole-sample shift of the source. (Rule 5 / `architecture.md` "output outside dirty regions equals the source" is already relative to `dirty_regions`, which returns origin-to-end for any timing edit (ADR-0005 §16); ADR-0023 states that stretched spans are inside it and the audio-verifier's check is defined against the shifted source.)
6. **Whole-sample shifts.** A slope-1 span's shift d = t - s must be a whole number of samples (`read_through_map`), and the source anchors s are fractional. `TimeMapParams::sample_rate` (default 0 = off) quantises **the shift of every anchor**, t' = s + round((t - s) * rate) / rate, after `enforce_min_segments`: two anchors of one slope-1 span have equal d, hence equal d', so the span stays slope 1 with a whole-sample shift. Strictness: if t'_{i+1} <= t'_i the later anchor is pushed one sample (a whole-sample push keeps d' whole); a pushed anchor that would exceed the fixed end (keep_length) cancels the quantisation for the whole map (the unrounded map is used and `repaired_notes` is raised). `repaired_notes` is decided **before** rounding, so rounding alone never raises it. A segment shorter than one sample after rounding is merged by the push rule. A stretched span's ratio changes by under one sample over the span. `render_region` sets `sample_rate`. Caller-supplied maps must satisfy the same: a fractional shift on a slope-1 span stays `not_implemented`.
7. **Cache and dirty region.** `dirty_regions` already returns "origin to end of recording" for a timing change, and the cache key includes the whole map and the stretch parameters (`RenderParams::stretch` is hashed by `make_render_key`), so a stretch invalidates every block after it. Accepted for slice 1; incremental invalidation is a later optimisation. A stretched voiced span chains its synthesis marks from the span start (periods are placed one after another), so a block inside a span costs O(span up to the block); spans are now bounded (rule 5), and a cost test with a block late in a 5 s span records the time.
8. **Reporting.** `TimeMapBuild::repaired_notes` is now audible (a request that could not be honoured). `RenderResult` gets `repaired_notes` (int, default 0) and `stretched` (bool: any slope != 1 span rendered over the region). `passthrough` stays `notes_rendered.empty()` and its doc says it does not imply the audio equals the source when `stretched` is true.

## Overview

```
render_region
  base   = render_unedited(source, analysis, map, range)
             slope-1 warp / insert -> read_through_map bytes
             slope != 1 warp       -> stretch_span()
  edited notes (pitch/level/..., slope-1 zones only) -> engine.render_note, blended as today
```

`stretch_span` (in `core/synthesis/stretch`, a free function, the engine of this slice; a `SynthesisEngine` wrapper is not needed because stretching is not a per-note choice):

```
stretch_span(src, analysis, span, out_range, params)
   per output sample range, by the class of the source frame at M^-1(position):
     voiced (voiced_classes)   -> synthesise_voiced()   TD-PSOLA time scaling
     other (noise, silence, breath, rough, plosive) -> synthesise_noise()  power-complementary grains
   class edges inside a span  -> aligned equal-power crossfade (class_edge_fade)
```

Frame classes come from the note covering the source time (`Note::frame_class` on the note's grid). Outside every note (gaps, tails) there is no class: the noise path is used, with an exception: a gap that is digital silence is rendered as silence (the noise path of zeros is zeros). Inside a note, beyond its first/last frame (the margin), the edge frame's class is held, as ADR-0016 does for pitch.

## Public types

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `StretchParams` | struct | validated by `check_stretch_params` (returns `Result<void>`; called first by `stretch_span`) | caller | value |
| `grain::Signal`, `grain::Grain`, `grain::Accumulator`, `grain::PeriodCurve` | structs moved from `psola_engine.cpp` | as today | the render call | value |

```cpp
struct StretchParams {
    Seconds class_edge_fade{0.004};             // aligned equal-power crossfade at class edges
    Seconds noise_grain{0.024};                 // unvoiced grain length (sine window, 50 % overlap)
    double  noise_jitter_hop_fraction = 0.25;   // seeded random read-position jitter, 0..0.5
    std::uint64_t noise_seed = 0x5eed;
    Ratio   min_slope{0.25}, max_slope{4.0};    // outside: not_implemented (the layout never asks)
    FrameClassSet voiced_classes = {FrameClass::voiced, FrameClass::breathy};  // as PsolaEngineParams
};
// RenderParams gains `StretchParams stretch` (hashed into the cache key with a version constant).
```

## Public functions

```cpp
/// The audio of output range `out_range` of one stretched warp `span`, pitch and timbre kept.
/// Positions are output samples at the source's rate; M^-1 gives SourceTime and the source is read
/// directly on the source axis (fractional positions, Catmull-Rom; samples beyond the recording
/// count as zero, three longest periods of context). Span boundaries are the output sample indices
/// round-to-nearest of output_start/output_end, so adjacent spans share their rounded boundary: no
/// gap and no overlap with a neighbouring copy.
/// Voiced: TD-PSOLA time scaling: analysis marks (grain::analysis_marks), synthesis marks one source
/// period apart, each taking the analysis mark nearest M^-1(position); the first synthesis mark sits
/// ON an analysis mark at the span start so the output is phase-continuous with a preceding
/// slope-1 copy (no crossfade needed there); periods are repeated (slope > 1) or skipped (< 1).
/// Noise: sine-window grains of noise_grain at 50 % overlap, read at M^-1 plus seeded jitter, the
/// seed derived from (noise_seed, span start sample, grain index), so any zone renders the same
/// samples regardless of call order. Per-grain random phase is NOT used: sine-window grains read
/// from partly overlapping source regions are partly coherent at slope > 1, and the envelope ripple
/// test below measures it; the sibilant/breath path is replaced if it fails the test.
/// Errors: invalid_argument (bad params, range outside the span), not_implemented (stereo, slope
/// outside [min_slope, max_slope]), out_of_range (as passthrough). Const; thread-safe; not RT-safe.
[[nodiscard]] Result<AudioBuffer> stretch_span(const AudioSource& src, const AnalysisResult& analysis,
                                               const TimeSpan& span, OutputRange out_range,
                                               const StretchParams& params);

/// Output samples [begin, begin+count) of the unedited recording through `map`: slope-1 warps and
/// inserts as read_through_map, slope != 1 warps through stretch_span. Replaces the call
/// read_through_map in render_region. Errors: those of both.
[[nodiscard]] Result<AudioBuffer> render_unedited(const AudioSource&, const AnalysisResult&,
                                                  const TimeMap&, Samples begin, Samples count,
                                                  const StretchParams&);
```

`render_unedited` lives in `core/render` (layer 4: it composes `read_through_map` from `engine` and `stretch_span` from `stretch`), internal to `render.cpp` (one caller; made public only if tests need it). `render` links `stretch` privately unless `StretchParams` is in a public render header, which it is (`RenderParams::stretch`), so PUBLIC.

`engine_support` gains `only_timing_edited(eff)`: `onset_shift`, `length_scale`, `handles`, `attack_time_scale` may be non-neutral; every other effective value must be neutral and `center == note.pitch_center` within `k_neutral_snap_offset`. The renderer uses it to classify a note as "timing only" (part of the base) or "edited" (needs an engine).

## Effect on audio

- Changes only regions whose map has a slope != 1 (those failed with `not_implemented`). Samples in slope-1 spans are byte-identical to `read_through_map`.
- Pitch is preserved by construction. Vibrato rate is **not**: re-timed contour changes it by the slope (fixed later by `regenerate_pitch_curve`); the audio-verifier reports it.
- The stretched voiced part is phase-continuous with a following slope-1 copy only if the last synthesis mark is placed ON an analysis mark at the span end (same rule as the start); otherwise a short aligned crossfade (`find_splice_offset`, equal power when the correlation is below 0.8, linear when above, as note-stretching.md §5) is used. Test: pure harmonic tone, level within 0.5 dB across both joins.
- New objective checks (audio-verifier): length to the sample, pitch within 10 cents, no clicks at joins, level of unvoiced stretches within 1 dB and short-time level ripple at 2x and 4x, determinism (twice = same hash), no change outside the dirty region.

## What this changes in `note-stretching.md` and the ADRs

Superseded for slice 1: the engine choice (`harmonic_noise` for steady regions: ADR-0021 chose PSOLA and this slice uses its grains); the dependencies (`stretch` links `grain`, not `psola`/`harmonic_noise`/`phase_vocoder`); `plan_note_stretch` lives in `synthesis/stretch` and needs `allocate_stretch` from layer 3, which breaks the layer rule: planning (segments, allocation) goes into `core/timemap` when it arrives (slice 2) and only the resulting map reaches synthesis; `render_note_stretch`'s signature (replaced by `stretch_span`). Unchanged: segmenting into attack/sibilant/steady/release, allocation maths, `regenerate_pitch_curve`, splices, the tests listed there (slice 2 and later). ADR-0008 §7 ("slope != 1 stays not_implemented until Phase 6") and the `read_through_map` doc are moved to Phase 5 by ADR-0023. ADR-0018's "plosive bursts repositioned, not stretched; stretch vowels only" is **not** met in slice 1: plosives and sibilants inside a stretched span go through the noise path. The weights that keep consonants short are slice 2 (they change the map, not this engine); until then the attack protection of `build_time_map` is the only protection.

## Split / join decisions

- **`grain` extracted** (coding-standards §5 "new subfolder when 3+ files share a concept": here two users, so the argument is the rule of three's exception): a copy of the 60-line tuned marks algorithm in `stretch` would drift from PSOLA and breaks the bit-identity gate; `psola` keeps its own behaviour; precedent: `engine/engine_support.h`. **Moves to `grain`:** `Signal` (fractional reader), `PeriodCurve` with its frame type (`PitchFrame`, `Run` stay in `psola`; `grain` takes a plain period callback or a `PeriodCurve` built from (position, period) pairs, to be decided in the header), `analysis_marks` and its constants, `Grain`, `add_grain`, `Accumulator`. **Stays in `psola`:** `build_pitch_frames`, `class_is_shifted`, `average_cycles`, the separation and `respace_zone`. Public headers: `grain/signal.h`, `grain/marks.h`, `grain/overlap_add.h`. `grain` links only `core/types` and `core/dsp`; `stretch` links `core/types`, `core/dsp`, `grain` (and `engine` only if `engine_support` is used: not needed, so no).
- **`-ffp-contract=off`** (psola/CMakeLists.txt does it on non-MSVC for reproducible renders) is set on `grain` and `stretch`. **The refactor is its own commit, first**, and the PSOLA fingerprint tests run before and after it unchanged. Stretch code is added only after that passes.
- **`stretch_span` is a free function, not an engine**: it is not chosen per note (it renders the unedited base), so a `SynthesisEngine` would have no second caller (join rule 5).
- **`synthesise_voiced` and `synthesise_noise` separate** (split rule 6).

## Dependencies

`grain`: `core/types`, `core/dsp`. `stretch`: `core/types`, `core/dsp`, `grain`. `render`: adds `stretch`. `psola`: adds `grain`. READMEs for `grain` and `stretch`; `src/core/synthesis/README.md` updated (it still says real engines arrive in Phase 4+ and one folder per engine).

## Limits (stated)

1. Stretch combined with a pitch/formant/level edit on a note in a stretched zone: `not_implemented`, naming the note (slice 3).
2. Vibrato rate changes with the stretch. The tool layer must not offer a stretch on a vibrato note without a warning (slice 3).
3. Plosives and sibilants are stretched where the map says (see above); transients inside a stretched span are smeared.
4. Stereo and polyphony: `not_implemented`.
5. A stretch dirties and invalidates the cache from the start of the recording (accepted; optimisation later).

## Tests (written first)

- **grain refactor:** existing PSOLA fingerprint tests unchanged before and after (own commit).
- **Contract:** output length = `round(map output length)`; spans with fractional-sample boundaries give no gap/overlap; ratio-1 map equals `read_through_map` bytes; twice = same bytes; the zone render equals the corresponding slice of a larger render (call-order independence); slope exactly 0.25 and 4.0 work, just outside is `not_implemented`; stereo `not_implemented`; bad params, range outside the span: `invalid_argument`; note with empty/mismatched `frame_class`: `invalid_argument` or noise path as documented; inactive note ignored.
- **Voiced synthetic:** harmonic tone 110/220/440 Hz stretched 0.5x, 1.5x, 2x and a ramp: f0 within 10 cents; level within 1 dB; across a copy-to-stretch and stretch-to-copy join the level is within 0.5 dB and there is no discontinuity above the signal's slope bound; a voiced run shorter than one period does not crash.
- **Noise synthetic:** white and band-limited noise: level within 1 dB; centroid within 5 %; autocorrelation at the grain hop below 0.3; short-time level standard deviation at 2x and 4x below a stated bound (the bound is measured on the source's own noise first and recorded).
- **Mixed:** a vowel with a noise burst before it; the burst does not enter the voiced path.
- **Render:** `build_time_map` output (a handle edit that stretches an unedited neighbour and a gap) renders end to end; with `tail = shift` the region after the stretch is the source shifted by the whole-sample offset, byte for byte; a 0.25x note shorter than twice the margin; a pitch-edited note in a stretched zone returns `not_implemented` with its id; a pitch-edited note in a slope-1 part of a map with a stretch elsewhere renders; inserts and cuts inside the range; `repaired_notes` reported.
- **Whole-sample shifts and tail policy:** `build_time_map` with `sample_rate` set: slope-1 spans have whole-sample shifts (a length-only edit with fractional note times; a pure fractional `onset_shift` move renders as the whole-sample shift); two anchors closer than a sample are pushed, never collide; `min_segment` held after rounding; `repaired_notes` not raised by rounding alone; a push past the fixed end cancels the quantisation and raises it; unset gives today\'s maps and `tail = keep_length` gives today\'s maps (all existing tests unchanged); `tail = shift` leaves the tail a slope-1 warp and the output length longer by the net change.
- **Plan, blend, cache:** a timing-only note is not in `notes_rendered`; `stretched` and `repaired_notes` set; the cache key changes with `StretchParams` and a second identical call hits; a pitch-edited note whose margin meets a stretched neighbour returns `not_implemented` naming it; a TSan test with concurrent stretched renders through the cache; existing tests that expect `not_implemented` for a timing edit (`resolve_time_map`) and the strings "stretching is Phase 6" (`render.cpp`, `render.h`, `source_through_map.h`) are updated.
- **Edges and silence:** a digital-silence gap stays exactly zero (no NaN, no denormals); a first or last note inside a stretched span; class-edge crossfade where `frame_class` is missing; a block late in a long span (cost recorded).
- **Real voice:** a Vocadito track stretched 1.5x and 0.7x, objective checks above (the hiccups of Phase 4 were found only on real voices). Cost: a 5 s note at 2x is rendered within a stated time (measured and recorded).
- **Audio scenarios** `tests/audio/scenarios/time/`: `stretch_vowel_1_5x`, `stretch_vowel_2x`, `shorten_vowel_0_5x`, `stretch_with_t_onset_2x`, `stretch_sibilant_1_5x`, in the audition package `phase5-timing`.

## Review log

- **Round 2 (2026-10-22): CHANGES REQUIRED, B1 (rounding t does not make shifts whole-sample).** Answered: rule 6 (round the shift, after `enforce_min_segments`, strictness push, fixed-end fallback, `repaired_notes` before rounding), a new rule 5 (tail policy: shift, not squeeze, found while answering B1 and S5: the fixed end would have stretched the whole recording), S1 (chain cost, rule 7), S2 (`RenderParams::stretch` hashed), S3 (zone wording, `stretched`, `passthrough`), S4 (period source and `grain::period_at`), S5 (rule 5 stated relative to dirty regions, in ADR-0023), S6 (tests).

- **Round 1 (structure-reviewer, 2026-10-22): CHANGES REQUIRED, B1–B7 (+S6).** Answered in this revision: B1 (the pristine base, unedited notes and gaps in stretched spans, `render_unedited`), B2 (`only_timing_edited`), B3 (mixed tracks stated; combination is a named error, routing is slice 3), B4 (`-ffp-contract=off` on both targets, refactor as its own commit), B5 (exact symbol list, rule reworded), B6 (phase-continuous start, aligned splice otherwise, 0.5 dB join test), B7 (classes outside the note, `voiced_classes`, errors), S6 (whole-sample anchors in `build_time_map`), S1 (types, axes, seeds), S2 (ripple test, no per-grain random phase, stated), S3 (what supersedes what, ADR-0023), S4 (dirty/cache accepted and stated, `repaired_notes`), S7 (test gaps).
