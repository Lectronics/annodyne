#include "core/dsp/interp_curve.h"
#include "core/dsp/k_weight.h"
#include "core/dsp/mono.h"
#include "core/dsp/resample.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

double db(double ratio) {
    return 20.0 * std::log10(ratio);
}

SourceTime t(double seconds) {
    return SourceTime::unchecked_from_origin(Seconds{seconds});
}

}  // namespace

TEST_CASE("resampling_keeps_a_tones_frequency_level_and_length", "[dsp]") {
    struct Case {
        double from;
        double to;
    };
    for (const Case c :
         {Case{.from = 48000.0, .to = 16000.0}, Case{.from = 44100.0, .to = 48000.0},
          Case{.from = 48000.0, .to = 24000.0}, Case{.from = 16000.0, .to = 48000.0}}) {
        const auto x = make_sine(1000.0, 1.5, c.from);
        const auto y = resample(x, Hz{c.from}, Hz{c.to}, {}).value();
        CHECK(std::abs(static_cast<double>(y.size()) -
                       static_cast<double>(x.size()) * c.to / c.from) <= 1.0);
        // Measure away from the edges (the kernel is truncated there).
        const std::vector<float> middle(y.begin() + static_cast<std::ptrdiff_t>(y.size() / 4),
                                        y.end() - static_cast<std::ptrdiff_t>(y.size() / 4));
        CHECK(std::abs(zero_crossing_hz(middle, c.to) - 1000.0) / 1000.0 <
              tolerance::k_resample_freq_rel.value());
        CHECK(std::abs(db(rms(middle) / rms(x, x.size() / 4, x.size() / 2))) <
              tolerance::k_resample_level.value());
    }
}

