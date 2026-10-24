#include "core/dsp/band_split.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace anno;

namespace {

constexpr double k_rate = 44100.0;

std::vector<float> tone(double hz, std::size_t n, double amp = 0.5) {
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(
            amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / k_rate));
    }
    return x;
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

}  // namespace

TEST_CASE("split_band_low_plus_high_is_the_input_for_constant_and_moving_cutoffs", "[dsp]") {
    auto x = tone(300.0, 20000);
    const auto hi = tone(5000.0, 20000, 0.2);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += hi[i];
    }
    std::vector<double> constant(x.size(), 2000.0);
    std::vector<double> moving(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        moving[i] = 500.0 + 4000.0 * static_cast<double>(i) / static_cast<double>(x.size());
    }
    for (const auto* cutoff : {&constant, &moving}) {
        const auto s = split_band(x, Hz{k_rate}, *cutoff, Hz{400.0});
        REQUIRE(s.has_value());
        REQUIRE(s->low.size() == x.size());
        REQUIRE(s->high.size() == x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            REQUIRE(std::abs((s->low[i] + s->high[i]) - x[i]) < 1e-6F);
        }
    }
}

TEST_CASE("split_band_zero_cutoff_gives_no_low_band_and_the_input_exactly_as_high", "[dsp]") {
    const auto x = tone(440.0, 8000);
    const std::vector<double> cutoff(x.size(), 0.0);
    const auto s = split_band(x, Hz{k_rate}, cutoff, Hz{400.0});
    REQUIRE(s.has_value());
    for (std::size_t i = 0; i < x.size(); ++i) {
        REQUIRE(s->low[i] == 0.0F);
        REQUIRE(s->high[i] == x[i]);
    }
}

TEST_CASE("split_band_keeps_a_tone_below_the_cutoff_at_unit_gain_and_drops_one_above", "[dsp]") {
    const std::vector<double> cutoff(30000, 2000.0);
    const auto below = split_band(tone(500.0, 30000), Hz{k_rate}, cutoff, Hz{400.0});
    const auto above = split_band(tone(6000.0, 30000), Hz{k_rate}, cutoff, Hz{400.0});
    REQUIRE(below.has_value());
    REQUIRE(above.has_value());
    const double in_rms = 0.5 / std::numbers::sqrt2;
    CHECK(std::abs(rms(below->low, 4000, 26000) / in_rms - 1.0) < 1e-3);
    CHECK(rms(below->high, 4000, 26000) < 1e-3 * in_rms);
    CHECK(rms(above->low, 4000, 26000) < 1e-3 * in_rms);
    CHECK(std::abs(rms(above->high, 4000, 26000) / in_rms - 1.0) < 1e-3);
}

TEST_CASE("split_band_rejects_bad_arguments", "[dsp]") {
    const auto x = tone(440.0, 4000);
    const std::vector<double> ok(x.size(), 1000.0);
    CHECK(split_band({}, Hz{k_rate}, {}, Hz{400.0}).error().code == ErrorCode::invalid_argument);
    CHECK(split_band(x, Hz{0.0}, ok, Hz{400.0}).error().code == ErrorCode::invalid_argument);
    CHECK(split_band(x, Hz{k_rate}, ok, Hz{0.0}).error().code == ErrorCode::invalid_argument);
    const std::vector<double> shorter(x.size() - 1, 1000.0);
    CHECK(split_band(x, Hz{k_rate}, shorter, Hz{400.0}).error().code ==
          ErrorCode::invalid_argument);
    auto negative = ok;
    negative[10] = -1.0;
    CHECK(split_band(x, Hz{k_rate}, negative, Hz{400.0}).error().code ==
          ErrorCode::invalid_argument);
    auto nan = ok;
    nan[10] = std::nan("");
    CHECK(split_band(x, Hz{k_rate}, nan, Hz{400.0}).error().code == ErrorCode::invalid_argument);
}
