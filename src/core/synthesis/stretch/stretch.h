#pragma once

#include "core/types/analysis_result.h"
#include "core/types/audio_buffer.h"
#include "core/types/audio_source.h"
#include "core/types/frame_class_set.h"
#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <cstdint>

// stretch_span: renders a TimeMap warp with slope != 1 from the source, pitch and timbre kept
// (ADR-0023; design: docs/design/phase5-stretch-render.md).

namespace anno {

/// What happens to a vibrato in a stretched run (ADR-0023 amendment 3): `preserve` regenerates it
/// at the source's rate and depth (phase-continuous, the rate within 0.5 / L Hz of the source's for
/// an output run of L seconds that joins slope-1 audio, exactly the source's otherwise); `retime`
/// re-times the contour with the audio, so the rate scales with 1 / slope (the Phase 5 behaviour).
enum class VibratoMode : std::uint8_t { preserve, retime };

/// Which renderer makes a stretched voiced span: this module's TD-PSOLA path (mono only; keeps
/// the vibrato rate, `vibrato`) or Rubber Band R3 (ADR-0026: any channel count, a phase vocoder;
/// the engine renders use, see RenderParams). The other fields below, `vibrato` and `voiced_bridge`
/// included, apply to the PSOLA path only. StretchParams itself defaults to psola so the module's
/// unit tests stay on the path they test; RenderParams overrides it.
enum class StretchEngine : std::uint8_t { psola, rubberband };

struct StretchParams {
    StretchEngine engine = StretchEngine::psola;
    VibratoMode vibrato = VibratoMode::preserve;
    // A stretch of non-voiced frames shorter than this (source time) with voiced frames on both
    // sides stays in the voiced run: a one-frame dropout of the classifier must not switch the
    // renderer to the noise path and back (a step at each switch). >= 0, finite.
    Seconds voiced_bridge{0.015};
    // Equal-power crossfade where voiced frames meet the others; >= 0, finite.
    Seconds class_edge_fade{0.004};
    // Unvoiced grain length (sine window, 50 % overlap); finite, > 0.
    Seconds noise_grain{0.024};
    // Seeded random read-position jitter of the unvoiced grains, as a fraction of a grain hop;
    // 0..0.5.
    double noise_jitter_hop_fraction = 0.25;
    std::uint64_t noise_seed = 0x5eed;
    // A span whose slope is outside [min_slope, max_slope] is not_implemented (the layout never
    // asks for it). 0 < min_slope <= max_slope.
    Ratio min_slope{0.25};
    Ratio max_slope{4.0};
    // Frame classes rendered as periodic (voiced) material; every other class, and every sample
    // outside a note, goes through the noise path. An empty `frame_class` of a note counts as
    // voiced, as in the PSOLA engine.
    FrameClassSet voiced_classes{FrameClass::voiced, FrameClass::breathy};
};

/// Errors: invalid_argument for a field outside the ranges above or not finite.
/// Pure; thread-safe.
[[nodiscard]] Result<void> check_stretch_params(const StretchParams& params);

/// The audio of `out_range` (output axis) of one warp `span`, played back with its slope and the
/// pitch and timbre of the source.
///  - Positions: output sample n (at the source's rate) maps to source position
///    source_start * rate + (n - output_start * rate) / slope; the source is read directly on the
///    source axis (fractional positions, Catmull-Rom; samples outside the recording are zero).
///  - Sample range: the span covers the output samples [round(output_start * rate),
///    round(output_end * rate)), the same snapping read_through_map uses, so a stretched span and
///    its slope-1 neighbours share their rounded boundary (no gap, no overlap). The result has
///    exactly the samples of `out_range` (rounded to nearest the same way) and its start is
///    `out_range.start()`.
///  - Voiced frames (the note frame at the source time has a class in `voiced_classes` and a finite
///    pitch): TD-PSOLA time scaling. Analysis marks follow the note's pitch curve; synthesis marks
///    are one source period apart; each takes the analysis mark nearest its source time. The first
///    and last synthesis mark of a voiced run sit on analysis marks at their offsets from the run's
///    ends (the spacing between them is scaled to fit), so the output is phase-continuous with an
///    adjoining slope-1 copy. Periods
///    are repeated (slope > 1) or skipped (slope < 1); the pitch contour is the source's, re-timed.
///  - Every other frame, and everything outside a note: sine-window grains of `noise_grain` at 50 %
///    overlap, read at the source time of the grain centre plus a seeded jitter (derived from
///    noise_seed, the span's first output sample and the grain index, so the samples of a range do
///    not depend on which range was asked for). Digital silence stays exactly zero.
///  - Voiced and noise paths are joined with an equal-power crossfade of class_edge_fade.
/// Mono only with engine psola (any channel count with rubberband). Errors: invalid_argument (bad
/// params; `span` not a warp; `out_range` outside the span; a note whose frame_class has the wrong
/// size), not_implemented (stereo; slope outside [min_slope, max_slope]). Source samples outside
/// the recording read as zero. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<OutputAudio> stretch_span(const AudioSource& src,
                                               const AnalysisResult& analysis, const TimeSpan& span,
                                               OutputRange out_range, const StretchParams& params);

}  // namespace anno
