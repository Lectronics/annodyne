#include "core/analysis/orchestrator/analyse.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

using namespace anno;
using namespace anno::test;

namespace {

struct Tone {
    double f0_hz;
    double length_s;
    double amplitude;
};

Tone note(double f0_hz, double length_s, double amplitude) {
    return Tone{.f0_hz = f0_hz, .length_s = length_s, .amplitude = amplitude};
}

/// Tones separated by 100 ms of silence, starting at 0.3 s; returns the start times.
std::vector<float> melody(const std::vector<Tone>& tones, double rate,
                          std::vector<double>& starts) {
    double total = 0.3;
    for (const Tone& t : tones) {
        total += t.length_s + 0.1;
    }
    std::vector<float> x(static_cast<std::size_t>(std::llround(total * rate)), 0.0F);
    double at = 0.3;
    for (const Tone& t : tones) {
        const auto samples = make_harmonic([f = t.f0_hz](double) { return f; }, t.length_s, rate,
                                           harmonic_amps(6, t.amplitude));
        const auto first = static_cast<std::size_t>(std::llround(at * rate));
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const double g = std::min({1.0, static_cast<double>(i) / (0.001 * rate),
                                       static_cast<double>(samples.size() - i) / (0.005 * rate)});
            x[first + i] += static_cast<float>(g * samples[i]);
        }
        starts.push_back(at);
        at += t.length_s + 0.1;
    }
    return x;
}

AudioSource source_of(const std::vector<float>& mono, double rate, int channels = 1) {
    return AudioSource::from_buffer(to_buffer(mono, rate, channels));
}

double sec(SourceTime t) {
    return (t - SourceTime{}).value();
}

/// Every note of a result is finished: curves of grid length, finite loudness and pitch (ADR-0010).
void require_complete(const AnalysisResult& r) {
    REQUIRE(validate(r).has_value());
    for (const Note& n : r.notes) {
        INFO("note " << n.id.value);
        REQUIRE(n.grid.n_frames > 0);
        const auto frames = static_cast<std::size_t>(n.grid.n_frames);
        REQUIRE(n.pitch_drift.size() == frames);
        REQUIRE(n.pitch_mod.size() == frames);
        REQUIRE(n.amp.size() == frames);
        REQUIRE(std::isfinite(n.loudness.value()));
        REQUIRE(std::isfinite(n.pitch_target.target.value()));
        REQUIRE(std::isfinite(n.pitch_center.value()));
        REQUIRE(n.pitch_target.target.value() != 0.0);  // the placeholder was replaced
        REQUIRE(n.is_active);
        REQUIRE(n.voice == 0);
        REQUIRE(n.track_id == TrackId{"t1"});
        // Phase 4 tracks: partials, sound classes and envelope are on the note's grid.
        REQUIRE(n.harmonics.grid == n.grid);
        REQUIRE(n.harmonics.freq_hz.rows == n.grid.n_frames);
        REQUIRE(n.harmonics.freq_hz.cols >= 1);
        REQUIRE(n.noise.grid == n.grid);
        REQUIRE(n.noise.frame_class.size() == frames);
        REQUIRE(n.noise.band_energy_linear.rows == n.grid.n_frames);
        REQUIRE(n.envelope.grid == n.grid);
        REQUIRE(n.envelope.log_mag_db.rows == n.grid.n_frames);
        REQUIRE(n.envelope.freqs.size() >= 65);
    }
}

std::uint64_t hash_of(const AnalysisResult& r) {
    std::uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](double v) {
        h = (h ^ std::bit_cast<std::uint64_t>(v)) * 1099511628211ULL;
    };
    for (const Note& n : r.notes) {
        mix(sec(n.source_start));
        mix(sec(n.source_end));
        mix(n.pitch_target.target.value());
        mix(n.pitch_center.value());
        mix(n.loudness.value());
        for (const Cents c : n.pitch_drift) {
            mix(c.value());
        }
        for (const Decibels a : n.amp) {
            mix(a.value());
        }
    }
    for (const Cents c : r.pitch->f0) {
        mix(std::isnan(c.value()) ? -1.0 : c.value());
    }
    return h;
}

}  // namespace

