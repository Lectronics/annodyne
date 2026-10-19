#pragma once

#include <vector>

// Windows for STFT and filter design (design: docs/design/phase3-dsp.md).

namespace anno {

enum class WindowKind { hann_periodic, blackman_harris };

/// Window of `length` samples, periodic form (N in the cosine denominator) so hann satisfies COLA
/// at hop = length/2 and length/4 (Harris 1978 for blackman_harris, 4-term). Pre: length >= 1; a
/// length-1 window is {1.0}. Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<double> make_window(WindowKind kind, int length);

}  // namespace anno
