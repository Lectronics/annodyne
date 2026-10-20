#include "core/dsp/filter.h"
#include "core/dsp/k_weight.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 48000.0;

std::vector<double> sine(double hz, double seconds, double rate = k_rate) {
    const auto n = static_cast<std::size_t>(std::llround(seconds * rate));
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate);
    }
    return x;
}

/// RMS of the middle half (the edges carry reflection effects).
double middle_rms(const std::vector<double>& x) {
    double sum = 0.0;
    const std::size_t lo = x.size() / 4;
    const std::size_t hi = 3 * x.size() / 4;
    for (std::size_t i = lo; i < hi; ++i) {
        sum += x[i] * x[i];
    }
    return std::sqrt(sum / static_cast<double>(hi - lo));
}

/// Gain in dB of a unit sine of `hz` through `filter`.
template <typename Filter>
double gain_db(Filter filter, double hz) {
    const auto x = sine(hz, 1.0);
    const auto y = filter(x).value();
    return 20.0 * std::log10(middle_rms(y) / middle_rms(x));
}

auto highpass_at(double cutoff_hz) {
    return [cutoff_hz](const std::vector<double>& x) {
        return zero_phase_highpass(x, Hz{k_rate}, Hz{cutoff_hz});
    };
}

auto lowpass_at(double cutoff_hz) {
    return [cutoff_hz](const std::vector<double>& x) {
        return zero_phase_lowpass(x, Hz{k_rate}, Hz{cutoff_hz});
    };
}

}  // namespace

TEST_CASE("the_passband_is_flat_and_the_cutoff_is_minus_six_db", "[dsp][filter]") {
    CHECK(std::abs(gain_db(highpass_at(100.0), 400.0)) < tolerance::k_filter_passband.value());
    CHECK(std::abs(gain_db(lowpass_at(2000.0), 500.0)) < tolerance::k_filter_passband.value());
    CHECK(std::abs(gain_db(highpass_at(100.0), 100.0) + 6.02) < tolerance::k_filter_cutoff.value());
    CHECK(std::abs(gain_db(lowpass_at(2000.0), 2000.0) + 6.02) <
          tolerance::k_filter_cutoff.value());
}

TEST_CASE("one_octave_outside_the_cutoff_the_attenuation_is_at_least_24_db", "[dsp][filter]") {
    // 2nd order, forward and backward: 10 log10(1 + 2^4) = 12.3 dB per pass, 24.6 dB in all.
    CHECK(gain_db(highpass_at(200.0), 100.0) <= -tolerance::k_filter_octave_db.value());
    CHECK(gain_db(lowpass_at(1000.0), 2000.0) <= -tolerance::k_filter_octave_db.value());
    CHECK(gain_db(highpass_at(200.0), 25.0) <= -60.0);  // three octaves down
}

TEST_CASE("the_filters_add_no_delay", "[dsp][filter]") {
    // A symmetric pulse stays symmetric and keeps its peak position.
    std::vector<double> x(4001, 0.0);
    x[2000] = 1.0;
    for (const auto& y : {lowpass_at(2000.0)(x).value(), highpass_at(100.0)(x).value()}) {
        std::size_t peak = 0;
        for (std::size_t i = 0; i < y.size(); ++i) {
            if (std::abs(y[i]) > std::abs(y[peak])) {
                peak = i;
            }
        }
        CHECK(peak == 2000);
        for (std::size_t k = 1; k < 400; ++k) {
            CHECK(std::abs(y[2000 + k] - y[2000 - k]) < 1e-12);
        }
    }
}

TEST_CASE("the_highpass_removes_dc_and_the_lowpass_keeps_it", "[dsp][filter]") {
    const std::vector<double> dc(48000, 0.5);
    const auto hp = highpass_at(60.0)(dc).value();
    const auto lp = lowpass_at(2000.0)(dc).value();
    CHECK(std::abs(middle_rms(hp)) < 1e-4);
    CHECK(std::abs(middle_rms(lp) - 0.5) < 1e-6);
}

TEST_CASE("a_very_low_cutoff_stays_stable_and_short_inputs_work", "[dsp][filter]") {
    const auto x = sine(1000.0, 1.0);
    const auto y = zero_phase_highpass(x, Hz{k_rate}, Hz{k_rate * 0.001}).value();
    REQUIRE(y.size() == x.size());
    for (const double v : y) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) < 2.0);
    }
    for (const std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{17}}) {
        const std::vector<double> tiny(n, 0.25);
        const auto out = zero_phase_lowpass(tiny, Hz{k_rate}, Hz{1000.0});
        REQUIRE(out.has_value());
        CHECK(out->size() == n);
        for (const double v : *out) {
            CHECK(std::isfinite(v));
        }
    }
}

TEST_CASE("the_filters_validate_their_arguments", "[dsp][filter]") {
    const auto x = sine(1000.0, 0.1);
    for (const double cutoff : {0.0, -5.0, k_rate / 2.0, k_rate, std::nan("")}) {
        CHECK(zero_phase_highpass(x, Hz{k_rate}, Hz{cutoff}).error().code ==
              ErrorCode::invalid_argument);
        CHECK(zero_phase_lowpass(x, Hz{k_rate}, Hz{cutoff}).error().code ==
              ErrorCode::invalid_argument);
    }
    CHECK(zero_phase_lowpass(x, Hz{0.0}, Hz{100.0}).error().code == ErrorCode::invalid_argument);
    CHECK(zero_phase_lowpass(x, Hz{std::nan("")}, Hz{100.0}).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("the_filters_are_deterministic_and_leave_their_input_alone", "[dsp][filter]") {
    const auto x = sine(300.0, 0.5);
    const auto copy = sine(300.0, 0.5);  // independent construction of the same signal
    const auto a = zero_phase_lowpass(x, Hz{k_rate}, Hz{1500.0}).value();
    const auto b = zero_phase_lowpass(x, Hz{k_rate}, Hz{1500.0}).value();
    CHECK(a == b);
    CHECK(x == copy);
}

TEST_CASE("k_weighting_is_bit_exact_after_sharing_the_biquad", "[dsp][filter]") {
    // Reference values recorded before the refactor (a 1 kHz sine at 48 kHz, 8 samples at 20000).
    const auto x = make_sine(1000.0, 1.0, k_rate, 0.5);
    const auto y = k_weight(x, Hz{k_rate});
    REQUIRE(y.size() == x.size());
    double energy = 0.0;
    for (const float v : y) {
        energy += static_cast<double>(v) * v;
    }
    CHECK(energy == tolerance::k_k_weight_reference_energy);
}
