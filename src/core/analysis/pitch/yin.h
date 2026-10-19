#pragma once

#include "core/dsp/fft.h"
#include "core/types/result.h"

#include <span>
#include <vector>

// The YIN difference function and its normalisations for one frame (de Cheveigne & Kawahara 2002).
// Internal to core/analysis/pitch (used by pYIN and its tests).

namespace anno {

/// Per-frame YIN analyser for a fixed window and maximum lag. The difference function is computed
/// with FFT autocorrelation. Thread-safe (const), not real-time safe.
class YinFrame {
public:
    /// Errors: invalid_argument if window < 2 or max_lag < 2.
    [[nodiscard]] static Result<YinFrame> create(int window, int max_lag);

    [[nodiscard]] int window() const noexcept { return window_; }
    [[nodiscard]] int max_lag() const noexcept { return max_lag_; }
    /// Number of samples analyse() reads: window + max_lag.
    [[nodiscard]] int segment_length() const noexcept { return window_ + max_lag_; }

    /// `segment` has segment_length() samples; the window is its first `window` samples and lag tau
    /// compares it with the samples tau later. Fills (each resized to max_lag + 1, index = lag):
    ///  cmnd[tau]: cumulative-mean-normalised difference d'(tau) (d'(0) = 1); ~0 at a period of a
    ///             periodic signal, ~1 for noise; 1 for a silent segment;
    ///  nccf[tau]: normalised cross-correlation 2 c(tau) / (e(0) + e(tau)) in [-1, 1], 1 at a
    ///  period.
    void analyse(std::span<const double> segment, std::vector<double>& cmnd,
                 std::vector<double>& nccf) const;

private:
    YinFrame(int window, int max_lag, RealFft fft) noexcept;

    int window_;
    int max_lag_;
    RealFft fft_;
};

}  // namespace anno