TEST_CASE("resampling_rejects_bad_arguments_and_handles_empty_input", "[dsp]") {
    const std::vector<float> x(100, 0.1F);
    CHECK(resample(x, Hz{0.0}, Hz{16000.0}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(resample(x, Hz{48000.0}, Hz{-1.0}, {}).error().code == ErrorCode::invalid_argument);
    CHECK(resample(x, Hz{48000.0}, Hz{16000.0}, {.taps_per_side = 4}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(resample({}, Hz{48000.0}, Hz{16000.0}, {}).value().empty());
}

TEST_CASE("k_weighting_gains_0_691_db_at_1khz_attenuates_lows_and_lifts_highs", "[dsp]") {
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        const auto one_khz = make_sine(1000.0, 1.0, rate);
        const auto k = k_weight(one_khz, Hz{rate});
        REQUIRE(k.size() == one_khz.size());
        // skip the filter's start-up transient
        // BS.1770's filter has +0.691 dB at 1 kHz: where the -0.691 in the LUFS formula comes from.
        CHECK(std::abs(db(rms(k, k.size() / 2, k.size() / 4) /
                          rms(one_khz, k.size() / 2, k.size() / 4)) -
                       0.691) < tolerance::k_k_weight_1khz.value());
        const auto low = make_sine(38.0, 2.0, rate);
        const auto kl = k_weight(low, Hz{rate});
        const double low_gain =
            db(rms(kl, kl.size() / 2, kl.size() / 4) / rms(low, kl.size() / 2, kl.size() / 4));
        CHECK(low_gain < -5.0);  // the 38 Hz high-pass has Q = 0.5: -6 dB at its corner
        CHECK(low_gain > -7.0);
        const auto high = make_sine(10000.0, 1.0, rate);
        const auto kh = k_weight(high, Hz{rate});
        CHECK(db(rms(kh, kh.size() / 2, kh.size() / 4) / rms(high, kh.size() / 2, kh.size() / 4)) >
              3.0);  // shelf ~ +4 dB
    }
}

TEST_CASE("a_mono_1khz_sine_measures_its_dbfs_minus_3_01_lufs", "[dsp]") {
    // BS.1770: a full-scale sine in one channel reads -3.01 LUFS, so a mono sine of amplitude a
    // reads 20 log10(a) - 3.01 LUFS. Amplitude 10^((-23 + 3.01) / 20) must read -23 LUFS.
    const double a = std::pow(10.0, (-23.0 + 3.0103) / 20.0);
    const auto x = make_sine(1000.0, 2.0, 48000.0, a);
    const auto k = k_weight(x, Hz{48000.0});
    const double rms_k = rms(k, k.size() / 2, k.size() / 4);
    const double lufs = -0.691 + 10.0 * std::log10(rms_k * rms_k);
    CHECK(std::abs(lufs - (-23.0)) < tolerance::k_k_weight_1khz.value());
}

TEST_CASE("interp_curve_is_exact_where_the_interpolator_is", "[dsp]") {
    const FrameGrid grid{.hop = Seconds{0.5}, .start = t(1.0), .n_frames = 6};
    std::vector<double> line(6);
    std::vector<double> cubic_data(6);
    for (int i = 0; i < 6; ++i) {
        line[static_cast<std::size_t>(i)] = 3.0 + 2.0 * i;  // v = 3 + 2 i
        const auto x = static_cast<double>(i);
        cubic_data[static_cast<std::size_t>(i)] = 1.0 - 2.0 * x + 0.5 * x * x + 0.1 * x * x * x;
    }
    const std::vector<SourceTime> times{t(1.0), t(1.25), t(1.6), t(2.3), t(3.0), t(3.4)};
    const auto lin = interp_curve(line, grid, times, InterpKind::linear);
    const auto cub = interp_curve(cubic_data, grid, times, InterpKind::cubic);
    for (std::size_t k = 0; k < times.size(); ++k) {
        const double pos = (times[k].unchecked_since_origin().value() - 1.0) / 0.5;
        CHECK(std::abs(lin[k] - (3.0 + 2.0 * pos)) < tolerance::k_interp_exact_abs);
        CHECK(std::abs(cub[k] - (1.0 - 2.0 * pos + 0.5 * pos * pos + 0.1 * pos * pos * pos)) <
              tolerance::k_interp_exact_abs);
    }
}

TEST_CASE("interp_curve_holds_clamps_and_propagates_nan", "[dsp]") {
    const FrameGrid grid{.hop = Seconds{1.0}, .start = t(0.0), .n_frames = 3};
    const std::vector<double> v{10.0, 20.0, 40.0};
    const std::vector<SourceTime> times{t(-5.0), t(0.0), t(0.9), t(1.0), t(1.99), t(2.0), t(9.0)};
    const auto hold = interp_curve(v, grid, times, InterpKind::hold);
    CHECK(hold == std::vector<double>{10.0, 10.0, 10.0, 20.0, 20.0, 40.0, 40.0});
    const auto lin = interp_curve(v, grid, times, InterpKind::linear);
    CHECK(lin.front() == 10.0);
    CHECK(lin.back() == 40.0);
    CHECK(std::abs(lin[4] - (20.0 + 0.99 * 20.0)) < 1e-9);
    const std::vector<double> gap{10.0, std::nan(""), 40.0};
    const auto filled =
        interp_curve(gap, grid, std::vector<SourceTime>{t(0.5), t(1.5)}, InterpKind::linear);
    CHECK(std::isnan(filled[0]));
    CHECK(std::isnan(filled[1]));
    CHECK(
        std::isnan(interp_curve({}, grid, std::vector<SourceTime>{t(0.0)}, InterpKind::linear)[0]));
}

TEST_CASE("read_mono_mixes_channels_and_leaves_the_source_alone", "[dsp]") {
    const auto left = make_sine(300.0, 0.1, 8000.0);
    AudioBuffer stereo =
        AudioBuffer::create(2, Samples{static_cast<std::int64_t>(left.size())}, Hz{8000.0}).value();
    for (std::size_t i = 0; i < left.size(); ++i) {
        stereo.channel(0)[i] = left[i];
        stereo.channel(1)[i] = -left[i];  // antiphase: the mean cancels
    }
    const AudioSource source = AudioSource::from_buffer(stereo);
    const auto mono = read_mono(source).value();
    CHECK(mono.sample_rate == Hz{8000.0});
    CHECK(mono.samples.size() == left.size());
    for (const float v : mono.samples) {
        REQUIRE(v == 0.0F);
    }
    const auto same = read_mono(AudioSource::from_buffer(to_buffer(left, 8000.0, 2))).value();
    CHECK(same.samples == left);
    const auto empty =
        read_mono(AudioSource::from_buffer(AudioBuffer::create(1, Samples{0}, Hz{8000.0}).value()))
            .value();
    CHECK(empty.samples.empty());
}
