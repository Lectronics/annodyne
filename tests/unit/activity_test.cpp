#include "core/analysis/activity/activity.h"
#include "support/synth_signals.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;
constexpr double k_hop = 0.005;

struct Burst {
    double start_s;
    double end_s;
    double amplitude;
};

/// Seeded white noise of RMS `noise` plus a 220 Hz tone in each burst (with `ramp_s` linear
/// fades at both ends).
MonoAudio scene(double seconds, double noise, const std::vector<Burst>& bursts, double ramp_s = 0.0,
                std::uint64_t seed = 7) {
    std::vector<float> x = make_noise(seconds, k_rate, noise, seed);
    for (const Burst& b : bursts) {
        const auto first = static_cast<std::size_t>(b.start_s * k_rate);
        const auto last = std::min(x.size(), static_cast<std::size_t>(b.end_s * k_rate));
        for (std::size_t i = first; i < last; ++i) {
            const double t = static_cast<double>(i) / k_rate;
            double env = 1.0;
            if (ramp_s > 0.0) {
                env = std::min({1.0, (t - b.start_s) / ramp_s, (b.end_s - t) / ramp_s});
            }
            x[i] += static_cast<float>(b.amplitude * env *
                                       std::sin(2.0 * std::numbers::pi * 220.0 * t));
        }
    }
    return MonoAudio{.samples = std::move(x), .sample_rate = Hz{k_rate}};
}

FrameGrid grid_for(const MonoAudio& m) {
    const double seconds = static_cast<double>(m.samples.size()) / m.sample_rate.value();
    FrameGrid g;
    g.hop = Seconds{k_hop};
    g.start = SourceTime{};
    g.n_frames = static_cast<std::int64_t>(std::floor(seconds / k_hop)) + 1;
    return g;
}

ActivityTrack detect(const MonoAudio& m, const ActivityParams& p = {}) {
    return detect_activity(m, grid_for(m), p).value();
}

double time_of(std::size_t frame) {
    return static_cast<double>(frame) * k_hop;
}

/// True where the active flag should be, per the burst list.
bool inside(double t, const std::vector<Burst>& bursts) {
    return std::ranges::any_of(bursts,
                               [t](const Burst& b) { return t >= b.start_s && t < b.end_s; });
}

/// Frames further than `margin_s` from every burst edge must agree with the truth.
std::size_t disagreements(const ActivityTrack& a, const std::vector<Burst>& bursts,
                          double margin_s) {
    std::size_t bad = 0;
    for (std::size_t i = 0; i < a.activity.size(); ++i) {
        const double t = time_of(i);
        bool near_edge = false;
        for (const Burst& b : bursts) {
            near_edge = near_edge || std::abs(t - b.start_s) <= margin_s ||
                        std::abs(t - b.end_s) <= margin_s;
        }
        if (!near_edge && is_active(a, i) != inside(t, bursts)) {
            ++bad;
        }
    }
    return bad;
}

std::size_t transitions(const ActivityTrack& a, std::size_t first, std::size_t last) {
    std::size_t n = 0;
    for (std::size_t i = first + 1; i < last; ++i) {
        n += (a.activity[i] != a.activity[i - 1]) ? 1U : 0U;
    }
    return n;
}

constexpr double k_noise = 0.002;  // -54 dBFS RMS

double dbfs(double rms_linear) {
    return 20.0 * std::log10(rms_linear);
}

}  // namespace

TEST_CASE("tone_bursts_in_noise_are_labelled_where_they_are", "[activity]") {
    const std::vector<Burst> bursts{{.start_s = 0.5, .end_s = 1.2, .amplitude = 0.1},
                                    {.start_s = 1.6, .end_s = 2.4, .amplitude = 0.1}};
    const auto m = scene(3.0, k_noise, bursts);
    const auto a = detect(m);
    REQUIRE(a.activity.size() == a.level.size());
    CHECK(a.activity.size() == static_cast<std::size_t>(grid_for(m).n_frames));
    CHECK(disagreements(a, bursts, tolerance::k_activity_edge.value()) == 0);
    CHECK(std::abs(a.noise_floor.value() - dbfs(k_noise)) <= tolerance::k_activity_floor.value());
    CHECK(validate(a.grid).has_value());
}

