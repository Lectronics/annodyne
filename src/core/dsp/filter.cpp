#include "core/dsp/filter.h"

#include "core/dsp/detail/biquad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

constexpr double k_butterworth_q = std::numbers::sqrt2 / 2.0;  // maximally flat 2nd order
constexpr double k_reflect_periods = 3.0;  // edge padding, in periods of the cutoff

enum class Side { high, low };

detail::Biquad design(Side side, double cutoff_hz, double rate_hz) {
    const double k = std::tan(std::numbers::pi * cutoff_hz / rate_hz);
    const double a0 = 1.0 + k / k_butterworth_q + k * k;
    detail::Biquad q;
    q.a1 = 2.0 * (k * k - 1.0) / a0;
    q.a2 = (1.0 - k / k_butterworth_q + k * k) / a0;
    if (side == Side::high) {
        q.b0 = 1.0 / a0;
        q.b1 = -2.0 / a0;
        q.b2 = 1.0 / a0;
    } else {
        q.b0 = k * k / a0;
        q.b1 = 2.0 * k * k / a0;
        q.b2 = k * k / a0;
    }
    return q;
}

/// `x` extended by `pad` samples of odd reflection about its end points at both ends (keeps the
/// signal continuous in value and slope, so the filter's start-up transient is small).
std::vector<double> reflect_pad(std::span<const double> x, std::size_t pad) {
    const std::size_t n = x.size();
    std::vector<double> out;
    out.reserve(n + 2 * pad);
    for (std::size_t i = pad; i >= 1; --i) {
        out.push_back(2.0 * x[0] - x[i]);
    }
    out.insert(out.end(), x.begin(), x.end());
    for (std::size_t i = 1; i <= pad; ++i) {
        out.push_back(2.0 * x[n - 1] - x[n - 1 - i]);
    }
    return out;
}

Result<std::vector<double>> filter_zero_phase(Side side, std::span<const double> x, Hz sample_rate,
                                              Hz cutoff) {
    const double rate = sample_rate.value();
    const double fc = cutoff.value();
    if (!std::isfinite(rate) || rate <= 0.0 || !std::isfinite(fc) || fc <= 0.0 ||
        fc >= rate / 2.0) {
        return fail(ErrorCode::invalid_argument,
                    "filter: need a finite sample rate > 0 and 0 < cutoff < rate / 2");
    }
    if (x.size() < 2) {
        return std::vector<double>(x.begin(), x.end());
    }
    const auto want = static_cast<std::size_t>(std::ceil(k_reflect_periods * rate / fc));
    const std::size_t pad = std::min(want, x.size() - 1);
    std::vector<double> y = reflect_pad(x, pad);
    const detail::Biquad q = design(side, fc, rate);
    detail::run_biquad(q, y);
    std::ranges::reverse(y);
    detail::run_biquad(q, y);
    std::ranges::reverse(y);
    return std::vector<double>(y.begin() + static_cast<std::ptrdiff_t>(pad),
                               y.begin() + static_cast<std::ptrdiff_t>(pad + x.size()));
}

}  // namespace

Result<std::vector<double>> zero_phase_highpass(std::span<const double> x, Hz sample_rate,
                                                Hz cutoff) {
    return filter_zero_phase(Side::high, x, sample_rate, cutoff);
}

Result<std::vector<double>> zero_phase_lowpass(std::span<const double> x, Hz sample_rate,
                                               Hz cutoff) {
    return filter_zero_phase(Side::low, x, sample_rate, cutoff);
}

}  // namespace anno
