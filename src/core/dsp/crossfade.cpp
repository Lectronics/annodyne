#include "core/dsp/crossfade.h"

#include "core/types/contract.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

/// Level (dB) of the quiet end of a log fade.
constexpr double k_log_floor_db = -60.0;

}  // namespace

CrossfadeGains crossfade_gains(FadeShape shape, double t) noexcept {
    ANNO_EXPECTS(t >= 0.0 && t <= 1.0);
    t = std::clamp(t, 0.0, 1.0);
    switch (shape) {
    case FadeShape::equal_power: {
        const double angle = t * std::numbers::pi / 2.0;
        return {.outgoing = static_cast<float>(std::cos(angle)),
                .incoming = static_cast<float>(std::sin(angle))};
    }
    case FadeShape::linear:
        return {.outgoing = static_cast<float>(1.0 - t), .incoming = static_cast<float>(t)};
    case FadeShape::log:
        return {.outgoing = static_cast<float>(std::pow(10.0, k_log_floor_db * t / 20.0)),
                .incoming = static_cast<float>(std::pow(10.0, k_log_floor_db * (1.0 - t) / 20.0))};
    }
    return {.outgoing = 1.0F, .incoming = 0.0F};
}

void crossfade(std::span<const float> a, std::span<const float> b, std::span<float> out,
               FadeShape shape, std::int64_t ramp_offset, std::int64_t ramp_length) noexcept {
    ANNO_EXPECTS(a.size() == b.size() && a.size() == out.size());
    const auto size = static_cast<std::int64_t>(out.size());
    if (size == 0) {
        return;
    }
    ANNO_EXPECTS(ramp_length >= 1 && ramp_offset >= 0 && ramp_offset + size <= ramp_length);
    const auto length = static_cast<double>(ramp_length);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t =
            (static_cast<double>(ramp_offset + static_cast<std::int64_t>(i)) + 0.5) / length;
        const CrossfadeGains g = crossfade_gains(shape, t);
        out[i] = a[i] * g.outgoing + b[i] * g.incoming;
    }
}

}  // namespace anno
