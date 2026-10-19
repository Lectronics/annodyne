#include "core/dsp/crossfade.h"
#include "tolerances.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace anno;

namespace {

constexpr std::array<FadeShape, 3> k_shapes{FadeShape::equal_power, FadeShape::linear,
                                            FadeShape::log};
constexpr double k_gain_abs = 1e-6;  // float cast of a double gain

}  // namespace

TEST_CASE("equal_power_gains_keep_constant_energy", "[crossfade]") {
    for (int i = 0; i <= 100; ++i) {
        const auto g = crossfade_gains(FadeShape::equal_power, i / 100.0);
        CHECK(std::abs(g.outgoing * g.outgoing + g.incoming * g.incoming - 1.0) < k_gain_abs);
    }
}

TEST_CASE("linear_gains_sum_to_one", "[crossfade]") {
    for (int i = 0; i <= 100; ++i) {
        const auto g = crossfade_gains(FadeShape::linear, i / 100.0);
        CHECK(std::abs(g.outgoing + g.incoming - 1.0) < k_gain_abs);
    }
}

TEST_CASE("log_gains_follow_the_documented_formula", "[crossfade]") {
    const auto mid = crossfade_gains(FadeShape::log, 0.5);
    CHECK(std::abs(mid.incoming - std::pow(10.0, -30.0 / 20.0)) < k_gain_abs);
    CHECK(std::abs(mid.outgoing - mid.incoming) < k_gain_abs);
    const auto end = crossfade_gains(FadeShape::log, 1.0);
    CHECK(std::abs(end.incoming - 1.0) < k_gain_abs);
    CHECK(std::abs(end.outgoing - 0.001) < k_gain_abs);  // the -60 dB floor, not zero
}

TEST_CASE("gains_at_the_ends_pick_one_side", "[crossfade]") {
    for (const FadeShape shape : k_shapes) {
        const auto start = crossfade_gains(shape, 0.0);
        const auto end = crossfade_gains(shape, 1.0);
        CHECK(start.outgoing == 1.0F);
        CHECK(end.incoming == 1.0F);
    }
}

TEST_CASE("crossfade_blends_two_constant_signals", "[crossfade]") {
    const std::vector<float> a(8, 1.0F);
    const std::vector<float> b(8, 3.0F);
    std::vector<float> out(8);
    crossfade(a, b, out, FadeShape::linear, 0, 8);
    CHECK(out[0] == Catch::Approx(1.0 + 2.0 * (0.5 / 8.0)).margin(k_gain_abs));
    CHECK(out[7] == Catch::Approx(1.0 + 2.0 * (7.5 / 8.0)).margin(k_gain_abs));
    for (std::size_t i = 1; i < out.size(); ++i) {
        CHECK(out[i] > out[i - 1]);
    }
}

TEST_CASE("slices_of_a_ramp_concatenate_to_the_whole_ramp_bit_exactly", "[crossfade]") {
    std::vector<float> a(100);
    std::vector<float> b(100);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = std::sin(0.1F * static_cast<float>(i));
        b[i] = std::cos(0.37F * static_cast<float>(i));
    }
    for (const FadeShape shape : k_shapes) {
        std::vector<float> whole(100);
        crossfade(a, b, whole, shape, 0, 100);
        std::vector<float> parts(100);
        const std::span<const float> as{a};
        const std::span<const float> bs{b};
        const std::span<float> ps{parts};
        crossfade(as.subspan(0, 37), bs.subspan(0, 37), ps.subspan(0, 37), shape, 0, 100);
        crossfade(as.subspan(37), bs.subspan(37), ps.subspan(37), shape, 37, 100);
        CHECK(parts == whole);
    }
}

TEST_CASE("crossfade_may_write_in_place_and_empty_spans_are_a_no_op", "[crossfade]") {
    std::vector<float> a(4, 2.0F);
    const std::vector<float> b(4, 4.0F);
    crossfade(a, b, a, FadeShape::linear, 0, 4);
    CHECK(a[0] == Catch::Approx(2.25).margin(k_gain_abs));
    crossfade({}, {}, {}, FadeShape::linear, 0, 1);
}

TEST_CASE("crossfade_is_deterministic", "[crossfade]") {
    const std::vector<float> a(64, 0.3F);
    const std::vector<float> b(64, -0.7F);
    std::vector<float> x(64);
    std::vector<float> y(64);
    crossfade(a, b, x, FadeShape::equal_power, 0, 64);
    crossfade(a, b, y, FadeShape::equal_power, 0, 64);
    CHECK(x == y);
}
