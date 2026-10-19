#include "core/types/units.h"

#include "core/types/contract.h"

#include <cmath>

namespace melo {

namespace {

/// Largest |seconds * rate| accepted by to_samples: well inside int64 and far beyond any recording.
constexpr double k_max_sample_magnitude = 4.611686018427388e18;  // 2^62

constexpr double k_cents_per_octave = 1200.0;
constexpr double k_db_per_decade_amplitude = 20.0;

bool is_valid_rate(Hz rate) {
    return std::isfinite(rate.value()) && rate.value() > 0.0;
}

}  // namespace

Samples to_samples(Seconds d, Hz sample_rate, Rounding rounding) noexcept {
    MELO_EXPECTS(std::isfinite(d.value()));
    MELO_EXPECTS(is_valid_rate(sample_rate));
    const double frames = d.value() * sample_rate.value();
    MELO_EXPECTS(std::fabs(frames) < k_max_sample_magnitude);

    double whole = 0.0;
    switch (rounding) {
    case Rounding::nearest: whole = std::round(frames); break;  // halves away from zero
    case Rounding::down: whole = std::floor(frames); break;
    case Rounding::up: whole = std::ceil(frames); break;
    }
    return Samples{static_cast<std::int64_t>(whole)};
}

Seconds to_seconds(Samples n, Hz sample_rate) noexcept {
    MELO_EXPECTS(is_valid_rate(sample_rate));
    return Seconds{static_cast<double>(n.value()) / sample_rate.value()};
}

Hz to_hz(Cents pitch, Hz a4_reference) noexcept {
    MELO_EXPECTS(std::isfinite(pitch.value()));
    MELO_EXPECTS(is_valid_rate(a4_reference));
    return a4_reference * std::exp2((pitch - k_a4_cents).value() / k_cents_per_octave);
}

Cents to_cents(Hz f, Hz a4_reference) noexcept {
    MELO_EXPECTS(std::isfinite(f.value()) && f.value() > 0.0);
    MELO_EXPECTS(is_valid_rate(a4_reference));
    return k_a4_cents + Cents{k_cents_per_octave * std::log2((f / a4_reference).value())};
}

Ratio to_ratio(Cents interval) noexcept {
    MELO_EXPECTS(std::isfinite(interval.value()));
    return Ratio{std::exp2(interval.value() / k_cents_per_octave)};
}

LinearGain to_linear_gain(Decibels g) noexcept {
    MELO_EXPECTS(std::isfinite(g.value()));
    return LinearGain{std::pow(10.0, g.value() / k_db_per_decade_amplitude)};
}

Decibels to_decibels(LinearGain a) noexcept {
    MELO_EXPECTS(std::isfinite(a.value()));
    const double magnitude = std::fabs(a.value());
    if (magnitude <= k_silence_floor_gain.value()) {
        return k_silence_floor_db;
    }
    return Decibels{k_db_per_decade_amplitude * std::log10(magnitude)};
}

}  // namespace melo
