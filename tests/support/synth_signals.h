#pragma once

#include "core/types/audio_buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// Synthetic signals with known answers for analysis tests (coding-standards §8). Deterministic: the
// only randomness is a seeded generator. Samples are float, peak well below 1.

namespace anno::test {

/// A sine of `hz` for `seconds` at `rate_hz`, amplitude `amplitude` (linear).
inline std::vector<float> make_sine(double hz, double seconds, double rate_hz,
                                    double amplitude = 0.5) {
    const auto n = static_cast<std::size_t>(std::llround(seconds * rate_hz));
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate_hz));
    }
    return x;
}

/// Wraps a mono vector as an AudioBuffer with `channels` identical channels.
inline AudioBuffer to_buffer(const std::vector<float>& mono, double rate_hz, int channels = 1) {
    AudioBuffer buffer =
        AudioBuffer::create(channels, Samples{static_cast<std::int64_t>(mono.size())}, Hz{rate_hz})
            .value();
    for (int c = 0; c < channels; ++c) {
        std::ranges::copy(mono, buffer.channel(c).begin());
    }
    return buffer;
}

/// Root mean square of a signal (linear).
inline double rms(const std::vector<float>& x, std::size_t first = 0, std::size_t count = 0) {
    const std::size_t n = count == 0 ? x.size() - first : count;
    double acc = 0.0;
    for (std::size_t i = first; i < first + n; ++i) {
        acc += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(acc / static_cast<double>(n));
}

/// Frequency of a (near-)sinusoid from its positive-going zero crossings over the whole signal.
inline double zero_crossing_hz(const std::vector<float>& x, double rate_hz) {
    std::vector<double> crossings;
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0F && x[i] >= 0.0F) {
            const double frac =
                -static_cast<double>(x[i - 1]) / (static_cast<double>(x[i]) - x[i - 1]);
            crossings.push_back(static_cast<double>(i - 1) + frac);
        }
    }
    if (crossings.size() < 2) {
        return 0.0;
    }
    return rate_hz * static_cast<double>(crossings.size() - 1) /
           (crossings.back() - crossings.front());
}

}  // namespace anno::test
