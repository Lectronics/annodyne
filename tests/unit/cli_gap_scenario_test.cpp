#include "cli/commands/render.h"
#include "core/io/wav.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <fstream>
#include <random>

using namespace anno;
using namespace anno::cli;
using namespace anno::test;
namespace fs = std::filesystem;

namespace {

constexpr double k_rate = 44100.0;

/// Room noise (-50 dBFS) all through, a tone "a" at 0.5-1.2 s, a high-passed hiss at 1.5-1.65 s
/// (a fricative in the pause) and a tone "b" at 2.0-2.7 s, 3.2 s long, in a fresh temp folder.
struct Fixture {
    fs::path dir;
    std::vector<float> samples;

    Fixture() {
        std::random_device entropy;
        dir = fs::temp_directory_path() / ("anno_cli_gap_" + std::to_string(entropy()));
        fs::create_directories(dir);
        samples = make_noise(3.2, k_rate, std::pow(10.0, -50.0 / 20.0), 17);
        const auto tone = [&](double f0, double start, double length) {
            const auto s =
                make_harmonic([f0](double) { return f0; }, length, k_rate, harmonic_amps(6, 0.3));
            const auto first = static_cast<std::size_t>(std::llround(start * k_rate));
            for (std::size_t i = 0; i < s.size(); ++i) {
                const double g = std::min({1.0, static_cast<double>(i) / 440.0,
                                           static_cast<double>(s.size() - i) / 440.0});
                samples[first + i] += static_cast<float>(g * s[i]);
            }
        };
        tone(262.0, 0.5, 0.7);
        tone(330.0, 2.0, 0.7);
        const std::vector<float> hiss = make_noise(0.15, k_rate, 0.04, 23);
        const auto at = static_cast<std::size_t>(1.5 * k_rate);
        for (std::size_t i = 1; i < hiss.size(); ++i) {
            samples[at + i] += hiss[i] - hiss[i - 1];  // first difference: high-passed
        }
        REQUIRE(io::write_wav(dir / "gap.wav", to_buffer(samples, k_rate)).has_value());
    }
    ~Fixture() { fs::remove_all(dir); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] fs::path scenario(const std::string& edits) const {
        const fs::path path = dir / "scenario.json";
        std::ofstream out{path};
        out << R"({"name": "gate", "source": "gap.wav", "notes": [
                  {"id": "a", "start_s": 0.5, "end_s": 1.2},
                  {"id": "b", "start_s": 2.0, "end_s": 2.7}], "edits": )"
            << edits << "}";
        return path;
    }
};

std::size_t at(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * k_rate));
}

bool same_bits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

constexpr const char* k_mute_gap = R"([{"category": "gap", "op": "set", "param": "gap_gain",
    "value": -120, "target": {"gap": {"before": "a", "after": "b"}}}])";

}  // namespace

TEST_CASE("muting_the_gap_keeps_the_notes_and_the_hiss_and_silences_the_rest", "[cli_gap]") {
    const Fixture fx;
    const fs::path out = fx.dir / "out.wav";
    REQUIRE(
        run_render(fx.scenario(k_mute_gap), out, EngineChoice::passthrough, fx.dir).has_value());
    const AudioBuffer rendered = io::read_wav(out).value();
    const auto y = rendered.channel(0);
    const auto& x = fx.samples;
    REQUIRE(y.size() == x.size());
    for (std::size_t i = at(0.0); i < at(1.2); ++i) {  // the first note and the lead-in
        REQUIRE(same_bits(y[i], x[i]));
    }
    for (std::size_t i = at(2.0); i < x.size(); ++i) {  // the second note and the tail
        REQUIRE(same_bits(y[i], x[i]));
    }
    // The hiss is a sibilant region: untouched, bit for bit (a margin inside its 10 ms padding).
    for (std::size_t i = at(1.51); i < at(1.64); ++i) {
        REQUIRE(same_bits(y[i], x[i]));
    }
    // The silence either side of it is gone.
    for (std::size_t i = at(1.3); i < at(1.47); ++i) {
        REQUIRE(y[i] == 0.0F);
    }
    for (std::size_t i = at(1.7); i < at(1.95); ++i) {
        REQUIRE(y[i] == 0.0F);
    }
}

TEST_CASE("an_unknown_gap_in_a_scenario_is_a_named_error", "[cli_gap]") {
    const Fixture fx;
    const auto bad = fx.scenario(R"([{"category": "gap", "op": "set", "param": "gap_gain",
        "value": -120, "target": {"gap": {"before": "a", "after": "nope"}}}])");
    const auto done = run_render(bad, fx.dir / "x.wav", EngineChoice::passthrough, fx.dir);
    REQUIRE_FALSE(done.has_value());
    CHECK(done.error().code == ErrorCode::invalid_argument);
}