TEST_CASE("a_recording_without_silence_is_all_active", "[activity]") {
    // Noise only, and a held note over noise: no frame is a clear silence.
    for (const auto& m :
         {scene(2.0, k_noise, {}),
          scene(2.0, k_noise, {{.start_s = 0.0, .end_s = 2.0, .amplitude = 0.1}})}) {
        const auto a = detect(m);
        for (std::size_t i = 0; i < a.activity.size(); ++i) {
            REQUIRE(is_active(a, i));
        }
    }
}

TEST_CASE("digital_silence_is_silent_and_does_not_hide_the_noise_floor", "[activity]") {
    const MonoAudio zeros{.samples = std::vector<float>(44100, 0.0F), .sample_rate = Hz{k_rate}};
    for (const FrameActivity f : detect(zeros).activity) {
        REQUIRE(f == FrameActivity::silent);
    }
    // 1 s of exact zeros, then noise with a burst: the zeros are silent, and the floor is the noise
    // (not -120 dB), so the noise-only stretch is still told apart from the burst.
    auto m = scene(3.0, k_noise, {{.start_s = 2.0, .end_s = 2.6, .amplitude = 0.1}});
    std::fill(m.samples.begin(), m.samples.begin() + 44100, 0.0F);
    const auto a = detect(m);
    CHECK(!is_active(a, 100));  // 0.5 s: zeros
    CHECK(!is_active(a, 300));  // 1.5 s: noise only
    CHECK(is_active(a, 450));   // 2.25 s: burst
    CHECK(std::abs(a.noise_floor.value() - dbfs(k_noise)) <= tolerance::k_activity_floor.value());
}

TEST_CASE("short_dips_stay_active_and_real_gaps_do_not", "[activity]") {
    const auto closure = scene(3.0, k_noise,
                               {{.start_s = 0.5, .end_s = 1.0, .amplitude = 0.1},
                                {.start_s = 1.08, .end_s = 1.6, .amplitude = 0.1},  // 80 ms dip
                                {.start_s = 1.8, .end_s = 2.2, .amplitude = 0.1},   // 200 ms gap
                                {.start_s = 2.4, .end_s = 2.9, .amplitude = 0.1}});
    const auto a = detect(closure);
    for (std::size_t i = 205; i < 215; ++i) {  // 1.025-1.075 s, inside the 80 ms dip
        CHECK(is_active(a, i));
    }
    for (std::size_t i = 334; i < 356; ++i) {  // 1.67-1.78 s, inside the 200 ms gap
        CHECK(!is_active(a, i));
    }
}

TEST_CASE("a_short_click_is_silent", "[activity]") {
    auto m = scene(2.0, k_noise, {{.start_s = 0.6, .end_s = 1.4, .amplitude = 0.1}});
    const auto at = static_cast<std::size_t>(1.7 * k_rate);
    for (std::size_t i = at; i < at + 88; ++i) {  // 2 ms
        m.samples[i] += 0.5F;
    }
    const auto a = detect(m);
    for (std::size_t i = 330; i < 350; ++i) {  // 1.65-1.75 s
        CHECK(!is_active(a, i));
    }
    CHECK(is_active(a, 200));  // the burst is untouched
}

TEST_CASE("a_decaying_tail_ends_the_activity_once", "[activity]") {
    // 220 Hz decaying 60 dB over 1.5 s into noise: one switch off, no chatter at the threshold.
    auto m = scene(3.0, k_noise, {});
    for (std::size_t i = 0; i < static_cast<std::size_t>(1.5 * k_rate); ++i) {
        const double t = static_cast<double>(i) / k_rate;
        m.samples[i] += static_cast<float>(0.2 * std::pow(10.0, -3.0 * t / 1.5) *
                                           std::sin(2.0 * std::numbers::pi * 220.0 * t));
    }
    const auto a = detect(m);
    CHECK(is_active(a, 10));
    CHECK(!is_active(a, 550));
    CHECK(transitions(a, 0, a.activity.size()) == 1);
}

