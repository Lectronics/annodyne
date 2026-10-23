#include "core/analysis/envelope/envelope.h"
#include "core/analysis/partials/partials.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;
constexpr double k_hop = 0.005;

/// A vocal-tract-like envelope in dB: two resonances (formants) on a falling slope.
double true_env_db(double f) {
    const auto bump = [&](double centre, double width, double height) {
        const double x = (f - centre) / width;
        return height * std::exp(-0.5 * x * x);
    };
    return -6.0 * std::log2(1.0 + f / 500.0) + bump(700.0, 150.0, 18.0) + bump(1800.0, 250.0, 12.0);
}

struct Scene {
    std::vector<float> mono;
    FrameGrid grid;
    std::vector<Cents> drift;
    std::vector<Cents> mod;
    Cents center;
    HarmonicTrack harmonics;

    [[nodiscard]] NoteCurves curves() const {
        return {.grid = grid, .pitch_center = center, .pitch_drift = drift, .pitch_mod = mod};
    }
};

Scene make_scene(double f0) {
    Scene sc;
    std::vector<double> amps;
    for (int k = 1; static_cast<double>(k) * f0 < 0.45 * k_rate && k <= 60; ++k) {
        amps.push_back(0.05 * std::pow(10.0, true_env_db(static_cast<double>(k) * f0) / 20.0));
    }
    sc.mono = make_harmonic([&](double) { return f0; }, 1.0, k_rate, amps);
    sc.grid =
        FrameGrid{.hop = Seconds{k_hop}, .start = SourceTime{} + Seconds{0.2}, .n_frames = 100};
    sc.center = to_cents(Hz{f0}, k_a4_standard_hz);
    sc.drift.assign(100, Cents{0.0});
    sc.mod.assign(100, Cents{0.0});
    sc.harmonics = analyse_partials(sc.mono, Hz{k_rate}, sc.curves(), {}).value();
    return sc;
}

/// Envelope at `hz` of frame `frame`, linearly interpolated between grid points.
double env_at(const EnvelopeTrack& e, std::size_t frame, double hz) {
    const auto cols = e.freqs.size();
    std::size_t hi = 1;
    while (hi + 1 < cols && e.freqs[hi].value() < hz) {
        ++hi;
    }
    const double f0 = e.freqs[hi - 1].value();
    const double f1 = e.freqs[hi].value();
    const double w = std::clamp((hz - f0) / (f1 - f0), 0.0, 1.0);
    const auto a = static_cast<double>(e.log_mag_db.data[frame * cols + hi - 1]);
    const auto b = static_cast<double>(e.log_mag_db.data[frame * cols + hi]);
    return a + w * (b - a);
}

}  // namespace

TEST_CASE("the_envelope_passes_through_the_harmonic_peaks", "[envelope]") {
    for (const double f0 : {150.0, 220.0, 400.0}) {
        const auto sc = make_scene(f0);
        const auto e = estimate_envelope(sc.curves(), sc.harmonics, Hz{k_rate}, {}).value();
        REQUIRE(validate(e).has_value());
        const auto cols = static_cast<std::size_t>(sc.harmonics.freq_hz.cols);
        for (const std::size_t frame : {10U, 50U, 90U}) {
            for (std::size_t k = 0; k < cols; ++k) {
                const auto amp =
                    static_cast<double>(sc.harmonics.amp_linear.data[frame * cols + k]);
                const auto f = static_cast<double>(sc.harmonics.freq_hz.data[frame * cols + k]);
                if (amp <= 0.0) {
                    continue;
                }
                // At least the peak, to within the tolerance plus the grid spacing of the lookup.
                CHECK(env_at(e, frame, f) > 20.0 * std::log10(amp) - 1.0);
            }
        }
    }
}

