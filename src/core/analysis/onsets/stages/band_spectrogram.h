#pragma once

#include "core/types/result.h"
#include "core/types/units.h"

#include <cstdint>
#include <span>
#include <vector>

// Stage P1: STFT magnitudes reduced to log-spaced bands.
// Stage conventions (all stages): STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a
// sine of amplitude A has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a
// unit-sum weighted MEAN of magnitudes. The activation is the MEAN over bands of the positive log
// difference (natural log). Every function is pure, thread-safe and not real-time safe (it
// allocates); inputs are never written. Times are on the source axis, frame i centred at i * hop
// from the start of the analysed signal. Design: docs/design/phase5-transient-pipeline.md.

namespace anno {

/// Frames x bands of non-negative values, frame-major.
struct BandSpectrogram {
    Seconds hop;                // the ACTUAL hop
    std::int64_t n_frames = 0;  // 0 when the signal is shorter than the window
    std::int64_t n_bands = 0;
    std::vector<Hz> centres;   // strictly increasing, one per band
    std::vector<double> data;  // n_frames * n_bands
};

/// Errors: invalid_argument if the counts are negative, centres.size() != n_bands, data.size() !=
/// n_frames * n_bands, the centres are not strictly increasing, or a value is negative or not
/// finite.
[[nodiscard]] Result<void> validate(const BandSpectrogram& spec);

struct BandParams {
    Seconds window{
        0.043};          // Hann window; n_fft = the power of two nearest to window * rate (>= 256)
    Seconds hop{0.005};  // rounded to whole samples; the result carries the actual hop
    int bands_per_octave = 24;  // log-spaced triangular filters, unit sum, centres on distinct bins
    Hz f_min{30.0};
    Hz f_max{16000.0};  // also limited to 0.45 of the rate; fewer than 4 bands fall back to a
                        // linear split
};

/// STFT magnitudes reduced to log-spaced bands (the Phase 3 / SuperFlux filterbank). A signal
/// shorter than n_fft, or an empty one, gives 0 frames (and the actual hop). Errors:
/// invalid_argument for a non-positive or non-finite window, hop, rate or f_min, f_min >= f_max,
/// bands_per_octave < 1.
[[nodiscard]] Result<BandSpectrogram> band_spectrogram(std::span<const float> mono, Hz rate,
                                                       const BandParams& params);

}  // namespace anno