TEST_CASE("fades_start_and_end_the_activity_inside_the_fade", "[activity]") {
    const auto m = scene(3.0, k_noise, {{.start_s = 1.0, .end_s = 2.0, .amplitude = 0.2}}, 0.1);
    const auto a = detect(m);
    CHECK(transitions(a, 0, a.activity.size()) == 2);
    std::size_t first = 0;
    std::size_t last = 0;
    for (std::size_t i = 0; i < a.activity.size(); ++i) {
        if (is_active(a, i)) {
            first = (first == 0) ? i : first;
            last = i;
        }
    }
    CHECK(time_of(first) > 0.98);  // the 25 ms window sees the fade slightly early
    CHECK(time_of(first) < 1.1);
    CHECK(time_of(last) > 1.9);
    CHECK(time_of(last) < 2.02);
}

TEST_CASE("a_take_with_little_silence_still_finds_its_floor", "[activity]") {
    // 20 s of singing at two levels (loud, and a soft phrase 20 dB lower) with only 3 % silence:
    // the floor must be the silence, so the soft phrase stays active.
    std::vector<Burst> bursts{{.start_s = 0.0, .end_s = 9.0, .amplitude = 0.2},
                              {.start_s = 9.3, .end_s = 14.0, .amplitude = 0.02},
                              {.start_s = 14.3, .end_s = 20.0, .amplitude = 0.2}};
    const auto m = scene(20.0, k_noise, bursts);
    const auto a = detect(m);
    CHECK(std::abs(a.noise_floor.value() - dbfs(k_noise)) <= tolerance::k_activity_floor.value());
    CHECK(is_active(a, static_cast<std::size_t>(12.0 / k_hop)));   // the soft phrase
    CHECK(!is_active(a, static_cast<std::size_t>(9.15 / k_hop)));  // the 300 ms gap
}

TEST_CASE("activity_works_at_any_rate_and_with_short_inputs", "[activity]") {
    for (const double rate : {4000.0, 22050.0, 48000.0}) {
        std::vector<float> x = make_noise(2.0, rate, k_noise, 3);
        const std::vector<float> tone = make_sine(220.0, 0.6, rate, 0.1);
        for (std::size_t i = 0; i < tone.size(); ++i) {
            x[static_cast<std::size_t>(0.7 * rate) + i] += tone[i];
        }
        const MonoAudio m{.samples = x, .sample_rate = Hz{rate}};
        const auto a = detect(m);
        INFO("rate " << rate);
        CHECK(is_active(a, 200));  // 1.0 s: in the tone
        CHECK(!is_active(a, 50));  // 0.25 s: noise
    }
    const MonoAudio tiny{.samples = std::vector<float>(10, 0.1F), .sample_rate = Hz{k_rate}};
    CHECK(detect_activity(tiny, grid_for(tiny), {}).has_value());
    const MonoAudio empty{.samples = {}, .sample_rate = Hz{k_rate}};
    FrameGrid none;
    none.n_frames = 0;
    const auto e = detect_activity(empty, none, {}).value();
    CHECK(e.activity.empty());
}