TEST_CASE("the_envelope_follows_the_true_formant_curve", "[envelope]") {
    const auto sc = make_scene(150.0);
    const auto e = estimate_envelope(sc.curves(), sc.harmonics, Hz{k_rate}, {}).value();
    // Compare the shape (offset removed) with the true envelope at the harmonics.
    const std::size_t frame = 50;
    const double offset = env_at(e, frame, 700.0) - true_env_db(700.0);
    for (const double f : {150.0, 450.0, 700.0, 1000.0, 1800.0, 2400.0}) {
        CHECK(std::abs(env_at(e, frame, f) - offset - true_env_db(f)) < 2.0);
    }
    // The first formant peak sits within 5 % of 700 Hz.
    double best_f = 0.0;
    double best_db = -1e9;
    for (std::size_t b = 0; b < e.freqs.size(); ++b) {
        const double f = e.freqs[b].value();
        if (f > 300.0 && f < 1300.0 &&
            static_cast<double>(e.log_mag_db.data[frame * e.freqs.size() + b]) > best_db) {
            best_db = static_cast<double>(e.log_mag_db.data[frame * e.freqs.size() + b]);
            best_f = f;
        }
    }
    CHECK(std::abs(best_f - 700.0) < 35.0);
}

TEST_CASE("a_frame_without_harmonics_sits_at_the_floor", "[envelope]") {
    auto sc = make_scene(200.0);
    const auto cols = static_cast<std::size_t>(sc.harmonics.amp_linear.cols);
    for (std::size_t k = 0; k < cols; ++k) {
        sc.harmonics.amp_linear.data[20 * cols + k] = 0.0F;
    }
    EnvelopeParams p;
    p.floor = Decibels{-100.0};
    const auto e = estimate_envelope(sc.curves(), sc.harmonics, Hz{k_rate}, p).value();
    for (std::size_t b = 0; b < e.freqs.size(); ++b) {
        CHECK(e.log_mag_db.data[20 * e.freqs.size() + b] == -100.0F);
    }
    CHECK(env_at(e, 21, 700.0) > -100.0);
}

TEST_CASE("the_frequency_axis_runs_from_zero_to_nyquist_and_all_values_are_finite", "[envelope]") {
    const auto sc = make_scene(180.0);
    EnvelopeParams p;
    p.n_bins = 129;
    const auto e = estimate_envelope(sc.curves(), sc.harmonics, Hz{k_rate}, p).value();
    REQUIRE(e.freqs.size() == 129);
    CHECK(e.freqs.front().value() == 0.0);
    CHECK(std::abs(e.freqs.back().value() - k_rate / 2.0) < 1e-6);
    CHECK(e.log_mag_db.cols == 129);
    CHECK(e.log_mag_db.rows == sc.grid.n_frames);
    for (const float v : e.log_mag_db.data) {
        CHECK(std::isfinite(v));
    }
}

TEST_CASE("bad_input_is_rejected", "[envelope]") {
    const auto sc = make_scene(200.0);
    const auto code = [&](const EnvelopeParams& p, double rate = k_rate) {
        return estimate_envelope(sc.curves(), sc.harmonics, Hz{rate}, p).error().code;
    };
    EnvelopeParams p;
    p.order_scale = 2.0;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.max_iterations = 1;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.n_bins = 10;
    CHECK(code(p) == ErrorCode::invalid_argument);
    p = {};
    p.tolerance = Decibels{-1.0};
    CHECK(code(p) == ErrorCode::invalid_argument);
    CHECK(code({}, 0.0) == ErrorCode::invalid_argument);
    HarmonicTrack bad = sc.harmonics;
    bad.freq_hz.rows = 3;
    CHECK(estimate_envelope(sc.curves(), bad, Hz{k_rate}, {}).error().code ==
          ErrorCode::invalid_argument);
    HarmonicTrack silent = sc.harmonics;
    std::ranges::fill(silent.amp_linear.data, 0.0F);
    CHECK(estimate_envelope(sc.curves(), silent, Hz{k_rate}, {}).error().code ==
          ErrorCode::invalid_argument);
}
