#include "cli/checks/audio_checks.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace anno::cli {

bool bit_identical(const AudioBuffer& a, const AudioBuffer& b) {
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

std::optional<std::int64_t> first_difference(const AudioBuffer& a, const AudioBuffer& b) {
    const std::int64_t frames = std::min(a.frame_count().value(), b.frame_count().value());
    const int channels = std::min(a.channel_count(), b.channel_count());
    for (std::int64_t i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
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

namespace {

/// Click candidates of one channel, appended to `clicks`.
void find_channel_clicks(std::span<const float> x, const ClickParams& params,
                         std::vector<std::int64_t>& clicks) {
    const auto frames = static_cast<std::int64_t>(x.size());
    std::vector<double> d(x.size(), 0.0);
    for (std::size_t i = 1; i < d.size(); ++i) {
        d[i] = static_cast<double>(x[i]) - static_cast<double>(x[i - 1]);
    }
    for (std::int64_t i = 1; i < frames; ++i) {
        const std::int64_t lo = std::max<std::int64_t>(1, i - params.window);
        const std::int64_t hi = std::min<std::int64_t>(frames, i + params.window + 1);
        double sum = 0.0;
        std::int64_t n = 0;
        for (std::int64_t j = lo; j < hi; ++j) {
            if (j != i) {
                sum += d[static_cast<std::size_t>(j)] * d[static_cast<std::size_t>(j)];
                ++n;
            }
        }
        const double local_rms = n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
        const double jump = std::abs(d[static_cast<std::size_t>(i)]);
        if (jump > params.floor && jump > params.ratio * local_rms) {
            clicks.push_back(i);
        }
    }
}

}  // namespace

std::vector<std::int64_t> find_clicks(const AudioBuffer& audio, const ClickParams& params) {
    std::vector<std::int64_t> clicks;
    for (int c = 0; c < audio.channel_count(); ++c) {
        find_channel_clicks(audio.channel(c), params, clicks);
    }
    std::ranges::sort(clicks);
    clicks.erase(std::ranges::unique(clicks).begin(), clicks.end());
    return clicks;
}

SanityReport sanity(const AudioBuffer& audio) {
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

std::uint64_t hash_audio(const AudioBuffer& audio) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto mix = [&h](std::uint64_t v) {
        for (unsigned i = 0; i < 8U; ++i) {
            h = (h ^ ((v >> (8U * i)) & 0xFFU)) * 0x100000001b3ULL;
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

double rms(const AudioBuffer& audio) {
    const double n = static_cast<double>(audio.frame_count().value()) * audio.channel_count();
    if (n <= 0.0) {
        return 0.0;
    }
    double sum = 0.0;
    for (int c = 0; c < audio.channel_count(); ++c) {
        for (const float v : audio.channel(c)) {
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
    }
    return std::sqrt(sum / n);
}

}  // namespace anno::cli