TEST_CASE("a_three_note_melody_is_analysed_into_complete_notes", "[analyse]") {
    std::vector<double> starts;
    // the second note is 6 dB quieter than the first
    const auto x = melody({note(262.0, 0.5, 0.4), note(330.0, 0.5, 0.2), note(392.0, 0.6, 0.4)},
                          44100.0, starts);
    const auto result = analyse(source_of(x, 44100.0), AnalyseParams{}).value();
    require_complete(*result);
    REQUIRE(result->notes.size() == 3);
    const std::array<double, 3> hz{262.0, 330.0, 392.0};
    for (std::size_t i = 0; i < 3; ++i) {
        const Note& n = result->notes[i];
        CHECK(std::abs(sec(n.source_start) - starts[i]) <= 0.02);
        CHECK(std::abs(n.pitch_target.target.value() - to_cents(Hz{hz[i]}, Hz{440.0}).value()) <=
              10.0);
        CHECK(std::abs(n.pitch_center.value() - n.pitch_target.target.value()) <= 5.0);
        CHECK(n.confidence_prob > 0.5);
    }
    CHECK(std::abs((result->notes[0].loudness - result->notes[1].loudness).value() - 6.0) <= 1.0);
    CHECK(result->version == 1);
    CHECK(result->algorithm == Algorithm::melodic);
    CHECK(result->pitch.has_value());
    CHECK(result->onsets.times.size() >= 3);
}

TEST_CASE("a_short_note_is_analysed_too", "[analyse]") {
    std::vector<double> starts;
    const auto x = melody({note(262.0, 0.5, 0.4), note(330.0, 0.09, 0.4), note(392.0, 0.5, 0.4)},
                          44100.0, starts);
    const auto result = analyse(source_of(x, 44100.0), AnalyseParams{}).value();
    require_complete(*result);  // complete whether or not the short tone survived segmentation
    CHECK(result->notes.size() >= 2);
}

TEST_CASE("analysis_never_modifies_the_source_and_is_deterministic", "[analyse]") {
    std::vector<double> starts;
    const auto x = melody({note(220.0, 0.5, 0.3), note(247.0, 0.5, 0.3)}, 48000.0, starts);
    const AudioSource src = source_of(x, 48000.0);
    const AudioBuffer before = src.read(Samples{0}, src.frame_count()).value();
    const auto a = analyse(src, AnalyseParams{}).value();
    const auto b = analyse(src, AnalyseParams{}).value();
    const AudioBuffer after = src.read(Samples{0}, src.frame_count()).value();
    for (int c = 0; c < before.channel_count(); ++c) {
        CHECK(std::ranges::equal(before.channel(c), after.channel(c)));
    }
    CHECK(hash_of(*a) == hash_of(*b));
    CHECK(a->source_id == src.id());
}

TEST_CASE("analysis_works_at_other_rates_and_for_stereo", "[analyse]") {
    for (const double rate : {22050.0, 32000.0}) {
        std::vector<double> starts;
        const auto x = melody({note(262.0, 0.5, 0.4), note(330.0, 0.5, 0.4)}, rate, starts);
        const auto result =
            analyse(source_of(x, rate, rate == 22050.0 ? 1 : 2), AnalyseParams{}).value();
        require_complete(*result);
        REQUIRE(result->notes.size() == 2);
        CHECK(std::abs(sec(result->notes[1].source_start) - starts[1]) <= 0.02);
    }
}

TEST_CASE("silence_and_empty_sources_give_empty_valid_results", "[analyse]") {
    const auto silent =
        analyse(source_of(std::vector<float>(44100, 0.0F), 44100.0), AnalyseParams{}).value();
    CHECK(silent->notes.empty());
    CHECK(validate(*silent).has_value());
    const auto empty = analyse(source_of({}, 44100.0), AnalyseParams{}).value();
    CHECK(empty->notes.empty());
    CHECK(validate(*empty).has_value());
}

TEST_CASE("other_algorithms_and_bad_parameters_are_errors", "[analyse]") {
    const AudioSource src = source_of(make_sine(220.0, 0.5, 44100.0), 44100.0);
    AnalyseParams p;
    p.algorithm = Algorithm::universal;
    CHECK(analyse(src, p).error().code == ErrorCode::not_implemented);
    p = AnalyseParams{};
    p.pitch.min_f0 = Hz{-1.0};
    const auto bad = analyse(src, p);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::invalid_argument);
    CHECK(bad.error().message.find("pitch") != std::string::npos);  // names the stage
}

