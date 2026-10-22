# Design note: Phase 4, first audible edit (pitch)

- **Phase:** 4
- **Status:** reviewed (structure-reviewer round 3: approved with fixes, all folded in)
- **Spec sections:** implementation guide B5 (partials), B6 (harmonic + noise), B7 (envelope), B8 (engines), Tool 4 (Main Tool), Tool 5d (shift by one uniform offset); architecture §2 (HarmonicTrack, NoiseTrack, EnvelopeTrack), §4.2, §4.5, §5.3 (pitch fold), §6 (edited curves). ADR-0002, 0005, 0006, 0008, 0013, 0014.
- **Folders / CMake targets:**
  - `src/core/analysis/partials` (layer 1)
  - `src/core/analysis/harmonic_noise` (layer 1)
  - `src/core/analysis/envelope` (layer 1)
  - `src/core/synthesis/harmonic_noise` (layer 2)
  - `src/core/synthesis/psola` (layer 2)
  - `src/core/tuning` (layer 1, minimal chromatic grid only)
  - `src/tools/main_tool` (layer 5)

## Purpose
Move a note's pitch and have it come out right. A per-note `pitch` edit (`center_offset`, a `Cents` offset, ADR-0005) is folded into `EffectiveNote::center`; the engine shifts the **whole** pitch curve of the note by `eff.center - note.pitch_center`, so vibrato, drift, scoops, overshoot and fine fluctuation come along unchanged (note-pitch-target §5; spec Tool 5d). Formants stay where they are, noise is not shifted, samples outside edited notes stay bit-identical. Two engines are built behind `SynthesisEngine` so a human can compare them (listening gate #1, then an ADR picks the default).

Nothing here chooses *which* notes to move or by how much. That is Correct Pitch (Phase 6, with `pitch_target_mode`, owner decision 2026-10-20). Main Tool only turns "this note should be at pitch P" into an `Edit`.

