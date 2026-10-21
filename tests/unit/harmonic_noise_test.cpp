#include "core/analysis/harmonic_noise/harmonic_noise.h"
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
constexpr double k_f0 = 220.0;

enum class Kind { silence, vowel, sibilant, breath, closure, burst, vowel_plus_noise };

struct Segment {
    double begin_s;
    double end_s;
    Kind kind;
    double level = 0.0;  // kind-specific: noise amplitude, or the HNR in dB for vowel_plus_noise
};

/// A note whose 1.2 s timeline is built from segments; the pitch tracker evidence follows the kind.
struct Scene {
    std::vector<float> mono;
    FrameGrid grid;
    std::vector<Cents> drift;
    std::vector<Cents> mod;
    std::vector<double> voicing;
    std::vector<Cents> alternate;
    Cents center = to_cents(Hz{k_f0}, k_a4_standard_hz);

    [[nodiscard]] NoteCurves curves() const {
        return {.grid = grid, .pitch_center = center, .pitch_drift = drift, .pitch_mod = mod};
    }
    [[nodiscard]] PitchEvidence evidence() const {
        return {.voicing_prob = voicing, .alternate_f0 = alternate};
    }
    [[nodiscard]] static std::size_t frame_at(double t) {
        return static_cast<std::size_t>(std::llround((t - 0.1) / k_hop));
    }
};

std::vector<float> white(double seconds, double amp, std::uint64_t seed) {
    return make_noise(seconds, k_rate, amp, seed);  // amp = RMS
}

std::vector<float> running_mean(const std::vector<float>& x, std::size_t taps) {
    std::vector<float> y(x.size(), 0.0F);
    for (std::size_t i = 0; i < x.size(); ++i) {
        float acc = 0.0F;
        for (std::size_t k = 0; k < taps && k <= i; ++k) {
            acc += x[i - k];
        }
        y[i] = acc / static_cast<float>(taps);
    }
    return y;
}

Scene make_scene(const std::vector<Segment>& segments, double total_s = 1.3) {
    Scene sc;
    const auto n = static_cast<std::size_t>(std::llround(total_s * k_rate));
    sc.mono.assign(n, 0.0F);
    const auto vowel =
        make_harmonic([](double) { return k_f0; }, total_s, k_rate, harmonic_amps(8, 0.5));
    std::uint64_t seed = 1;
    for (const Segment& s : segments) {
        const auto a = static_cast<std::size_t>(std::llround(s.begin_s * k_rate));
        const auto b = static_cast<std::size_t>(std::llround(s.end_s * k_rate));
        const auto noise = white(s.end_s - s.begin_s + 0.001, s.level, seed++);
        // Breath: a doubly smoothed (steeper low-pass) noise, centroid well under 4 kHz.
        const auto smooth = running_mean(noise, 16);
        const auto soft = running_mean(smooth, 16);
        for (std::size_t i = a; i < b && i < n; ++i) {
            const float nz = noise[i - a];
            switch (s.kind) {
            case Kind::silence: break;
            case Kind::vowel: sc.mono[i] = vowel[i]; break;
            case Kind::sibilant: sc.mono[i] = nz - (i > a ? noise[i - a - 1] : 0.0F); break;
            case Kind::breath: sc.mono[i] = soft[i - a]; break;
            case Kind::closure: sc.mono[i] = smooth[i - a]; break;
            case Kind::burst: sc.mono[i] = nz; break;
            case Kind::vowel_plus_noise: sc.mono[i] = vowel[i] + nz; break;
            }
        }
    }
    const double first = 0.1;
    const double last = total_s - 0.1;
    sc.grid =
        FrameGrid{.hop = Seconds{k_hop},
                  .start = SourceTime{} + Seconds{first},
                  .n_frames = static_cast<std::int64_t>(std::llround((last - first) / k_hop))};
    sc.drift.assign(static_cast<std::size_t>(sc.grid.n_frames), Cents{0.0});
    sc.mod.assign(static_cast<std::size_t>(sc.grid.n_frames), Cents{0.0});
    sc.voicing.assign(static_cast<std::size_t>(sc.grid.n_frames), 0.0);
    sc.alternate.assign(static_cast<std::size_t>(sc.grid.n_frames), Cents{std::nan("")});
    for (std::size_t i = 0; i < sc.voicing.size(); ++i) {
        const double t = first + static_cast<double>(i) * k_hop;
        for (const Segment& s : segments) {
            if (t >= s.begin_s && t < s.end_s &&
                (s.kind == Kind::vowel || s.kind == Kind::vowel_plus_noise)) {
                sc.voicing[i] = 1.0;
            }
        }
    }
    return sc;
}

