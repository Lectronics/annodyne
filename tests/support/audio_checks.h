#pragma once

#include "core/types/audio_buffer.h"
#include "tolerances.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

// The objective audio checks of docs/AUDIO_VERIFICATION.md §1 as plain functions, plus synthetic
// signal builders. Thresholds come from tests/tolerances.h only.

namespace anno::test {

/// True if both buffers have the same shape and every sample has the same bit pattern.
inline bool bit_identical(const AudioBuffer& a, const AudioBuffer& b) {
    if (a.channel_count() != b.channel_count() || a.frame_count() != b.frame_count() ||
        a.sample_rate() != b.sample_rate()) {
        return false;
    }
    for (int c = 0; c < a.channel_count(); ++c) {
        const auto x = a.channel(c);
        const auto y = b.channel(c);
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (std::bit_cast<std::uint32_t>(x[i]) != std::bit_cast<std::uint32_t>(y[i])) {
                return false;
            }
        }
    }
    return true;
}

/// First frame at which the buffers differ in any channel (shapes must match).
inline std::optional<std::int64_t> first_difference(const AudioBuffer& a, const AudioBuffer& b) {
    const std::int64_t frames = std::min(a.frame_count().value(), b.frame_count().value());
    for (std::int64_t i = 0; i < frames; ++i) {
        for (int c = 0; c < std::min(a.channel_count(), b.channel_count()); ++c) {
            const auto k = static_cast<std::size_t>(i);
            if (std::bit_cast<std::uint32_t>(a.channel(c)[k]) !=
                std::bit_cast<std::uint32_t>(b.channel(c)[k])) {
                return i;
            }
        }
    }
    if (a.frame_count() != b.frame_count()) {
        return frames;
    }
    return std::nullopt;
}

/// Click detector: frames whose first difference exceeds k_click_ratio times the local RMS of the
/// first difference (window k_click_window each side, the frame itself excluded) and k_click_floor.
inline std::vector<std::int64_t> find_clicks(const AudioBuffer& audio) {
    std::vector<std::int64_t> clicks;
    const auto frames = audio.frame_count().value();
    for (int c = 0; c < audio.channel_count(); ++c) {
        const auto x = audio.channel(c);
        std::vector<double> d(static_cast<std::size_t>(frames), 0.0);
        for (std::size_t i = 1; i < d.size(); ++i) {
            d[i] = static_cast<double>(x[i]) - static_cast<double>(x[i - 1]);
        }
        for (std::int64_t i = 1; i < frames; ++i) {
            const auto lo = std::max<std::int64_t>(1, i - tolerance::k_click_window);
            const auto hi = std::min<std::int64_t>(frames, i + tolerance::k_click_window + 1);
            double sum = 0.0;
            std::int64_t n = 0;
            for (std::int64_t j = lo; j < hi; ++j) {
                if (j != i) {
                    sum += d[static_cast<std::size_t>(j)] * d[static_cast<std::size_t>(j)];
                    ++n;
                }
            }
            const double rms = n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
            const double jump = std::abs(d[static_cast<std::size_t>(i)]);
            if (jump > tolerance::k_click_floor && jump > tolerance::k_click_ratio * rms) {
                clicks.push_back(i);
            }
        }
    }
    std::ranges::sort(clicks);
    clicks.erase(std::unique(clicks.begin(), clicks.end()), clicks.end());
    return clicks;
}

struct SanityReport {
    std::int64_t nan_inf_samples = 0;
    Decibels worst_dc_dbfs{-200.0};  // largest channel mean, dBFS
    float peak = 0.0F;               // largest |sample|
};

inline SanityReport sanity(const AudioBuffer& audio) {
    SanityReport report;
    for (int c = 0; c < audio.channel_count(); ++c) {
        double sum = 0.0;
        for (const float v : audio.channel(c)) {
            if (!std::isfinite(v)) {
                ++report.nan_inf_samples;
                continue;
            }
            sum += static_cast<double>(v);
            report.peak = std::max(report.peak, std::abs(v));
        }
        const auto n = static_cast<double>(std::max<std::int64_t>(1, audio.frame_count().value()));
        const double mean = std::abs(sum / n);
        const double dbfs = mean > 1e-10 ? 20.0 * std::log10(mean) : -200.0;
        report.worst_dc_dbfs = Decibels{std::max(report.worst_dc_dbfs.value(), dbfs)};
    }
    return report;
}

/// FNV-1a over shape and sample bit patterns. Per build only (never store across platforms).
inline std::uint64_t hash_audio(const AudioBuffer& audio) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h = (h ^ ((v >> (8 * i)) & 0xFFU)) * 0x100000001b3ULL;
        }
    };
    mix(static_cast<std::uint64_t>(audio.channel_count()));
    mix(static_cast<std::uint64_t>(audio.frame_count().value()));
    for (int c = 0; c < audio.channel_count(); ++c) {
        for (const float v : audio.channel(c)) {
            mix(std::bit_cast<std::uint32_t>(v));
        }
    }
    return h;
}

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
            const double noise = (static_cast<double>(state >> 40) / 16777216.0 - 0.5) * 0.02;
            const double t = static_cast<double>(i) / sample_rate_hz;
            samples[i] =
                static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * f1 * t) +
                                   0.3 * std::sin(2.0 * std::numbers::pi * f2 * t) + noise);
        }
    }
    return buffer;
}

}  // namespace anno::test
