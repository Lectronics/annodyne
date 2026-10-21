#include "core/dsp/k_weight.h"

#include "core/dsp/detail/biquad.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

// BS.1770 stage constants (De Man's analytic form).
constexpr double k_shelf_f0_hz = 1681.974450955533;
constexpr double k_shelf_gain_db = 3.999843853973347;
constexpr double k_shelf_q = 0.7071752369554196;
constexpr double k_shelf_vb_exponent = 0.4996667741545416;
constexpr double k_hp_f0_hz = 38.13547087602444;
constexpr double k_hp_q = 0.5003270373238773;

using detail::Biquad;

Biquad shelf(double rate_hz) {
    const double k = std::tan(std::numbers::pi * k_shelf_f0_hz / rate_hz);
    const double vh = std::pow(10.0, k_shelf_gain_db / 20.0);
    const double vb = std::pow(vh, k_shelf_vb_exponent);
    const double a0 = 1.0 + k / k_shelf_q + k * k;
    return {.b0 = (vh + vb * k / k_shelf_q + k * k) / a0,
            .b1 = 2.0 * (k * k - vh) / a0,
            .b2 = (vh - vb * k / k_shelf_q + k * k) / a0,
            .a1 = 2.0 * (k * k - 1.0) / a0,
            .a2 = (1.0 - k / k_shelf_q + k * k) / a0};
}

Biquad high_pass(double rate_hz) {
    const double k = std::tan(std::numbers::pi * k_hp_f0_hz / rate_hz);
    const double a0 = 1.0 + k / k_hp_q + k * k;
    return {.b0 = 1.0,
            .b1 = -2.0,
            .b2 = 1.0,
            .a1 = 2.0 * (k * k - 1.0) / a0,
            .a2 = (1.0 - k / k_hp_q + k * k) / a0};
}

}  // namespace

std::vector<float> k_weight(std::span<const float> mono, Hz sample_rate) {
    assert(std::isfinite(sample_rate.value()) && sample_rate.value() >= 4000.0);
    std::vector<double> x(mono.begin(), mono.end());
    detail::run_biquad(shelf(sample_rate.value()), x);
    detail::run_biquad(high_pass(sample_rate.value()), x);
    std::vector<float> out(x.size());
    std::ranges::transform(x, out.begin(), [](double v) { return static_cast<float>(v); });
    return out;
}

}  // namespace anno
