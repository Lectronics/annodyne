#include "core/dsp/fft.h"
#include "core/dsp/stft.h"
#include "core/dsp/window.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>

using namespace anno;
using namespace anno::test;

TEST_CASE("hann_windows_sum_to_one_at_quarter_hop", "[dsp]") {
    const auto w = make_window(WindowKind::hann_periodic, 1024);
    REQUIRE(w.size() == 1024);
    CHECK(w[0] == 0.0);
    CHECK(w[512] == 1.0);
    for (std::size_t n = 0; n < 256; ++n) {  // COLA: shifts by a quarter window sum to 2
        const double sum = w[n] + w[n + 256] + w[n + 512] + w[n + 768];
        CHECK(std::abs(sum - 2.0) < 1e-12);
    }
    CHECK(make_window(WindowKind::hann_periodic, 1) == std::vector<double>{1.0});
    CHECK(make_window(WindowKind::blackman_harris, 8).size() == 8);
    CHECK(std::abs(make_window(WindowKind::blackman_harris, 64)[0] - 6e-5) < 1e-6);
}

TEST_CASE("real_fft_matches_a_naive_dft_and_round_trips", "[dsp]") {
    for (const std::size_t n : {2UL, 8UL, 12UL, 100UL, 1024UL}) {
        std::mt19937_64 rng{n};
        std::uniform_real_distribution<double> dist{-1.0, 1.0};
        std::vector<double> x(n);
        for (double& v : x) {
            v = dist(rng);
        }
        const auto fft = RealFft::create(n).value();
        std::vector<std::complex<double>> bins(n / 2 + 1);
        fft.forward(x, bins);
        for (std::size_t k = 0; k < bins.size(); ++k) {
            std::complex<double> dft{};
            for (std::size_t i = 0; i < n; ++i) {
                dft += x[i] * std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k * i) /
                                                  static_cast<double>(n));
            }
            REQUIRE(std::abs(dft - bins[k]) < tolerance::k_fft_vs_dft_abs);
        }
        std::vector<double> back(n);
        fft.inverse(bins, back);
        for (std::size_t i = 0; i < n; ++i) {
            REQUIRE(std::abs(back[i] / static_cast<double>(n) - x[i]) < 1e-12);
        }
    }
    CHECK(RealFft::create(1).error().code == ErrorCode::invalid_argument);
    CHECK(RealFft::create(0).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("stft_of_a_sine_peaks_at_its_bin", "[dsp]") {
    const double rate = 48000.0;
    const auto x = make_sine(3000.0, 0.5, rate);  // bin = 3000 / (48000 / 2048) = 128
    const auto spec =
        stft(x, Hz{rate}, {.n_fft = 2048, .hop_samples = 512, .window = WindowKind::hann_periodic})
            .value();
    CHECK(spec.n_frames == static_cast<std::int64_t>(x.size()) / 512 + 1);
    CHECK(spec.n_bins == 1025);
    CHECK(validate(spec).has_value());
    const std::int64_t frame = spec.n_frames / 2;
    std::int64_t peak = 0;
    for (std::int64_t b = 1; b < spec.n_bins; ++b) {
        if (std::abs(spec.at(frame, b)) > std::abs(spec.at(frame, peak))) {
            peak = b;
        }
    }
    CHECK(peak == 128);
}

TEST_CASE("stft_then_istft_reconstructs_the_signal", "[dsp]") {
    const double rate = 44100.0;
    auto x = make_sine(440.0, 0.4, rate, 0.4);
    const auto y = make_sine(1234.5, 0.4, rate, 0.2);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += y[i];
    }
    for (const auto window : {WindowKind::hann_periodic, WindowKind::blackman_harris}) {
        const auto spec =
            stft(x, Hz{rate}, {.n_fft = 1024, .hop_samples = 256, .window = window}).value();
        const auto back = istft(spec, static_cast<std::int64_t>(x.size())).value();
        REQUIRE(back.size() == x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            REQUIRE(std::abs(back[i] - x[i]) < tolerance::k_stft_roundtrip_abs);
        }
    }
}

TEST_CASE("stft_edge_cases_and_validation", "[dsp]") {
    const std::vector<float> tiny(10, 0.5F);
    const auto spec = stft(tiny, Hz{8000.0},
                           {.n_fft = 64, .hop_samples = 16, .window = WindowKind::hann_periodic})
                          .value();
    CHECK(spec.n_frames == 1);  // shorter than n_fft: one zero-padded frame
    CHECK(stft({}, Hz{8000.0}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(stft(tiny, Hz{8000.0},
               {.n_fft = 63, .hop_samples = 16, .window = WindowKind::hann_periodic})
              .error()
              .code == ErrorCode::invalid_argument);
    CHECK(
        stft(tiny, Hz{8000.0}, {.n_fft = 64, .hop_samples = 0, .window = WindowKind::hann_periodic})
            .error()
            .code == ErrorCode::invalid_argument);
    CHECK(stft(tiny, Hz{0.0}, {}).error().code == ErrorCode::invalid_argument);
    Spectrogram bad = spec;
    bad.data.pop_back();
    CHECK_FALSE(validate(bad).has_value());
    CHECK(istft(bad, 10).error().code == ErrorCode::invalid_argument);
    CHECK(istft(spec, 1000).error().code == ErrorCode::invalid_argument);
    CHECK(istft(spec, -1).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("stft_frame_counts_hold_at_other_sample_rates_and_are_deterministic", "[dsp]") {
    for (const double rate : {22050.0, 32000.0, 96000.0}) {
        const auto x = make_sine(500.0, 0.3, rate);
        const auto a =
            stft(x, Hz{rate},
                 {.n_fft = 1024, .hop_samples = 128, .window = WindowKind::hann_periodic})
                .value();
        const auto b =
            stft(x, Hz{rate},
                 {.n_fft = 1024, .hop_samples = 128, .window = WindowKind::hann_periodic})
                .value();
        CHECK(a.n_frames == static_cast<std::int64_t>(x.size()) / 128 + 1);
        CHECK(a.data == b.data);
    }
}