NoiseTrack run(const Scene& sc, const HarmonicNoiseParams& p = {}) {
    const auto h = analyse_partials(sc.mono, Hz{k_rate}, sc.curves(), {}).value();
    return split_harmonic_noise(sc.mono, Hz{k_rate}, sc.curves(), h, sc.evidence(), p).value();
}

/// Every frame whose centre lies in [from, to] has class `want`.
bool all_are(const NoiseTrack& n, double from, double to, FrameClass want) {
    for (std::size_t i = Scene::frame_at(from); i <= Scene::frame_at(to); ++i) {
        if (n.frame_class[i] != want) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("a_modal_vowel_with_sibilant_and_breath_gets_each_class", "[harmonic_noise]") {
    const auto sc =
        make_scene({{.begin_s = 0.1, .end_s = 0.5, .kind = Kind::vowel},
                    {.begin_s = 0.5, .end_s = 0.7, .kind = Kind::sibilant, .level = 0.05},
                    {.begin_s = 0.7, .end_s = 0.9, .kind = Kind::vowel},
                    {.begin_s = 0.9, .end_s = 1.1, .kind = Kind::breath, .level = 0.8},
                    {.begin_s = 1.1, .end_s = 1.2, .kind = Kind::vowel}});
    const auto n = run(sc);
    REQUIRE(validate(n).has_value());
    CHECK(n.frame_class.size() == static_cast<std::size_t>(sc.grid.n_frames));
    CHECK(all_are(n, 0.2, 0.45, FrameClass::voiced));
    CHECK(all_are(n, 0.55, 0.65, FrameClass::sibilant));
    CHECK(all_are(n, 0.75, 0.85, FrameClass::voiced));
    CHECK(all_are(n, 0.95, 1.05, FrameClass::breath));
}

TEST_CASE("digital_silence_is_silence", "[harmonic_noise]") {
    const auto sc = make_scene({{.begin_s = 0.1, .end_s = 0.5, .kind = Kind::vowel},
                                {.begin_s = 0.5, .end_s = 0.8, .kind = Kind::silence},
                                {.begin_s = 0.8, .end_s = 1.2, .kind = Kind::vowel}});
    const auto n = run(sc);
    CHECK(all_are(n, 0.55, 0.75, FrameClass::silence));
    CHECK(all_are(n, 0.2, 0.45, FrameClass::voiced));
}

TEST_CASE("a_burst_after_a_closure_is_a_plosive_even_though_it_is_broadband", "[harmonic_noise]") {
    const auto sc =
        make_scene({{.begin_s = 0.1, .end_s = 0.5, .kind = Kind::vowel},
                    {.begin_s = 0.5, .end_s = 0.58, .kind = Kind::closure, .level = 0.02},
                    {.begin_s = 0.58, .end_s = 0.593, .kind = Kind::burst, .level = 0.2},
                    {.begin_s = 0.593, .end_s = 0.9, .kind = Kind::vowel}});
    const auto n = run(sc);
    CHECK(n.frame_class[Scene::frame_at(0.585)] == FrameClass::plosive);
    CHECK(all_are(n, 0.2, 0.45, FrameClass::voiced));
    CHECK(all_are(n, 0.7, 0.85, FrameClass::voiced));
}

TEST_CASE("noise_on_a_vowel_makes_it_breathy_and_a_clean_vowel_stays_voiced", "[harmonic_noise]") {
    // The vowel's RMS is about 0.14, so noise at 0.14 RMS gives an HNR near 0 dB.
    const auto sc =
        make_scene({{.begin_s = 0.1, .end_s = 0.6, .kind = Kind::vowel},
                    {.begin_s = 0.6, .end_s = 1.2, .kind = Kind::vowel_plus_noise, .level = 0.14}});
    const auto n = run(sc);
    CHECK(all_are(n, 0.2, 0.55, FrameClass::voiced));
    CHECK(all_are(n, 0.7, 1.1, FrameClass::breathy));
}

TEST_CASE("an_octave_apart_second_path_marks_voiced_frames_rough", "[harmonic_noise]") {
    auto sc = make_scene({{.begin_s = 0.1, .end_s = 1.2, .kind = Kind::vowel}});
    for (std::size_t i = Scene::frame_at(0.5); i <= Scene::frame_at(0.8); ++i) {
        sc.alternate[i] = sc.center - Cents{1200.0};  // ratio 2
    }
    const auto n = run(sc);
    CHECK(all_are(n, 0.55, 0.75, FrameClass::rough));
    CHECK(all_are(n, 0.2, 0.45, FrameClass::voiced));
    sc.alternate[Scene::frame_at(0.3)] = sc.center - Cents{700.0};  // a fifth is not an octave
    CHECK(run(sc).frame_class[Scene::frame_at(0.3)] == FrameClass::voiced);
}

TEST_CASE("classes_do_not_depend_on_the_recording_level", "[harmonic_noise]") {
    auto sc = make_scene({{.begin_s = 0.1, .end_s = 0.5, .kind = Kind::vowel},
                          {.begin_s = 0.5, .end_s = 0.7, .kind = Kind::sibilant, .level = 0.05},
                          {.begin_s = 0.7, .end_s = 1.2, .kind = Kind::vowel}});
    const auto loud = run(sc);
    for (float& v : sc.mono) {
        v *= 0.1F;  // -20 dB
    }
    const auto quiet = run(sc);
    CHECK(quiet.frame_class == loud.frame_class);
}

TEST_CASE("an_isolated_one_frame_glitch_is_smoothed_but_a_burst_is_not", "[harmonic_noise]") {
    FrameMetrics m;
    const std::size_t n = 40;
    m.level_db.assign(n, -20.0);
    m.centroid_hz.assign(n, 1000.0);
    m.zero_crossing_rate.assign(n, 0.02);
    m.hnr_db.assign(n, 30.0);
    m.mvf_hz.assign(n, 8000.0);
    m.flux.assign(n, 0.0);
    m.hf_share.assign(n, 0.0);
    m.burst.assign(n, 0);
    m.octave_flag.assign(n, 0);
    m.hnr_db[10] = 2.0;  // one breathy-looking frame
    m.burst[25] = 1;     // one burst frame
    const std::vector<double> voicing(n, 1.0);
    const auto classes = classify_frames(m, voicing, Seconds{k_hop}, {}).value();
    CHECK(classes[10] == FrameClass::voiced);
    CHECK(classes[25] == FrameClass::plosive);
}

TEST_CASE("a_short_run_joins_its_longer_neighbour", "[harmonic_noise]") {
    FrameMetrics m;
    const std::size_t n = 40;
    m.level_db.assign(n, -20.0);
    m.centroid_hz.assign(n, 1000.0);
    m.zero_crossing_rate.assign(n, 0.02);
    m.hnr_db.assign(n, 30.0);
    m.mvf_hz.assign(n, 8000.0);
    m.flux.assign(n, 0.0);
    m.hf_share.assign(n, 0.0);
    m.burst.assign(n, 0);
    m.octave_flag.assign(n, 0);
    for (std::size_t i = 15; i < 18; ++i) {  // 3 frames = 15 ms < min_segment
        m.hnr_db[i] = 2.0;
    }
    HarmonicNoiseParams p;
    p.median_frames = 1;  // isolate the min_segment rule
    const std::vector<double> voicing(n, 1.0);
    const auto classes = classify_frames(m, voicing, Seconds{k_hop}, p).value();
    for (std::size_t i = 15; i < 18; ++i) {
        CHECK(classes[i] == FrameClass::voiced);
    }
    for (std::size_t i = 15; i < 18; ++i) {
        m.hnr_db[i] = 2.0;
    }
    m.hnr_db[18] = 2.0;  // now 4 frames = 20 ms: kept
    m.hnr_db[19] = 2.0;
    const auto kept = classify_frames(m, voicing, Seconds{k_hop}, p).value();
    CHECK(kept[16] == FrameClass::breathy);
}

TEST_CASE("the_noise_band_power_is_finite_non_negative_and_sized_frames_by_bands",
          "[harmonic_noise]") {
    const auto sc = make_scene({{.begin_s = 0.1, .end_s = 1.2, .kind = Kind::vowel}});
    const auto n = run(sc);
    CHECK(n.band_energy_linear.rows == sc.grid.n_frames);
    CHECK(n.band_energy_linear.cols == 32);
    for (const float v : n.band_energy_linear.data) {
        CHECK(std::isfinite(v));
        CHECK(v >= 0.0F);
    }
    // A clean tone leaves little residual: far below its own power (0.1 at this level).
    double total = 0.0;
    for (std::size_t b = 0; b < 32; ++b) {
        total += static_cast<double>(n.band_energy_linear.data[Scene::frame_at(0.6) * 32 + b]);
    }
    CHECK(total < 0.01);
}

TEST_CASE("bad_input_is_rejected", "[harmonic_noise]") {
    const auto sc = make_scene({{.begin_s = 0.1, .end_s = 1.2, .kind = Kind::vowel}});
    const auto h = analyse_partials(sc.mono, Hz{k_rate}, sc.curves(), {}).value();
    const auto code = [&](const PitchEvidence& e, const HarmonicNoiseParams& p,
                          double rate = k_rate) {
        return split_harmonic_noise(sc.mono, Hz{rate}, sc.curves(), h, e, p).error().code;
    };
    HarmonicNoiseParams p;
    p.n_bands = 4;
    CHECK(code(sc.evidence(), p) == ErrorCode::invalid_argument);
    p = {};
    p.median_frames = 4;
    CHECK(code(sc.evidence(), p) == ErrorCode::invalid_argument);
    p = {};
    p.octave_ratio_min = 3.0;
    CHECK(code(sc.evidence(), p) == ErrorCode::invalid_argument);
    CHECK(code(sc.evidence(), {}, 0.0) == ErrorCode::invalid_argument);
    PitchEvidence short_voicing{.voicing_prob = std::span<const double>(sc.voicing).first(3),
                                .alternate_f0 = {}};
    CHECK(code(short_voicing, {}) == ErrorCode::invalid_argument);
    std::vector<double> bad = sc.voicing;
    bad[2] = 1.5;
    CHECK(code({.voicing_prob = bad, .alternate_f0 = {}}, {}) == ErrorCode::invalid_argument);
    PitchEvidence short_alt{.voicing_prob = sc.voicing,
                            .alternate_f0 = std::span<const Cents>(sc.alternate).first(3)};
    CHECK(code(short_alt, {}) == ErrorCode::invalid_argument);
}

TEST_CASE("rumble_below_the_voice_does_not_lower_the_maximum_voiced_frequency",
          "[harmonic_noise]") {
    // Real recordings carry low-frequency rumble under the first harmonic; it is not part of the
    // voiced range (found on the Vocadito corpus: it made 10 % of clean frames breathy).
    auto sc = make_scene({{.begin_s = 0.1, .end_s = 1.2, .kind = Kind::vowel}});
    for (std::size_t i = 0; i < sc.mono.size(); ++i) {
        sc.mono[i] += 0.1F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 38.0 *
                                                         static_cast<double>(i) / k_rate));
    }
    const auto h = analyse_partials(sc.mono, Hz{k_rate}, sc.curves(), {}).value();
    const auto m = measure_frames(sc.mono, Hz{k_rate}, sc.curves(), h, sc.evidence(), {}).value();
    const auto mid = Scene::frame_at(0.6);
    CHECK(m.mvf_hz[mid] > 1500.0);
    CHECK(run(sc).frame_class[mid] == FrameClass::voiced);
}