TEST_CASE("activity_arguments_are_validated", "[activity]") {
    const auto m = scene(1.0, k_noise, {});
    const FrameGrid g = grid_for(m);
    const auto bad = [&](auto mutate) {
        ActivityParams p;
        mutate(p);
        return detect_activity(m, g, p).error().code == ErrorCode::invalid_argument;
    };
    CHECK(bad([](ActivityParams& p) { p.window = Seconds{0.0}; }));
    CHECK(bad([](ActivityParams& p) { p.highpass = Hz{-1.0}; }));
    CHECK(bad([](ActivityParams& p) { p.highpass = Hz{30000.0}; }));
    CHECK(bad([](ActivityParams& p) { p.floor_mass_prob = 0.0; }));
    CHECK(bad([](ActivityParams& p) { p.floor_mass_prob = 0.6; }));
    CHECK(bad([](ActivityParams& p) { p.floor_band = Decibels{0.0}; }));
    CHECK(bad([](ActivityParams& p) { p.range_percentile_prob = 0.3; }));
    CHECK(bad([](ActivityParams& p) { p.on_margin = Decibels{3.0}; }));  // below off_margin
    CHECK(bad([](ActivityParams& p) { p.off_margin = Decibels{std::nan("")}; }));
    CHECK(bad([](ActivityParams& p) { p.min_active = Seconds{-0.1}; }));
    CHECK(bad([](ActivityParams& p) { p.max_dip = Seconds{std::nan("")}; }));
    CHECK(bad([](ActivityParams& p) { p.min_range = Decibels{-1.0}; }));
    FrameGrid broken = g;
    broken.hop = Seconds{0.0};
    CHECK(detect_activity(m, broken, {}).error().code == ErrorCode::invalid_argument);
    const MonoAudio no_rate{.samples = m.samples, .sample_rate = Hz{0.0}};
    CHECK(detect_activity(no_rate, g, {}).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("activity_is_deterministic_and_leaves_the_signal_alone", "[activity]") {
    const auto m = scene(2.0, k_noise, {{.start_s = 0.5, .end_s = 1.2, .amplitude = 0.1}});
    const auto copy = m.samples;
    const auto a = detect(m);
    const auto b = detect(m);
    CHECK(a.activity == b.activity);
    CHECK(a.level == b.level);
    CHECK(a.noise_floor == b.noise_floor);
    CHECK(m.samples == copy);
}

namespace {

PitchTrack voiced_track(std::size_t n) {
    PitchTrack t;
    t.grid.hop = Seconds{k_hop};
    t.grid.n_frames = static_cast<std::int64_t>(n);
    t.f0.assign(n, Cents{6900.0});
    t.confidence_prob.assign(n, 0.8);
    t.voicing_prob.assign(n, 0.9);
    t.alternates.assign(2, std::vector<Cents>(n, Cents{7200.0}));
    return t;
}

ActivityTrack alternating(const FrameGrid& g) {
    ActivityTrack a;
    a.grid = g;
    a.level.assign(static_cast<std::size_t>(g.n_frames), Decibels{-30.0});
    a.activity.assign(static_cast<std::size_t>(g.n_frames), FrameActivity::active);
    for (std::size_t i = 0; i < a.activity.size(); i += 3) {
        a.activity[i] = FrameActivity::silent;
    }
    return a;
}

}  // namespace

TEST_CASE("gating_unvoices_exactly_the_silent_frames_and_keeps_the_input", "[activity]") {
    const PitchTrack in = voiced_track(30);
    const PitchTrack before = voiced_track(30);  // an independent copy of the input to compare with
    const ActivityTrack a = alternating(in.grid);
    const PitchTrack out = gate_pitch(in, a).value();
    CHECK(in.f0 == before.f0);
    CHECK(in.voicing_prob == before.voicing_prob);
    REQUIRE(out.f0.size() == in.f0.size());
    for (std::size_t i = 0; i < out.f0.size(); ++i) {
        if (is_active(a, i)) {
            CHECK(out.f0[i] == in.f0[i]);
            CHECK(out.voicing_prob[i] == in.voicing_prob[i]);
            CHECK(out.confidence_prob[i] == in.confidence_prob[i]);
            CHECK(out.alternates[1][i] == in.alternates[1][i]);
        } else {
            CHECK(std::isnan(out.f0[i].value()));
            CHECK(out.voicing_prob[i] == 0.0);
            CHECK(out.confidence_prob[i] == 1.0);
            CHECK(std::isnan(out.alternates[0][i].value()));
            CHECK(std::isnan(out.alternates[1][i].value()));
            CHECK(!is_voiced(out, i));
        }
    }
    CHECK(validate(out).has_value());
}

TEST_CASE("gating_rejects_mismatched_grids_and_arrays", "[activity]") {
    const PitchTrack t = voiced_track(30);
    ActivityTrack other = alternating(t.grid);
    other.grid.n_frames = 29;
    other.activity.pop_back();
    other.level.pop_back();
    CHECK(gate_pitch(t, other).error().code == ErrorCode::invalid_argument);
    ActivityTrack short_array = alternating(t.grid);
    short_array.activity.pop_back();
    CHECK(gate_pitch(t, short_array).error().code == ErrorCode::invalid_argument);
    PitchTrack bad = t;
    bad.voicing_prob.pop_back();
    CHECK(gate_pitch(bad, alternating(t.grid)).error().code == ErrorCode::invalid_argument);
}
