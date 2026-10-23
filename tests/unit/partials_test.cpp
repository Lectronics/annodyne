#include "core/analysis/partials/partials.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;
constexpr double k_hop = 0.005;

/// A note with its pitch curve given by f0_hz(t); center = the curve's mean cents, the rest in
/// drift.
struct Fixture {
    std::vector<float> mono;
    FrameGrid grid;
    Cents center;
    std::vector<Cents> drift;
    std::vector<Cents> mod;

    [[nodiscard]] NoteCurves curves() const {
        return {.grid = grid, .pitch_center = center, .pitch_drift = drift, .pitch_mod = mod};
    }
    [[nodiscard]] double frame_seconds(std::int64_t i) const {
        return (frame_time(grid, i) - SourceTime{}).value();
    }
};

template <class F0Hz>
Fixture make_fixture(const F0Hz& f0_hz, double note_start, double note_end, double total,
                     const std::vector<double>& amps) {
    Fixture f;
    f.mono = make_harmonic(f0_hz, total, k_rate, amps);
    const auto first = static_cast<std::int64_t>(std::ceil(note_start / k_hop));
    const auto last = static_cast<std::int64_t>(std::floor(note_end / k_hop));
    f.grid = FrameGrid{.hop = Seconds{k_hop},
                       .start = SourceTime{} + Seconds{static_cast<double>(first) * k_hop},
                       .n_frames = last - first};
    f.center = to_cents(Hz{f0_hz(0.5 * (note_start + note_end))}, k_a4_standard_hz);
    for (std::int64_t i = 0; i < f.grid.n_frames; ++i) {
        const double cents = to_cents(Hz{f0_hz(f.frame_seconds(i))}, k_a4_standard_hz).value();
        f.drift.emplace_back(cents - f.center.value());
        f.mod.emplace_back(0.0);
    }
    return f;
}

double cents_between(double measured_hz, double true_hz) {
    return 1200.0 * std::log2(measured_hz / true_hz);
}

/// Largest and mean |frequency error| in cents over harmonics 1..max_k and the interior frames.
struct FreqError {
    double worst = 0.0;
    double mean = 0.0;
};

template <class F0Hz>
FreqError freq_error(const HarmonicTrack& h, const Fixture& f, const F0Hz& f0_hz, int max_k) {
    FreqError e;
    double sum = 0.0;
    int n = 0;
    for (std::int64_t i = 4; i < h.freq_hz.rows - 4; ++i) {
        const double t = f.frame_seconds(i);
        for (int k = 1; k <= max_k; ++k) {
            const auto idx = static_cast<std::size_t>(i * h.freq_hz.cols + (k - 1));
            const double err =
                std::abs(cents_between(static_cast<double>(h.freq_hz.data[idx]), k * f0_hz(t)));
            e.worst = std::max(e.worst, err);
            sum += err;
            ++n;
        }
    }
    e.mean = sum / n;
    return e;
}

float at(const FrameMatrix& m, std::int64_t row, int k) {
    return m.data[static_cast<std::size_t>(row * m.cols + (k - 1))];
}

}  // namespace

TEST_CASE("steady_harmonic_tones_are_measured_within_a_cent_off_the_bin_grid", "[partials]") {
    for (const double f0 : {220.0, 233.3, 330.7}) {
        const auto amps = harmonic_amps(8, 0.8);
        auto fx = make_fixture([&](double) { return f0; }, 0.2, 0.8, 1.0, amps);
        const auto h = analyse_partials(fx.mono, Hz{k_rate}, fx.curves(), {}).value();
        REQUIRE(validate(h).has_value());
        const auto err = freq_error(h, fx, [&](double) { return f0; }, 8);
        CHECK(err.worst < 1.0);
        for (int k = 1; k <= 8; ++k) {
            const auto a = static_cast<double>(at(h.amp_linear, 20, k));
            CHECK(std::abs(a / amps[static_cast<std::size_t>(k - 1)] - 1.0) < 0.03);
        }
    }
}

TEST_CASE("phase_is_the_cosine_phase_at_the_frame_centre", "[partials]") {
    const auto amps = harmonic_amps(6, 0.8);
    auto fx = make_fixture([](double) { return 247.0; }, 0.2, 0.8, 1.0, amps);
    const auto h = analyse_partials(fx.mono, Hz{k_rate}, fx.curves(), {}).value();
    for (const std::int64_t row : {10, 30, 60}) {
        double sum = 0.0;
        for (int k = 1; k <= 6; ++k) {
            sum += static_cast<double>(at(h.amp_linear, row, k)) *
                   std::cos(static_cast<double>(at(h.phase_rad, row, k)));
        }
        const auto centre = static_cast<std::size_t>(std::llround(fx.frame_seconds(row) * k_rate));
        CHECK(std::abs(sum - static_cast<double>(fx.mono[centre])) < 0.03);
    }
}

TEST_CASE("a_vibrato_tone_is_followed_within_three_cents", "[partials]") {
    const auto f0 = [](double t) {
        return 220.0 * std::pow(2.0, 40.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t) / 1200.0);
    };
    auto fx = make_fixture(f0, 0.2, 1.2, 1.5, harmonic_amps(10, 0.8));
    const auto h = analyse_partials(fx.mono, Hz{k_rate}, fx.curves(), {}).value();
    const auto err = freq_error(h, fx, f0, 10);
    CHECK(err.mean < 3.0);
    CHECK(err.worst < 6.0);
}

