#include "core/dsp/resample.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

constexpr double k_kaiser_beta = 8.6;
constexpr double k_cutoff = 0.97;

/// Modified Bessel function of the first kind, order 0 (power series).
double bessel_i0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 60; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < sum * 1e-17) {
            break;
        }
    }
    return sum;
}

double sinc(double x) {
    if (std::abs(x) < 1e-12) {
        return 1.0;
    }
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
}

}  // namespace

Result<std::vector<float>> resample(std::span<const float> mono, Hz from, Hz to,
                                    const ResampleParams& params) {
    if (!std::isfinite(from.value()) || !std::isfinite(to.value()) || from.value() <= 0.0 ||
        to.value() <= 0.0 || params.taps_per_side < 8) {
        return fail(ErrorCode::invalid_argument,
                    "resample: rates must be finite and > 0 and taps_per_side >= 8");
    }
    const double ratio = to.value() / from.value();
    const auto n_out =
        static_cast<std::size_t>(std::llround(static_cast<double>(mono.size()) * ratio));
    std::vector<float> out(n_out, 0.0F);
    const double scale = std::min(1.0, ratio);  // cutoff relative to the input Nyquist
    const double cutoff = k_cutoff * scale;
    const double half_width = static_cast<double>(params.taps_per_side) / scale;  // input samples
    const double i0_beta = bessel_i0(k_kaiser_beta);
    const auto n_in = static_cast<std::int64_t>(mono.size());
    for (std::size_t j = 0; j < n_out; ++j) {
        const double centre = static_cast<double>(j) / ratio;
        const auto first = static_cast<std::int64_t>(std::ceil(centre - half_width));
        const auto last = static_cast<std::int64_t>(std::floor(centre + half_width));
        double acc = 0.0;
        for (std::int64_t k = std::max<std::int64_t>(first, 0); k <= std::min(last, n_in - 1);
             ++k) {
            const double x = static_cast<double>(k) - centre;
            const double u = x / half_width;
            const double window =
                bessel_i0(k_kaiser_beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0_beta;
            acc += static_cast<double>(mono[static_cast<std::size_t>(k)]) * cutoff *
                   sinc(cutoff * x) * window;
        }
        out[j] = static_cast<float>(acc);
    }
    return out;
}

}  // namespace anno