TEST_CASE("low_sample_rates_and_tiny_sources", "[analyse]") {
    for (const double rate : {8000.0, 16000.0}) {
        std::vector<double> starts;
        const auto x = melody({note(262.0, 0.5, 0.4), note(330.0, 0.5, 0.4)}, rate, starts);
        const auto result = analyse(source_of(x, rate), AnalyseParams{}).value();
        require_complete(*result);
        CHECK(result->notes.size() == 2);
    }
    const auto tiny =
        analyse(source_of(make_sine(220.0, 0.02, 44100.0), 44100.0), AnalyseParams{}).value();
    CHECK(validate(*tiny).has_value());
    CHECK(tiny->notes.empty());
    // below 4 kHz: refused, naming the limit
    const auto too_low = analyse(source_of(make_sine(100.0, 0.5, 3000.0), 3000.0), AnalyseParams{});
    REQUIRE_FALSE(too_low.has_value());
    CHECK(too_low.error().code == ErrorCode::invalid_argument);
}

TEST_CASE("notes_appear_only_where_the_recording_is_active", "[analyse]") {
    // Three tones with 100 ms gaps over a steady noise floor, and a long silent tail and head.
    std::vector<double> starts;
    const std::vector<Tone> tones{note(220.0, 0.6, 0.3), note(262.0, 0.6, 0.3),
                                  note(330.0, 0.6, 0.3)};
    auto x = melody(tones, 48000.0, starts);
    x.resize(x.size() + 48000, 0.0F);  // one more second of nothing
    const auto noise = make_noise(static_cast<double>(x.size()) / 48000.0, 48000.0, 0.003, 11);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += noise[i];
    }
    const auto r = analyse(source_of(x, 48000.0), AnalyseParams{}).value();
    require_complete(*r);
    REQUIRE(r->notes.size() == 3);
    for (std::size_t k = 0; k < 3; ++k) {
        CHECK(std::abs(sec(r->notes[k].source_start) - starts[k]) <= 0.02);
    }
    // Nothing is voiced in the silent head, the gaps or the tail.
    const double tail_start = starts.back() + 0.6 + 0.2;
    for (std::size_t i = 0; i < r->pitch->f0.size(); ++i) {
        const double t = static_cast<double>(i) * r->pitch->grid.hop.value();
        bool in_tone = false;
        for (std::size_t k = 0; k < 3; ++k) {
            in_tone = in_tone || (t >= starts[k] - 0.02 && t <= starts[k] + 0.6 + 0.03);
        }
        if (!in_tone && (t < 0.25 || t > tail_start)) {
            CHECK(!is_voiced(*r->pitch, i));
        }
    }
}

TEST_CASE("a_sung_trill_stays_one_note_and_is_an_ornament", "[analyse][pitch_target]") {
    constexpr double k_rate = 44100.0;
    // 6 Hz trill between A3 and B3 (about 200 cents), flat plateaus, 1.2 s, after 0.3 s of silence.
    const auto f0 = [](double t) { return std::fmod(t, 0.1667) < 0.0833 ? 220.0 : 246.94; };
    const auto tone = make_harmonic(f0, 1.2, k_rate, harmonic_amps(6, 0.3));
    std::vector<float> x(static_cast<std::size_t>(std::llround(2.0 * k_rate)), 0.0F);
    std::ranges::copy(tone, x.begin() + static_cast<std::ptrdiff_t>(0.3 * k_rate));
    const auto result = analyse(source_of(x, k_rate), {}).value();
    REQUIRE(!result->notes.empty());
    int ornaments = 0;
    for (const Note& n : result->notes) {
        ornaments += n.pitch_target.kind == PitchTargetKind::ornament ? 1 : 0;
    }
    INFO("notes " << result->notes.size());
    CHECK(result->notes.size() <= 2);
    CHECK(ornaments >= 1);
}

TEST_CASE("a_clean_tone_gets_harmonics_at_its_pitch_and_the_voiced_class", "[analyse]") {
    std::vector<double> starts;
    const auto x = melody({note(330.0, 0.8, 0.4)}, 44100.0, starts);
    const auto result = analyse(source_of(x, 44100.0), AnalyseParams{}).value();
    REQUIRE(result->notes.size() == 1);
    const Note& n = result->notes[0];
    const auto cols = static_cast<std::size_t>(n.harmonics.freq_hz.cols);
    const auto mid = static_cast<std::size_t>(n.grid.n_frames) / 2;
    const auto f1 = static_cast<double>(n.harmonics.freq_hz.data[mid * cols]);
    CHECK(std::abs(1200.0 * std::log2(f1 / 330.0)) < 3.0);
    CHECK(n.harmonics.amp_linear.data[mid * cols] > 0.0F);
    std::size_t voiced = 0;
    for (const FrameClass c : n.noise.frame_class) {
        voiced += c == FrameClass::voiced ? 1U : 0U;
    }
    CHECK(static_cast<double>(voiced) > 0.8 * static_cast<double>(n.noise.frame_class.size()));
}