TEST_CASE("a_glide_is_followed_within_three_cents", "[partials]") {
    const auto f0 = [](double t) { return 200.0 * std::pow(2.0, t); };  // one octave per second
    auto fx = make_fixture(f0, 0.2, 0.8, 1.0, harmonic_amps(8, 0.8));
    const auto h = analyse_partials(fx.mono, Hz{k_rate}, fx.curves(), {}).value();
    const auto err = freq_error(h, fx, f0, 8);
    CHECK(err.mean < 3.0);
    CHECK(err.worst < 6.0);
}

TEST_CASE("harmonics_in_noise_stay_within_three_cents", "[partials]") {
    auto fx = make_fixture([](double) { return 196.0; }, 0.2, 0.8, 1.0, harmonic_amps(8, 0.8));
    add_white_noise(fx.mono, 20.0, 7);
    const auto h = analyse_partials(fx.mono, Hz{k_rate}, fx.curves(), {}).value();
    const auto err = freq_error(h, fx, [](double) { return 196.0; }, 5);
    CHECK(err.mean < 3.0);
}

TEST_CASE("harmonics_are_capped_by_the_count_and_by_the_nyquist_margin", "[partials]") {
    const double rate = 16000.0;
    std::vector<float> x = make_harmonic([](double) { return 395.0; }, 1.0, rate, {0.5, 0.25});
    Fixture fx;
    fx.grid =
        FrameGrid{.hop = Seconds{k_hop}, .start = SourceTime{} + Seconds{0.2}, .n_frames = 100};
    fx.center = to_cents(Hz{395.0}, k_a4_standard_hz);
    fx.drift.assign(100, Cents{0.0});
    fx.mod.assign(100, Cents{0.0});
    const auto h = analyse_partials(x, Hz{rate}, fx.curves(), {}).value();
    CHECK(h.freq_hz.cols == 18);  // floor(0.45 * 16000 / 395)
    for (std::int64_t i = 0; i < h.freq_hz.rows; ++i) {
        for (int k = 1; k <= 18; ++k) {
            // Inside the sieve window of the nominal harmonic: +-40 cents widened to whole bins,
            // plus half a bin of interpolation (the bin is about 20 Hz here).
            CHECK(std::abs(static_cast<double>(at(h.freq_hz, i, k)) - k * 395.0) <
                  k * 395.0 * 0.0234 + 50.0);
        }
    }
    PartialsParams few;
    few.max_harmonics = 5;
    CHECK(analyse_partials(x, Hz{rate}, fx.curves(), few).value().freq_hz.cols == 5);
}

TEST_CASE("unvoiced_frames_and_missing_signal_give_zero_amplitude_and_no_nan", "[partials]") {
    auto fx = make_fixture([](double) { return 220.0; }, 0.2, 0.8, 1.0, harmonic_amps(4, 0.8));
    fx.mod[10] = Cents{std::nan("")};  // unvoiced frame
    const auto h = analyse_partials(fx.mono, Hz{k_rate}, fx.curves(), {}).value();
    for (int k = 1; k <= 4; ++k) {
        CHECK(at(h.amp_linear, 10, k) == 0.0F);
    }
    for (const float v : h.freq_hz.data) {
        CHECK(std::isfinite(v));
    }
    for (const float v : h.amp_linear.data) {
        CHECK(std::isfinite(v));
        CHECK(v >= 0.0F);
    }
    for (const float v : h.phase_rad.data) {
        CHECK(std::isfinite(v));
    }
    // A note outside the recording: every frame is silent, the call still succeeds.
    std::vector<float> short_signal(2000, 0.0F);
    const auto h2 = analyse_partials(short_signal, Hz{k_rate}, fx.curves(), {}).value();
    for (const float v : h2.amp_linear.data) {
        CHECK(v == 0.0F);
    }
}

TEST_CASE("bad_input_is_rejected", "[partials]") {
    auto fx = make_fixture([](double) { return 220.0; }, 0.2, 0.8, 1.0, harmonic_amps(4, 0.8));
    const auto code = [&](std::span<const float> x, double rate, const NoteCurves& c,
                          const PartialsParams& p) {
        return analyse_partials(x, Hz{rate}, c, p).error().code;
    };
    CHECK(code({}, k_rate, fx.curves(), {}) == ErrorCode::invalid_argument);
    CHECK(code(fx.mono, 0.0, fx.curves(), {}) == ErrorCode::invalid_argument);
    PartialsParams p;
    p.max_harmonics = 0;
    CHECK(code(fx.mono, k_rate, fx.curves(), p) == ErrorCode::invalid_argument);
    p = {};
    p.window_periods = 2.0;
    CHECK(code(fx.mono, k_rate, fx.curves(), p) == ErrorCode::invalid_argument);
    p = {};
    p.fft_pad_factor = 2.0;
    CHECK(code(fx.mono, k_rate, fx.curves(), p) == ErrorCode::invalid_argument);
    p = {};
    p.search_halfwidth = Cents{5.0};
    CHECK(code(fx.mono, k_rate, fx.curves(), p) == ErrorCode::invalid_argument);
    NoteCurves c = fx.curves();
    c.pitch_mod = std::span<const Cents>(fx.mod).first(fx.mod.size() - 1);
    CHECK(code(fx.mono, k_rate, c, {}) == ErrorCode::invalid_argument);
    std::vector<Cents> all_nan(fx.drift.size(), Cents{std::nan("")});
    c = fx.curves();
    c.pitch_drift = all_nan;
    CHECK(code(fx.mono, k_rate, c, {}) == ErrorCode::invalid_argument);
}
