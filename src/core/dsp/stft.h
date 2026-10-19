#pragma once

#include "core/dsp/window.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

// STFT / ISTFT (design: docs/design/phase3-dsp.md).

namespace anno {

struct StftParams {
    int n_fft = 2048;
    int hop_samples = 512;
    WindowKind window = WindowKind::hann_periodic;
};

/// Frame i is centred at sample i * hop_samples of the input (the signal is zero-padded by n_fft /
/// 2 at both ends). The hop is a whole number of samples: a caller that wants a hop in seconds
/// rounds it (hop_samples = round(hop * rate)) and uses the ACTUAL hop, hop_samples / rate, for
/// frame times.
struct Spectrogram {
    int n_fft = 0;
    int hop_samples = 0;
    WindowKind window = WindowKind::hann_periodic;  // stored so istft cannot be given another one
    Hz sample_rate;
    std::int64_t n_frames = 0;              // floor(N / hop_samples) + 1 for an N-sample input
    std::int64_t n_bins = 0;                // n_fft / 2 + 1
    std::vector<std::complex<float>> data;  // frame-major: data[frame * n_bins + bin]

    /// Pre: 0 <= frame < n_frames, 0 <= bin < n_bins. Real-time safe.
    [[nodiscard]] std::complex<float> at(std::int64_t frame, std::int64_t bin) const noexcept {
        return data[static_cast<std::size_t>(frame * n_bins + bin)];
    }
};

/// Errors: invalid_argument if n_fft or hop are not positive, n_bins != n_fft / 2 + 1, n_frames <
/// 0, or data.size() != n_frames * n_bins. Pure; thread-safe.
[[nodiscard]] Result<void> validate(const Spectrogram& spec);

/// Errors: invalid_argument for bad params (n_fft < 2 or odd, hop < 1 or > n_fft), a non-finite or
/// non-positive rate, or an empty signal. Pure; thread-safe; not real-time safe. Cost O(N log
/// n_fft).
[[nodiscard]] Result<Spectrogram> stft(std::span<const float> mono, Hz sample_rate,
                                       const StftParams& params);

/// Inverse by weighted overlap-add with window-power normalisation (uses spec.window); returns
/// exactly `length` samples. Reconstructs the input of `stft` to about 1e-5 (float storage) where
/// the window power sum exceeds 1e-3; elsewhere the samples are zero.
/// Errors: invalid_argument if the spectrogram is invalid, length < 0, or length needs more frames
/// than the spectrogram holds. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<std::vector<float>> istft(const Spectrogram& spec, std::int64_t length);

}  // namespace anno
