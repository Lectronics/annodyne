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
    double window_periods = 4.0;      // f0-adaptive analysis window length in periods, 3..8
    double fft_pad_factor = 5.0;      // zero-padding for QIFFT, >= 4 (spec B5)
    bool slope_correction = true;     // Röbel frequency-slope bias correction
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
- `tools/main_tool`: `core/types`, `core/edits`, `core/tuning` (layer-5 allowlist). It does not link `dsp`, `synthesis` or `render`.
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
- **R12** `ChromaticGrid` lives in `core/tuning`; `PitchDrag` in `tools/main_tool`, which links exactly `core/types` and `core/tuning`.
- **R13** Analysis signatures take narrow inputs, not a half-built `Note`: `NoteCurves{FrameGrid grid; Cents pitch_center; span pitch_drift; span pitch_mod}` (built from `NoteSpan`/Note fields by `analyse()`). `mono` starts at SourceTime 0 and sample index = round(t * rate) as in `loudness`; a test with an offset note. `EnvelopeParams::order_scale` is dimensionless against `max_f0` in `Hz`.
- **R14** Parameter fields carry units: `sibilant_centroid` (`Hz`), `silence_level` (`Decibels`), `tolerance` (`Decibels`), `class_edge_fade_s`/`consonant_fade_s` (`Seconds`); `window_periods`, `fft_pad_factor`, `breath_aperiodicity`, `plosive_flux` documented dimensionless; `psola_max_shift` becomes `PsolaParams::max_shift_cents`.
- **R15 Tests added:** shifts beyond Nyquist cap and large downshifts; `onset_shift` with a slope-1 map; blend zone clamped at the recording edges; edit on a note with all-zero harmonics; render-key changes when any engine setting changes; `snap_chromatic` NaN/empty; `scripts/check_includes.py` on the new targets.
- **Notes:** `NoiseTrack::band_energy_linear` is kept for Phase 6 noise gains (not used by Phase 4 synthesis). The default-engine selection hook is the single `SynthesisEngine` the caller passes to `render()`; per-note selection is a post-gate decision. ADR-0013 item 6 is amended in the same commit as ADR-0016.
- **R16 (round 2) Margin.** Option (b): see body step 6.
- **R17 (round 2) Version and mapping.** `AnalysisResult::version` and `AnalyseParams::version` both exist (Phase 3); the caller bumps the latter on re-analysis and `analyse()` copies it. Phase 3 golden numbers for the existing tracks are unchanged. Output-to-source mapping in the engine: slope-1 spans only, the time map is authoritative: output to source uses its inverse over the slope-1 spans; a zone crossing a non-slope-1 span returns `not_implemented` (test); a test with a shifted note.