## Waiting on the owner's pitch-correction research
The owner is collecting research on pitch correction across consonant types and vibrato. Phase 4 therefore fixes the **mechanism** and keeps the **policy** in parameters, with provisional defaults named here, so findings change a parameter table and not a type:
- Which frame classes are shifted (`HarmonicNoiseParams::shifted_classes`, default: `voiced` only). Voiced consonants (m, n, l, v, z, r), glides, plosive bursts and fricatives are open questions; today only `FrameClass` (voiced, sibilant, breath, plosive, silence) exists, so voiced consonants shift with their vowel and `Note::attack_end` is only used for crossfade placement.
- How wide the crossfade between shifted and original audio is at the consonant | vowel boundary and at the edges of unshifted runs inside a note (`consonant_fade_s`, `class_edge_fade_s`); the fade at the note's own edges belongs to the renderer.
- Whether vibrato amplitude is kept exactly (default, the owner's rule: one uniform offset) or re-centred. No such option is built until the research asks for it.
All of these thresholds are provisional in the sense of the ROADMAP fuzzy-decisions item.

## Public types

| Type | struct/class | Invariant | Owner | Shared how |
|---|---|---|---|---|
| `PartialsParams` | struct | ranges below | caller | value |
| `HarmonicNoiseParams` | struct | ranges below | caller | value |
| `EnvelopeParams` | struct | ranges below | caller | value |
| `HarmonicNoiseEngine` | class | immutable after construction; `settings_key` reflects every field of its params | caller | const reference / shared_ptr |
| `PsolaEngine` | class | same | caller | same |
| `PitchDrag` | struct | `target` finite | caller | value |
| `ChromaticGrid` | struct | `a4_reference` finite and > 0 | caller | value |
| `NoteCurves` | struct | grid valid; drift/mod sized `grid.n_frames` | caller | value (views) |

```cpp
namespace anno {
struct PartialsParams {
    int max_harmonics = 60;           // 1..200; capped by Nyquist (0.45 * rate) per frame
    double window_periods = 4.0;      // f0-adaptive window length in periods, 4..8 (Blackman-Harris)
    double fft_pad_factor = 5.0;      // zero-padding for QIFFT, >= 4 (spec B5)
    Cents search_halfwidth{40.0};     // sieve window around k * f0, 10..100
};

struct HarmonicNoiseParams {
    int n_bands = 32;                       // 8..64
    Hz sibilant_centroid{4500.0};           // provisional (fuzzy later)
    double breath_aperiodicity = 0.7;       // dimensionless 0..1, provisional
    double plosive_flux = 4.0;              // dimensionless, provisional
    Decibels silence_level{-70.0};          // provisional, frame level below which a frame is silence
};

struct EnvelopeParams {
    double order_scale = 1.0;         // dimensionless; cepstral order = floor(order_scale * rate / (2 * max_f0)) (IRCAM rule)
    int max_iterations = 50;          // True Envelope loop, 5..200
    Decibels tolerance{0.2};          // stop when the envelope is within this of every harmonic peak
};

struct PitchDrag { NoteId note; Cents target; };   // absolute cents, A4 = 6900
struct ChromaticGrid { Hz a4_reference = k_a4_standard_hz; };   // 12-TET around a4_reference

/// What the analysis stages read of a note (ADR-0010 style; no half-built Note).
struct NoteCurves {
    FrameGrid grid;
    Cents pitch_center;
    std::span<const Cents> pitch_drift;
    std::span<const Cents> pitch_mod;
};
}  // namespace anno
```

Time and units: every track is on `Note::grid` (source axis, 5 ms hop). Frequencies in Hz inside `HarmonicTrack`, pitch in `Cents`, levels linear in tracks and dB in `EnvelopeTrack`, as `core/types/tracks.h` already says.

## Public functions

```cpp
/// Harmonic tracks of the note described by `curves`, measured in `mono` (the source's mono mix, original, unmodified).
/// Frame i uses f0(i) = note.pitch_center + drift[i] + mod[i] as the sieve centre; harmonic k is
/// searched within search_halfwidth of k*f0, each measured with a window of window_periods
/// periods, zero-padded fft_pad_factor times, QIFFT-refined; with slope_correction the frequency
/// slope of the frame is estimated and its bias removed. Unvoiced note frames (no f0 on the grid)
/// get amp 0. phase_rad is the measured phase at the frame centre.
[[nodiscard]] Result<HarmonicTrack> analyse_partials(std::span<const float> mono, Hz sample_rate,
                                                     const NoteCurves& curves, const PartialsParams& params);

/// Residual after subtracting the additive resynthesis of `harmonics`, split into `n_bands`
/// band energies and classified per frame. The residual itself is NOT stored in the Note (see
/// "Engine" below): the engine recomputes it from source + harmonics with the same synthesis.
[[nodiscard]] Result<NoiseTrack> split_harmonic_noise(std::span<const float> mono, Hz sample_rate,
                                                      const NoteCurves& curves, const HarmonicTrack& harmonics,
                                                      const HarmonicNoiseParams& params);

/// True Envelope through the harmonic peaks, cepstral order from max f0 in the note.
[[nodiscard]] Result<EnvelopeTrack> estimate_envelope(const NoteCurves& curves, const HarmonicTrack& harmonics,
                                                      Hz sample_rate, const EnvelopeParams& params);

/// Nearest 12-TET pitch to `pitch` on the grid (absolute cents, A4 = 6900 at a4_reference 440 Hz;
/// a4_reference shifts the grid by 1200 * log2(a4_reference / 440) cents, via to_hz/to_cents). Ties
/// round toward +infinity. Non-finite pitch or a4_reference not finite and > 0: invalid_argument.
/// Pure; real-time safe.
[[nodiscard]] Result<Cents> target_pitch(Cents pitch, const ChromaticGrid& grid);

/// Main Tool pitch drag: one `pitch` edit spec (category pitch, op set, `center_offset`) that puts the
/// note's pitch target at `drag.target`: offset = drag.target - note.pitch_target.target.
/// Baseline is the analysed target, so repeated drags land on the last target. Does not read audio, does not snap (the caller passes the final target; snap_chromatic is a
/// separate call). Errors: invalid_argument (non-finite target, note unknown or inactive,
/// note kind `unpitched` or `multi` because they have no single target), out_of_range (|offset| above
/// k_max_pitch_offset, R3). Pure; thread-safe.
[[nodiscard]] Result<EditSpec> pitch_drag_edit(const AnalysisResult& analysis, const PitchDrag& drag);  // R1, R2
```

| | |
|---|---|
| Inputs | mono mix as `span<const float>` + rate (from `read_mono`; the source is never modified), the `Note` with centre/drift/mod filled by Phase 3 |
| Returns | new tracks by value; `Note` is immutable, so `analyse()` builds the final `Note` with the three tracks in place |
| Errors | `invalid_argument` for bad params, empty/NaN input, a note whose grid disagrees with the track; `internal` if a post-condition fails (NaN output, negative amplitude) |
| Pure | yes |
| Thread safety | thread-safe (no shared state) |
| Real-time safe | no (allocates). Analysis is offline |
| Cost | about 60 FFTs per voiced frame at 200 frames/s on the 5 ms grid: a few times real time on one core; measured at stage close |

### Engines

```cpp
class HarmonicNoiseEngine final : public SynthesisEngine { ... };   // name "harmonic_noise", version 1
class PsolaEngine         final : public SynthesisEngine { ... };   // name "psola", version 1
```

**HarmonicNoiseEngine::render_note** (const; thread-safe; allocates):
1. Fetch the original samples of the blend zone through the time map (`read_through_map`; slope-1 only, `not_implemented` otherwise, as the passthrough engine).
2. Per frame, `shift(t) = eff.center - note.pitch_center` in cents (a constant here; the structure allows a curve for Phase 6); new frequency `f_k'(t) = f_k(t) * 2^(shift/1200)` for the **measured** `freq_hz` (so inharmonic stretch survives); drop harmonics above `0.45 * rate`.
3. **Residual** `r = original - additive(original harmonics)` is recomputed here from the source and the stored `HarmonicTrack` (same oscillator bank, original freqs/amps/phases). It is what is left unshifted, so nothing but the harmonic model is stored in the Note. Output = `additive(shifted harmonics) + r` over frames whose class is in `shifted_classes`; elsewhere (and wherever all harmonic amplitudes of the frame are zero or its class is not in `shifted_classes` (R5)) the original samples are used.
4. Amplitudes after the shift are re-read from the **envelope** at the new frequencies (formant offset 0, so `env'(t,f) = env(t,f)`): `a_k'(t) = a_k(t) * E(f_k'(t)) / E(f_k(t))`, with E interpolated linearly in dB across the stored `freqs`. This keeps formants where they were while the harmonics move. Hollow/buzzy timbre on large upward shifts is measured in the listening package, not claimed fixed (carried item, note-pitch-target §9).
5. Phase: waveform shape is kept by phase-locking harmonics to the fundamental. `phi_k(t) = k * phi_1(t) + theta_k(t)` with `theta_k` the measured offset `phase_k - k*phase_1`, and `phi_1` the running integral of `f_1'(t)` anchored at the first frame of each voiced run to the measured `phase_1`.
6. Margin: the tracks cover only the note, so in the blend-zone margin the engine **holds the first/last frame's harmonic model** (frequency and amplitude held, phase running on, shifted like the note), so the renderer's ramp (`blend_plan`, pristine to note and back) is a real equal-power fade and the engine adds none at the note edges. Inside the note it crossfades only at internal boundaries: `consonant_fade_s` (default 12 ms) at `attack_end` and `class_edge_fade_s` (default 8 ms) at edges of runs not in `shifted_classes` (`core/dsp/crossfade`). Samples of unshifted runs are the original bits. If the first or last note frame is not in `shifted_classes` (consonant, silence), there is no model to hold: the margin on that side is the original samples and the engine fades at the edge of the shifted run. Tests, for both engines: click detector zero at note edges; ramp energy equal-power with no double fade.

**PsolaEngine::render_note** (comparison): TD-PSOLA from epoch marks derived from the stored f0 track (one mark per period, snapped to the local waveform peak), grains of two periods, Hann-windowed, re-spaced by the pitch ratio, unvoiced stretches copied through unchanged (so noise is never shifted). Offset only; `not_implemented` for any shift above `PsolaParams::max_shift_cents` (default 700, where PSOLA is not trustworthy) so the caller falls back to the other engine. Margin: it extends the first/last period's grain spacing and pitch ratio into the blend margin so the renderer's ramp is a real fade; the engine fades only at internal boundaries, as the harmonic engine does.

**Stage 6 amendment (2026-10-21).** The implemented `PsolaEngine` differs from the paragraph above as follows (ADR-0017 amendment): it moves everything inside a frame of a shifted class, noise included (that is the point of the comparison; unvoiced classes are still copied through); its parameters are `PsolaEngineParams{shifted_classes (voiced, breathy), class_edge_fade (4 ms), max_shift (Cents, 700)}` (R14's `psola_max_shift` / `max_shift_cents`); the margin continues the shifted edge run with the edge pitch held for at most 0.1 s inside the zone (then the original), rather than extending grain spacing. Shared pieces (`only_pitch_edited`, `zone_shift`, `hex64`, `run_weights`) live in `synthesis/engine/engine_support`; `FrameClassSet` in `core/types`. Algorithm details are in the header and `synthesis/psola/README.md`.

Both engines return `OutputAudio` whose range equals `out_range` to the sample (the `SynthesisEngine` contract). `settings_key` contains every parameter; `version` is bumped on any output change.

## Effect on audio
- **Changes rendered output:** yes, the first phase to do so. Category `pitch` only (`center_offset`; `onset_shift` through a slope-1 time map is also realised), notes with `eff.is_edited == true` and a non-zero shift. Mod/drift scales, formant, gain, noise gains and timing stay neutral and keep the passthrough path.
- **Formula:** `f0'(t) = f0(t) + (eff.center - note.pitch_center)`; `pitch_ratio(t) = 2^((f0'(t) - f0(t))/1200)` (spec §6), harmonics re-read from the unchanged envelope.
- **Bit-identical:** an empty edit list, undone edits, every sample outside edited notes' blend zones, and unvoiced/noise frames inside an edited note except in the crossfade at their ends.
- **Objective checks (tests/audio, label `audio`):** pitch round-trip ±3 cents; vibrato rate ±0.2 Hz and extent ±10 % after a pure offset; formant preservation ±5 % after ±7 semitones; noise not shifted (sibilant centroid ±5 %); click detector 0; NaN/Inf, DC < -60 dBFS; determinism (hash); length ±1 sample; null, undo-null, untouched. Tolerances are not loosened (AUDIO_VERIFICATION §1).
- **New scenarios:** synthetic voiced vowel with vibrato; vowel + sibilant + breath; Vocadito excerpts with the offsets +-50, +-200 cents and +-7 semitones; engine A/B package `phase4-pitch`. No claim about how it sounds, only numbers plus the package for the human.

## Split / join decisions
- `analyse_partials`, `split_harmonic_noise`, `estimate_envelope` are three functions in three folders (split rule 3: different reasons to change, different test data), matching the architecture folder layout. They share only the `Note` and `HarmonicTrack`.
- The residual is **not** stored in `Note` (join rule: derived data that is cheap to recompute stays derived; the cache key already covers analysis version). If measurement shows the recompute dominating render time, a later ADR can store it.
- The engines are two classes (the interface exists for this); their shared crossfade and run-detection helpers live in `synthesis/engine` only if both need them after stage 6 (extract then, not before).
- `target_pitch` sits in `core/tuning` (the architecture spec's name, with only the chromatic grid); `pitch_drag_edit` is separate from snapping (split function rule 5: different inputs).
- `pitch_target_mode` is **not** here (owner, 2026-10-20): Phase 6 / ADR-0015. ADR-0013 item 6 is amended (done) to say Phase 6; Phase 4 decisions are ADR-0016 (written).

## Dependencies
- `analysis/partials`: `core/types`, `core/dsp` (`stft` is not enough for the zero-padded per-harmonic windows, so it uses `core/dsp/fft`, `window`).
- `analysis/harmonic_noise`: `core/types`, `core/dsp`; reads `HarmonicTrack`, does not call `partials`.
- `analysis/envelope`: `core/types`, `core/dsp/fft`.
- `synthesis/harmonic_noise`, `synthesis/psola`: `core/types`, `core/dsp`, `synthesis/engine`. Nothing from `analysis/*`: the engines read tracks inside `Note`.
- `tools/main_tool`: links only `core/types` (`find_note`, `validate_for_add` and `k_max_pitch_offset` are all there; it does not call `target_pitch`, so linking `core/tuning` would be unused). The layer-5 allowlist would also allow `core/edits` and `core/tuning`. It does not link `dsp`, `synthesis` or `render`.
- `analysis/orchestrator` gains the three new analysis targets.
- No new external library (pocketfft is already in `core/dsp`).

## Tests
- **Unit (first, they fail before implementation):**
  - Partials: pure harmonic tone, 1 kHz bin-centred and half-bin offset, f0 error < 1 cent on clean tones; vibrato tone (6 Hz, 50 cents) with and without slope correction (residual energy < gate measured at stage 1); glide; tone in noise at 20 dB SNR; Nyquist cap; unvoiced frames amp 0; K bounded; NaN-free; error cases.
  - Harmonic/noise: tone + shaped noise burst (sibilant), tone + breath-like noise, plosive-like click, silence; frame-class accuracy on construction; residual is noise-like on clean tone; level independence (same classes at -20 dB).
  - Envelope: source-filter signal with known formant peaks; envelope >= harmonic peaks within `tolerance_db`; order from f0; low-f0 and high-f0 cases.
  - `target_pitch`: exact pitches, +-49/51 cents, ties, reference 432 Hz, non-finite.
  - `pitch_drag_edit`: offset = target - note target; two drags land on the second; rejects unpitched/multi/inactive/unknown/non-finite/out-of-range; glide and ornament accepted.
- **Contract:** each engine satisfies the `SynthesisEngine` contract (range equals `out_range`, deterministic, thread-safe call from two threads under TSan, slope-1 equals `read_through_map` when the shift is 0, not_implemented for slope != 1). `HarmonicNoiseEngine` and `PsolaEngine` keys change when settings change.
- **Audio scenarios:** the checks listed under "Effect on audio", run through `render()`, with the existing null/undo/untouched scenarios extended to the new engines.
- **Golden:** re-run `vocadito_report`; the Phase 3 numbers must not change (analysis stages only add tracks). New: partial-fit residual share on the 35 tuning tracks (informational, becomes a guard once measured).

## Open questions
1. Owner research on consonant types and vibrato (see top): feeds `shifted_classes`, the fade table and any vibrato option. Defaults above until then.
2. Offset limit `k_max_pitch_offset{2400.0}` (a `Cents`` (R3): owner accepted as a starting gate (2026-10-20) and wants it easy to change. It is one named constant in `core/types/edit.h`, used by the fold clamp, the `param_info` range and `pitch_drag_edit`; nothing else hard-codes 2400. It limits the *edit value*, not the synthesis: the engine does not fail at the limit, it renders whatever offset reaches it (quality at large shifts is measured in the listening package).
3. Should the engine pick (listening gate #1) be per-note automatic (PSOLA for small clean shifts, harmonic-noise otherwise) or one default? Left to the gate; the interface allows both.
4. Streaming/partial render (Phase 9) is not designed here; the engine takes the whole blend zone as ADR-0006 says.

## Review log
Round 1 (structure-reviewer, 2026-10-20): CHANGES REQUIRED, 7 blocking and 8 should-fix. All resolved in the section below, which **overrides the sections above where they differ**. Round 2 (2026-10-20): one blocking finding (R4 margin), fixed in step 6 above and R16; the rest folded into the body.

## Review resolutions (round 1)
**Blocking**
- **R1 Return type.** `pitch_drag_edit` returns `Result<EditSpec>` (origin `"main_tool"`, category `pitch`, param `center_offset`, `Cents`); it calls `validate_for_add`. `EditList` fills seq/group/time.
- **R2 Baseline and repeated drags.** The tool emits `op = set` with value `drag.target - note.pitch_target.target` (offset from the analysed target, so repeated drags are idempotent and land on the last target). The `AnalysisResult` lookup rejects an unknown or inactive note (no `EffectiveNote` parameter). Per kind: `whole`, `settled`, `glide` (end target moves, start follows by the same offset), `ornament` (both pitches move by the offset) are allowed; `multi` and `unpitched` return `invalid_argument` ("no single target", UI suggests a split). Tests: two drags land on the second target; one per kind.
- **R3 Offset limit.** There is no `k_max_pitch_offset`. Phase 4 defines `k_max_pitch_offset{2400.0}` (a `Cents`` in `core/types/edit.h` as a **limit on the folded `EffectiveNote::center - pitch_center`** (set and add alike), enforced by a new `clamp_to(center_offset, -k, +k)` in `finish()` of `effective_state.cpp` (to be added with a unit test) and by the `param_info` set range being ±k so `validate_for_add` rejects a `set` outside it, and by `pitch_drag_edit` returning `out_of_range`. `PsolaParams::max_shift_cents` (700) is the only engine-specific limit; the harmonic-noise engine accepts everything the clamp allows. ADR-0015 is reserved for Phase 6, so the constant is recorded in **ADR-0016 (Phase 4 pitch edit)** with the other Phase 4 deviations (R10, R11). Owner to confirm 2400 (open question 2).
- **R4 Fades.** The renderer (`blend_plan`, ADR-0006) owns the crossfade between edited and original audio at the note's span edges. Engines return the shifted signal over the **whole blend zone** (shifted also in the margin around the note, continuous with the edited run) and fade only at **internal** boundaries: the `attack_end` boundary and the edges of frame runs not in `shifted_classes`. Parameters renamed `class_edge_fade_s` and `consonant_fade_s` (`Seconds`).
- **R5 Fit quality.** No per-frame fit-quality field is added. A frame counts as "poorly fitted" when its harmonic amplitudes are all zero (the sieve found none) or the frame is not in `shifted_classes`; such frames are passed through. Residual-share is an informational golden metric computed by the test from source and tracks.
- **R6 Channels.** Phase 4 engines support **mono sources only** (`src.channels() == 1`); other layouts return `not_implemented`. Test added. Stereo is carried to a later phase (per-channel residual).
- **R7 Supported edit subset.** The engines realise only `eff.center` (and `onset_shift` through the time map). Any non-neutral `mod_scale`, `drift_scale`, `transition_*`, `formant_offset`, `gain`, `fade_*`, `noise_gain`, `within_note_flatten`, `length_scale`, `handles`, `attack_time_scale` returns `not_implemented`. A note with an empty `harmonics` track or `is_active == false` returns `not_implemented` (the renderer already leaves unedited notes alone). Contract tests for each.

**Should-fix**
- **R8** `AnalysisResult::version` must be bumped by callers and golden/fingerprint tests re-baselined, since Notes now carry three filled tracks. Engine CMake targets use `-ffp-contract=off` like `core/render`, and the residual uses the same float oscillator bank in both directions.
- **R9** A shift within `k_neutral_snap_offset` returns the original samples exactly (`read_through_map`), so a gain-only edit never reaches the additive path. Test with a cancelled edit.
- **R10** The envelope re-read `a_k' = a_k * E(f_k')/E(f_k)` deviates from spec §5.3 (`env'(t,k*f0')`) because it keeps the measured fit error. Recorded in ADR-0016. If `f'` is outside `freqs`, E is held at the edge value; frames with all amps 0 are passed through.
- **R11** `snap_chromatic` keeps the spec name through `Result<Cents> target_pitch(Cents pitch, const ChromaticGrid& grid)` (no variant until a second grid exists, Phase 6) (`ChromaticGrid{Hz a4_reference = k_a4_standard_hz}`, `to_hz/to_cents` reused, NaN/inf/<=0 reference -> `invalid_argument`, ties round toward +infinity on the absolute-cents axis). Recorded in ADR-0016.
- **R12** `ChromaticGrid` lives in `core/tuning`; `PitchDrag` in `tools/main_tool`, which links only `core/types` (stage 7: the tool does not call `target_pitch`).
- **R13** Analysis signatures take narrow inputs, not a half-built `Note`: `NoteCurves{FrameGrid grid; Cents pitch_center; span pitch_drift; span pitch_mod}` (built from `NoteSpan`/Note fields by `analyse()`). `mono` starts at SourceTime 0 and sample index = round(t * rate) as in `loudness`; a test with an offset note. `EnvelopeParams::order_scale` is dimensionless against `max_f0` in `Hz`.
- **R14** Parameter fields carry units: `sibilant_centroid` (`Hz`), `silence_level` (`Decibels`), `tolerance` (`Decibels`), `class_edge_fade_s`/`consonant_fade_s` (`Seconds`); `window_periods`, `fft_pad_factor`, `breath_aperiodicity`, `plosive_flux` documented dimensionless; `psola_max_shift` becomes `PsolaParams::max_shift_cents`.
- **R15 Tests added:** shifts beyond Nyquist cap and large downshifts; `onset_shift` with a slope-1 map; blend zone clamped at the recording edges; edit on a note with all-zero harmonics; render-key changes when any engine setting changes; `snap_chromatic` NaN/empty; `scripts/check_includes.py` on the new targets.
- **Notes:** `NoiseTrack::band_energy_linear` is kept for Phase 6 noise gains (not used by Phase 4 synthesis). The default-engine selection hook is the single `SynthesisEngine` the caller passes to `render()`; per-note selection is a post-gate decision. ADR-0013 item 6 is amended in the same commit as ADR-0016.
- **R16 (round 2) Margin.** Option (b): see body step 6.
- **R17 (round 2) Version and mapping.** `AnalysisResult::version` and `AnalyseParams::version` both exist (Phase 3); the caller bumps the latter on re-analysis and `analyse()` copies it. Phase 3 golden numbers for the existing tracks are unchanged. Output-to-source mapping in the engine: slope-1 spans only, the time map is authoritative: output to source uses its inverse over the slope-1 spans; a zone crossing a non-slope-1 span returns `not_implemented` (test); a test with a shifted note.

## Addendum: owner research `vocal-resynthesis-research.md` (2026-10-20)
The owner's research (`docs/design/vocal-resynthesis-research.md`) routes every 2-5 ms frame to a path by sound class. It confirms the Phase 4 structure and fills the "waiting on research" parameters. Every threshold in it is a starting value to calibrate per singer, so they are provisional parameters for the later fuzzy layer, not constants.

| Research path | Phase 4 handling | Where |
|---|---|---|
| Plosive burst (flux peak after closure dip, 5-20 ms) | Protected: original samples, no shift; 2-5 ms crossfade into the vowel, aligned to a pitch period | `shifted_classes` excludes `plosive`; `consonant_fade_s` default 12 ms -> **4 ms** inside this protected-window rule |
| Fricative/sibilant (unvoiced, centroid > ~4 kHz) | Bypass: original samples | `shifted_classes` excludes `sibilant` |
| Breath/other noise (unvoiced) | Bypass | excludes `breath`, `silence` |
| Rough/fry/growl (octave-ratio flag 1.74-2.3 or high SHR) | Not shifted in Phase 4 (texture kept; the harmonic comb must not be forced on a subharmonic voice); frames flagged `rough` pass through | new class, see below |
| Breathy phonation (MVF < 2 kHz or HNR < 10 dB) | Harmonics below MVF shifted, noise above stays pinned to the original envelope (already how the engine treats the residual) | MVF becomes a per-frame field |
| Modal vowel | Shifted | default |

Consequences for this phase (tell me if you disagree before stage 2):
1. **`FrameClass` needs two more members**, `breathy` and `rough` (it is `voiced, sibilant, breath, plosive, silence`). That is a public-type change in `core/types/tracks.h`: it needs an ADR (ADR-0017) and the `NoiseTrack` JSON/validation to follow. Until then the classifier writes `voiced` for breathy and rough frames and the engine shifts them, which is the only behaviour available with five classes.
2. **Voiced consonants** (m, n, l, v, z, r): the research treats voiced fricatives as mixed excitation (harmonics below MVF, noise above), which is exactly the engine's residual rule; no separate class.
3. **Metrics for stage 2** (`split_harmonic_noise`): positive spectral flux with the closure-dip test, spectral centroid, ZCR, HNR from autocorrelation at the pitch lag, band aperiodicity / MVF, then (optional, later) CPPS, GNE, H1-H2, jitter, shimmer, SHR, two-tracker octave ratio. Stage 2 builds the first four plus MVF and the octave-ratio flag from the pitch track's `alternates`; the rest are carried until a test shows they are needed.
4. **Routing order** is the research's: burst, fricative, breath, rough, breathy, modal; labels median-filtered over 3-5 frames, minimum segment 20 ms except bursts, class boundaries crossfaded 2-5 ms. `class_edge_fade_s` default 8 ms -> **4 ms**.
5. **Calibration** (round-trip null-edit and +-2 semitone tests per class, tolerances in the research's last table, e.g. plosive onset within 2 ms, centroid within 5 %, modal F0 under 10 cents) become the objective checks of stages 5-9; they are the research's guesses, so they are recorded as provisional and never loosened without asking.
6. Neural vocoders (h-NSF and similar) stay Phase 13. Not built here.

- **Stage 1 result (2026-10-20):** `analyse_partials` implemented with a Blackman-Harris window (Hann leaked 3.8 cent at four periods); `window_periods` is 4..8; the `slope_correction` option was dropped because the vibrato and glide gates pass without it (see `src/core/analysis/partials/README.md`). Real-voice residual is measured at stage 4.
- **Stage 2 result (2026-10-20):** `analysis/harmonic_noise` implemented (`measure_frames`, `classify_frames`, `split_harmonic_noise`), 10 unit tests on synthetic scenes (vowel, sibilant, breath, plosive after a closure, silence, breathy, rough, level independence, smoothing). Deviations from the first sketch: `NoteCurves` moved to `core/types/note_curves.h` so the analysis stages do not depend on each other; the function also takes a `PitchEvidence` (voicing probability and the second-best f0, because Phase 3 interpolates the curves through unvoiced gaps and they no longer say which frames are voiced); the parameter set follows ADR-0017 (breathy HNR and MVF, burst thresholds, octave ratio, smoothing); `band_significance` default is 30 dB, not the research's implied value, because Hann leakage tails read as aperiodic at 50 dB. Real-voice behaviour is measured at stage 4.
- **Stage 3 result (2026-10-20):** `analysis/envelope` implemented (`estimate_envelope`, True Envelope on the harmonic peaks), 5 unit tests. `n_bins` default is 513 (43 Hz): at 257 the linear lookup between grid points sat up to 2 dB under a sharp formant peak. `EnvelopeParams::floor` (Decibels) is new: the level of a frame with no harmonic. No time smoothing yet.
- **Stage 4 result (2026-10-20):** `analyse()` fills `Note::harmonics`, `noise` and `envelope` for every note (`AnalyseParams::{partials, harmonic_noise, envelope}`); the pitch track's voicing and second-best path are passed on the note's frames; the CLI JSON gains `harmonic_count` and `sound_classes` (runs). Phase 3 metrics are unchanged on the 35 tuning tracks (1671 notes, onset F20 0.582, note pitch 0.853, APC target 0.655) and on the 5 held-out tracks (0.552 / 0.803 / recall 0.997). On the real Vocadito corpus (86,096 note frames): voiced 81.0 %, breathy 18.7 %, sibilant 0.1 %, breath 0.2 %, plosive 0, silence 0, rough 0; the voiced frames carry 98.9 % of their power in the measured harmonics. Findings: (1) rumble below the voice made 10 % of clean frames breathy; fixed (bands under 0.75 of the first harmonic are ignored for MVF and HNR, with a unit test). (2) 18.7 % breathy remains and the cause is the MVF rule (below 2 kHz), not HNR; it is an open calibration item for Phase 8 and the listening gate (Phase 4 shifts breathy frames anyway, so nothing is bypassed by this). (3) rough is never set with the c4dm tracker, which gives no second path; the second tracker is Phase 13. (4) plosive 0 because notes begin at the vowel; consonant bursts are mostly outside note spans (consonant preroll aside).

- **Stage 5 result (2026-10-21), harmonic-noise engine** (`synthesis/harmonic_noise`, 15 unit tests). Structure review of the header (CHANGES REQUIRED, B1-B4, S1-S9) resolved: breathy shifts all harmonics (ADR-0017 amendment); both margin branches documented and tested (held model when the edge frame is shifted, original samples plus an inner fade when it is not); a frame with no measured harmonic is passed through; `w == 0` copies the original bits (a -0.0 survives); `FrameClassSet` replaces the vector; params validated (`invalid_argument`); taper fields renamed `taper_start_of_rate`/`taper_end_of_rate` and apply to the shifted harmonics only (a smooth taper replaces the hard 0.45 cut); `settings_key` is a hex-pattern layout; the fade is equal-power (sine/cosine pair); `consonant_fade_s` dropped (ADR-0017 amendment). Also changed in `analyse_partials`: amplitude and phase now come from projecting the frame on the QIFFT frequency (no interpolation bias). Pitch round trip is gated as AUDIO_VERIFICATION says: the note's re-detected pitch (median) within 3 cents; the worst single frame is 3.2-4 cents because the original partial is cancelled to about 1 % and beats against the shifted one (bounded at 6). The PSOLA engine (stage 6) is next.

## Stages 8 and 9: CLI engines, analysed scenarios, audition package, objective checks (2026-10-21)

**Stage 8, CLI.**
- `EngineChoice` gains `harmonic_noise` and `psola`; `parse_engine` accepts `passthrough|harmonic_noise|psola`; `anno-cli render|audition ... --engine <name>`. The engine objects are built with default params; `cli/commands` links `core/synthesis/{harmonic_noise,psola}` and `tools/main_tool` (layer 7 may link everything).
- A scenario can ask for a real analysis: `"analysis": "analyse"` (default `"given"`, the Phase 2 hand-made notes). With it, `load_run` runs `analyse(source, AnalyseParams{.track = scenario_track()})` on the source. `notes` must be absent in that mode. As built (review S1/S2): in analyse mode only `tool` edits address notes by `note_index` (sorted as in `AnalysisResult::notes`); a raw `category/op/param` edit may use `note_id` only; an `edits` array is all tool edits or all raw edits (a mix would be applied in a different order than written, so it is a parse error); an index past the analysed notes is `invalid_argument` from `load_run`, naming the index.
- A new edit form drives the Main Tool: `{"tool":"pitch_drag","target":{"note_index":i},"offset_cents":N}` or `"target_cents":N` (absolute, A4 = 6900; exactly one). It is resolved at load time with `pitch_drag_edit` (relative: target = the note's analysed target + offset), so scenarios exercise the same code a UI will. `"snap": "chromatic"` applies `target_pitch` to the final target. The raw `category/op/param` form stays.
- Engine refusal: `run_audition` returns the engine's `not_implemented` error and writes no package; `scripts/phase4_audition.py` records it as `REFUSED.txt` (any other failure is `FAILED.txt`) and lists it in `INDEX.md`. `expect.engines` keeps PSOLA off the +-2400 scenarios, so the refusal path is exercised by `cli_engines_test`; the +-700 scenarios sit at PSOLA's exact limit and render. The `phase4-pitch` package renders the same scenarios through `harmonic_noise` and `psola` into `audition/phase4-pitch/<scenario>/<engine>/` plus an `INDEX.md` that lists, per scenario, both engines side by side and what to listen for (AUDIO_VERIFICATION §4). Scenarios: +-50, +-200, +-700 cents and +-2400 (harmonic_noise only, PSOLA refuses above 700) on a synthetic vowel with vibrato; vowel + sibilant + breath; two Vocadito excerpts.
- The audition report adds, for pitch edits: the requested offset, the measured median pitch offset of the edited note (re-analysed), vibrato extent before/after, and the sibilant centroid before/after, all as numbers; never a judgement.

**Stage 9, objective checks** (`tests/audio/pitch_engine_checks.cpp`, label `audio`, run for both engines through `render_region` with a real `analyse()` result). Each check uses `tests/tolerances.h` only:
null, undo-null, untouched (bit-exact outside the edited note's blend zone), pitch round trip (`k_pitch_roundtrip`, median over the note, re-analysed with `analyse()`; the worst frame is printed), vibrato rate/extent (`k_vibrato_rate`, `k_vibrato_extent_rel`), formant preservation (`k_formant_preservation_rel`, +-700 cents), noise not shifted (`k_noise_centroid_rel`, harmonic_noise only: PSOLA moves noise by design and the check is reported, not asserted, for it), click detector (`k_click_detections` on the whole render and at the edit's note edges), sanity, determinism, length (`k_length`). The engine unit tests keep their own tolerances but are moved onto the `tolerance::` constants where one exists. A failing check is fixed in the code, never by changing a tolerance; PSOLA checks that fail are recorded as engine limits in the listening sheet (the engine is the comparison one) and the result is reported to the owner, not hidden.

**Stage 8 result (2026-10-21).** Built as above, with the review fixes (S1-S10). Package `audition/phase4-pitch` (git-ignored, regenerate with `python3 scripts/phase4_audition.py`): 13 scenarios, 24 scenario/engine pairs, run from a release build. Sources are synthetic vowels (`scripts/make_phase4_sources.py`) and Vocadito track 2. Finding that changed a fixture, not a tolerance: with all-zero harmonic phases the synthetic voice is a pulse train and the click detector fired on the source itself (478 detections) and on the shifted render (45 "new"); random harmonic phases (seeded) remove that and the detector then reports 0 new detections on every pair. One pair still fails an objective check: **PSOLA at -700 cents on `vowel_vibrato` has DC -58.5 dBFS against the -60 dBFS limit** (the same scenario at -200 gives -117.7 dBFS and at +700 -67.1). This is an engine limit to be recorded for gate #1 (large down-shifts leave gaps in the grain overlap); the tolerance is not changed. `docs/AUDIO_VERIFICATION.md` section 2 is a spec: its example uses `"param": "center_offset_cents"` (the parser takes `center_offset`) and `note_index` in a raw edit; the new `analysis`, `tool` and `expect.engines` forms are not in it. Left for the owner to approve a spec edit; recorded here as an ADR-0016 amendment (below).

**Stage 9 result (2026-10-21), objective checks.** `cli/checks/pitch_checks` (median pitch shift, vibrato from `pitch_mod`, first formant by parabolic envelope peak, spectral centroid) feeds both the audition report (rows per edited note of an analysed scenario) and `tests/audio/pitch_engine_checks.cpp` (limits from `tests/tolerances.h` via `PitchCheckParams`, pinned by `pitch_checks_test`). The `audio-verifier` ran `ctest -L audio` (13/13 pass) and the 24-pair package (release). Synthetic vowels: `harmonic_noise` passes every check on all 8 pairs (largest round-trip deviation 0.37 cents at +2400; null, untouched, determinism, length, clicks, sanity all pass on all 24 pairs). PSOLA passes pitch round trip on all pairs and fails: first formant at -200 (-18.0 %), -700 (+11.0 %) and -200 on the sibilant vowel (-8.5 %); vibrato rate at +700 (5.52 -> 5.19 Hz, 0.33 Hz against 0.2). Those are recorded as PSOLA limits for gate #1 in a `[!mayfail]` test; no tolerance was changed. Not measurable: the -2400 round trip (55 Hz is below the tracker). Real-voice scenarios (`vocadito2_*`) show round-trip, vibrato and formant FAILs that are measurement limits (jittery 10 Hz 'vibrato' in `pitch_mod`, 10-cent tracker grid, formant peak pick landing on low harmonics); HN +700 on the real voice measures +692.08 cents (7.9 off), probably the tracker's grid, unverified. Open: a steady-vibrato real voice to test the vibrato check, a better first-formant measure on real voices, and the pitch-tracker range for -2400. Fix found by the checks: PSOLA left a DC offset (-58.5 dBFS at -700, limit -60) from down-shift grain gaps; the output now has its one-period local mean removed. The first version of that fix only reached samples a grain covered at window >= 0.5 and left -68.1 dBFS; the unit test `a_large_down_shift_leaves_no_dc_offset` (random-phase voice, -700 cents) caught it at -53.8 dBFS, and with every grain-covered sample corrected the audition scenario is -135 dBFS.

**Stage 9 review resolutions (2026-10-21).** Structure review: CHANGES REQUIRED (B1-B3, S1-S9). Done: (B3) a formant found in the source but not in the render is a FAIL row; (S7) rows carry an `AuditionCheck` kind and tests select by kind; (S4) the DC fix has a direct unit test, which found the half-fix above; (S2) the scenario README and the report say `expect` is echoed, not checked; (S6) stale statements: the audition report does not print the sibilant centroid (only the test does, for both engines, for a sibilant outside the edited note, so it shows a neighbouring consonant is left alone, not that noise inside a note is: that needs a note-contained sibilant scenario, open); undo-null and note-edge click checks for the pitch engines are not written yet (open; the Phase 2 null/undo tests use the gain engine); the checks run through `run_audition`, not `render_region` directly; "13/13" meant the ctest labels passed, with the PSOLA case then `[!mayfail]`. (B1) The `[!mayfail]` case is replaced: the strict check is a hidden case `[psola_limits]` that fails (run `pitch_engine_checks "[psola_limits]"`), and a visible case pins the recorded deviations (formant <= 20 %, vibrato rate within 0.4 Hz of 5.52) so they cannot worsen unnoticed. **The PSOLA formant/vibrato failures are not accepted; they need the owner's decision (fix PSOLA, or an ADR accepting them as engine limits).** The formant drift may partly be a measurement effect (sparse harmonics against a 150 Hz wide formant); not yet separated. (B2) The real-voice FAIL rows are NOT shown to be measurement limits: that is an assumption. The +692.08 cents of `harmonic_noise` at +700 on `vocadito2_note2` is open; a grid-independent measure (`analyse_partials` on the render) or a synthetic source through the same tracker would settle it. Open, not done: S1 (record the differential pitch measure and the single-peak formant measure as the definitions in an ADR; they differ from AUDIO_VERIFICATION wording), S3 (relative centroid tolerance, scenario-derived constants), S5 (the DC removal is unconditional; a test that content below f0/2 is not attenuated), S8 (a report-only row status so unmeasurable real-voice rows do not fail the package), S9 (header polish and more unit tests for pitch_checks).

**Stage 9b (2026-10-21): PSOLA hiccups, marks by waveform match.** Owner listening: PSOLA sounded better than harmonic_noise on every file heard (harmonic_noise added distortion), but with audible hiccups; the large vibrato-rate errors on real voices (for example 5.98 -> 20.95 Hz) disappeared or shrank after the mark change below, so they are attributed, by inference, to the same irregularities. Measure: `scripts/phase4_cycle_jump.py` (cycle dissimilarity per 5 ms over the samples where the render differs from the source, 60 ms trimmed at each end; reproduce with `python3 scripts/phase4_cycle_jump.py psola audition/phase4-real`, the package is git-ignored; values below are from one run on 2026-10-21, the 'before' from `audition/phase4-real` and the 'after' from `audition/phase4-real-v2`). The measure takes the period from a 70..700 Hz autocorrelation, so a wrong period estimate can inflate a single hop; the high 'before' values were not individually checked against what the owner heard, only the overall impression and the vanishing of the spikes agree. Before: the render's maximum was 0.12-1.14 against the source's 0.01-0.15 (for example vocadito37_note43_up700 1.14 against 0.03, vocadito28_note16_down200 0.58 against 0.06); the median stayed near the source's, which suggests isolated events (event duration was not measured). Cause: the analysis marks snapped to the tallest sample within +-0.2 period, which jumps between waveform features on real voices. Change: each mark is placed where the waveform best matches the previous mark's (normalised correlation over one period, +-0.2 period search), refined to a fraction of a sample by a parabola (the whole-sample version moved the synthetic pitch round trip to 4.5 cents). After, 12 scenarios (tracks 13, 28, 37, 4; notes 59, 29/16, 43, 21; shifts +200, -200, +700): the render's maximum is at or below the source's on 11 of 12 (for example 0.02 against 0.03, 0.03 against 0.04); the exception is vocadito4_note21_up700 (0.09 against 0.08). Vibrato rate rows: several now pass (5.52 -> 5.49 Hz), the rest are within about 1 Hz (6.52 -> 5.43, 7.61 -> 6.59); not yet explained (short notes or the measure). Unit tests: `psola_engine_test` 19/20; the failing one is `psola_moves_the_noise_inside_a_shifted_frame_and_keeps_the_timing` (level change 1.94 dB, bound 1.5 dB): overlap-adding independent noise grains adds them incoherently, the noise drop the owner heard on noisy files; open (ideas: period-synchronous separation of the periodic part, level compensation, PSOLA on the periodic part only; EMD was considered and not recommended: it separates by time scale, so a harmonic voice spreads over several components). `psola_keeps_the_first_formant_where_it_was` uses a 110 Hz source (dense harmonics) with the unchanged 5 % bound; with 165 Hz the harmonic spacing after +700 cents (247 Hz) alone put the estimate 5.9 % off for any engine. The research behind this: `psola-vibrato-hiccups.md` (grain repeat/skip hiccups, recipes; our time map already is its recipe 1) and `pitch-marks-integration.md` (ADR-0019).

**Stage 9c (2026-10-21): the noise bed, period-synchronous separation.** Owner listening: the background noise dropped during shifts on noisy files. Measurement on real voices (power above 8 kHz in the edited span, render against source): plain PSOLA lost 1.2-5.1 dB (for example vocadito28_note29_up700 -5.0 dB); with the separation (4 cycles) below 0.3-0.9 dB on the five scenarios of the sweep except 3.2 dB on vocadito37_note43_up700 (that track's content above 8 kHz is probably harmonics, not checked). Cause: overlap-adding unrelated noise grains adds them incoherently. Change: `PsolaEngineParams::separation_cycles` (default 4, 0 = off, 0..8). Per shifted run the periodic part is the average of the same point of up to `n` aligned cycles on each side (Hann weights, further weighted by the cycle's energy relative to the centre cycle so attacks and decays do not smear; the point is aligned by the waveform-matched marks, offsets scaled by the ratio of local periods). Only the periodic part is re-spaced; the remainder (source minus periodic part) stays in place, with each neighbouring cycle scaled to the centre cycle's level before averaging, so the average keeps the local level through a decay (without this a fading note left a step of 0.002 at its end, which the click detector found in vowel_vibrato_down700), scaled by `remainder_gain(n)` = sqrt((1 - b) / (1 - 2a + b)) (a = w0 / sum w, b = sum w^2 / (sum w)^2) so that remainder plus re-spaced noise leaves the power of stationary white noise unchanged. EMD was considered and not used (it separates by time scale, not periodic against aperiodic). Settings key `v2`, engine version 2.

Findings that matter for the decision: (1) the old test `psola_moves_the_noise_inside_a_shifted_frame_and_keeps_the_timing` measured the whole-signal rms, which is the voice (25 dB SNR); the 1.9 dB it flagged was the voice level, which swings between -0.6 and -2.5 dB with tiny noise differences (SNR 70 to 45 dB) and is -3 dB at +700 cents even without noise. The likely reason (a hypothesis, not tested): adjacent grains of a periodic signal carry harmonics in different phases when the output spacing is not a multiple of the period, so they partly cancel in the overlap, more so for higher harmonics and larger shifts. The test is replaced by `psola_keeps_the_noise_level_and_the_timing_inside_a_shifted_frame`, which measures the second difference (above about 8 kHz only noise; test voices end at 2.6 kHz) with the same 1.5 dB bound, and checks that without the separation the loss is more than 1 dB larger. (2) A cost: the cycle-jump measure rises with `n` at +700 cents (vocadito13_note59_up700 p90 0.055 plain, 0.085 / 0.100 / 0.128 at 1 / 2 / 4 cycles, source 0.051; max 0.14 / 0.24 / 0.28 / 0.33 against 0.15; vocadito4_note21_up700 p90 0.035 / 0.045 / 0.052 / 0.066, source 0.033). Noise above 8 kHz for the same scenario, plain / 1 / 2 / 4 cycles: -3.9 / -1.4 / -0.9 / -0.5 dB. The measure cannot separate hiccups from non-periodicity that is correct (the noise bed is not periodic) and from original-pitch harmonics the average did not capture, which would stay behind as a second voice. Which `n` to use is a listening decision: `audition/phase4-separation/` has plain, 1, 2 and 4 cycles for five scenarios. Default stays 4 until then. Not yet done: a test that separation does not remove or smear content that repeats only slowly (vibrato depth, amplitude changes), clang-tidy on the final files.

