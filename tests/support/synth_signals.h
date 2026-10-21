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

namespace anno::test {

/// A pitched signal: a sum of harmonics of an instantaneous fundamental. `f0_hz` gives the
/// fundamental (Hz) at time t seconds; `partial_amps[k]` is the amplitude of partial k + 1
/// (linear). The phase is integrated per sample, so glides and vibrato are exact.
template <class F0Hz>
std::vector<float> make_harmonic(const F0Hz& f0_hz, double seconds, double rate_hz,
                                 const std::vector<double>& partial_amps) {
    const auto n = static_cast<std::size_t>(std::llround(seconds * rate_hz));
    std::vector<float> x(n, 0.0F);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / rate_hz;
        double v = 0.0;
        for (std::size_t k = 0; k < partial_amps.size(); ++k) {
            v += partial_amps[k] * std::sin(phase * static_cast<double>(k + 1));
        }
        x[i] = static_cast<float>(v);
        phase += 2.0 * std::numbers::pi * f0_hz(t) / rate_hz;
    }
    return x;
}

/// Amplitudes 1/k for k = 1..n (a sawtooth-like spectrum) scaled so the peak stays below `peak`.
inline std::vector<double> harmonic_amps(int n, double peak = 0.5) {
    std::vector<double> a(static_cast<std::size_t>(n));
    double sum = 0.0;
    for (int k = 1; k <= n; ++k) {
        a[static_cast<std::size_t>(k - 1)] = 1.0 / k;
        sum += 1.0 / k;
    }
    for (double& v : a) {
        v *= peak / sum;
    }
    return a;
}

/// Adds seeded white noise so that the signal-to-noise ratio (RMS) is `snr_db`.
inline void add_white_noise(std::vector<float>& x, double snr_db, std::uint64_t seed) {
    const double sigma = rms(x) * std::pow(10.0, -snr_db / 20.0);
    std::uint64_t state = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    for (float& v : x) {
        double sum = 0.0;  // sum of 12 uniforms - 6: approximately N(0, 1)
        for (int k = 0; k < 12; ++k) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            sum += static_cast<double>(state >> 40U) / 16777216.0;
        }
        v += static_cast<float>(sigma * (sum - 6.0));
    }
}

/// Seeded white noise of RMS about `level` (linear).
inline std::vector<float> make_noise(double seconds, double rate_hz, double level,
                                     std::uint64_t seed) {
    std::vector<float> x(static_cast<std::size_t>(std::llround(seconds * rate_hz)), 0.0F);
    std::uint64_t state = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    for (float& v : x) {
        double sum = 0.0;
        for (int k = 0; k < 12; ++k) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            sum += static_cast<double>(state >> 40U) / 16777216.0;
        }
        v = static_cast<float>(level * (sum - 6.0));
    }
    return x;
}

}  // namespace anno::test
