#include "core/analysis/onsets/stages/level_norm.h"

#include "core/analysis/onsets/detail/stage_check.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace anno {

namespace {

constexpr double k_level_frame_s = 0.020;

/// The level in dB per `mode`, empty for silence (no non-zero sample or no active frame).
std::optional<double> measure_level_db(std::span<const float> mono, Hz rate,
                                       const LevelNormParams& params) {
    if (params.mode == LevelNorm::peak) {
        double peak = 0.0;
        for (const float v : mono) {
            peak = std::max(peak, std::abs(static_cast<double>(v)));
        }
        return peak > 0.0 ? std::optional{20.0 * std::log10(peak)} : std::nullopt;
    }
    const auto frame = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(k_level_frame_s * rate.value())));
    std::vector<double> powers;
    for (std::size_t i = 0; i + frame <= mono.size(); i += frame) {
        double sum = 0.0;
        for (std::size_t k = 0; k < frame; ++k) {
            sum += static_cast<double>(mono[i + k]) * static_cast<double>(mono[i + k]);
        }
        powers.push_back(sum / static_cast<double>(frame));
    }
    const double loudest = powers.empty() ? 0.0 : *std::ranges::max_element(powers);
    if (!(loudest > 0.0)) {
        return std::nullopt;
    }
    const double threshold = loudest * std::pow(10.0, params.active_floor.value() / 10.0);
    double sum = 0.0;
    std::size_t count = 0;
    for (const double p : powers) {
        if (p >= threshold) {
            sum += p;
            ++count;
        }
    }
    return 10.0 * std::log10(sum / static_cast<double>(count));
}

}  // namespace

Result<NormalisedSignal> normalise_level(std::span<const float> mono, Hz rate,
                                         const LevelNormParams& params) {
    if (!detail::positive(rate.value()) || !std::isfinite(params.target.value()) ||
        !std::isfinite(params.active_floor.value()) || !std::isfinite(params.max_gain.value()) ||
        params.max_gain.value() < 0.0 ||
        !std::ranges::all_of(mono, [](float v) { return std::isfinite(v); })) {
        return fail(ErrorCode::invalid_argument,
                    "normalise_level: non-finite sample or parameter, or max_gain < 0");
    }
    NormalisedSignal out{.samples = std::vector<float>(mono.begin(), mono.end()),
                         .gain = Decibels{0.0}};
    const std::optional<double> level_db = measure_level_db(mono, rate, params);
    if (!level_db) {
        return out;
    }
    const double gain_db = std::min(params.target.value() - *level_db, params.max_gain.value());
    const double gain = std::pow(10.0, gain_db / 20.0);
    for (float& v : out.samples) {
        v = static_cast<float>(static_cast<double>(v) * gain);
    }
    out.gain = Decibels{gain_db};
    return out;
}

}  // namespace anno
