#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <span>
#include <vector>

// Stage P0: level normalisation (a scaled copy).
// Stage conventions (all stages): STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a
// sine of amplitude A has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a
// unit-sum weighted MEAN of magnitudes. The activation is the MEAN over bands of the positive log
// difference (natural log). Every function is pure, thread-safe and not real-time safe (it
// allocates); inputs are never written. Times are on the source axis, frame i centred at i * hop
// from the start of the analysed signal. Design: docs/design/phase5-transient-pipeline.md.

namespace anno {

enum class LevelNorm { peak, rms_active };

struct LevelNormParams {
    LevelNorm mode = LevelNorm::rms_active;
    Decibels target{-23.0};  // peak mode: the peak in dBFS; rms mode: the RMS of the active frames
    Decibels active_floor{-60.0};  // 20 ms frames more than this below the loudest do not count
    Decibels max_gain{
        40.0};  // the largest gain applied: a very quiet file is not amplified without bound
};

struct NormalisedSignal {
    std::vector<float> samples;  // a scaled COPY
    Decibels gain{0.0};          // what was applied
};

/// A copy of `mono` scaled so a fixed detection threshold means the same at any recording level.
/// rms_active: the RMS is sqrt(mean of the squares) over the 20 ms frames within active_floor of
/// the loudest frame. Silence (or no active frame) is returned unchanged with gain 0 dB. The gain
/// is limited to max_gain (so a quiet file ends below the target). Errors: invalid_argument for a
/// non-finite sample, rate or parameter, or max_gain < 0. The source is never touched.
[[nodiscard]] Result<NormalisedSignal> normalise_level(std::span<const float> mono, Hz rate,
                                                       const LevelNormParams& params);

}  // namespace anno
