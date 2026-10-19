#pragma once

#include "cli/checks/audio_checks.h"
#include "core/types/audio_buffer.h"
#include "tolerances.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

// The objective audio checks live in cli/checks (shared with `anno-cli audition`); this header
// re-exports them for tests and adds the length check and synthetic signal builders. Click-detector
// defaults are pinned to tests/tolerances.h by scenario_test.

namespace anno::test {

using anno::cli::bit_identical;
using anno::cli::find_clicks;
using anno::cli::first_difference;
using anno::cli::hash_audio;
using anno::cli::sanity;
using anno::cli::SanityReport;

/// Length check: |actual - expected| <= tolerance::k_length frames.
inline bool length_matches(const AudioBuffer& audio, Samples expected) {
    const auto diff = audio.frame_count().value() - expected.value();
    return std::abs(diff) <= tolerance::k_length.value();
}

/// A deterministic test signal: two sines per channel (different per channel) plus fixed
/// pseudo-random noise, so no two samples coincide. Peak below 0.9.
inline AudioBuffer make_signal(int channels, double seconds, double sample_rate_hz = 48000.0) {
    const Hz rate{sample_rate_hz};
    const Samples frames = to_samples(Seconds{seconds}, rate, Rounding::nearest);
    AudioBuffer buffer = AudioBuffer::create(channels, frames, rate).value();
    std::uint64_t state = 0x2545F4914F6CDD1DULL;
    for (int c = 0; c < channels; ++c) {
        const double f1 = 220.0 * (c + 1);
        const double f2 = 331.0 * (c + 2);
        auto samples = buffer.channel(c);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            const double noise = (static_cast<double>(state >> 40U) / 16777216.0 - 0.5) * 0.02;
            const double t = static_cast<double>(i) / sample_rate_hz;
            samples[i] =
                static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * f1 * t) +
                                   0.3 * std::sin(2.0 * std::numbers::pi * f2 * t) + noise);
        }
    }
    return buffer;
}

}  // namespace anno::test
