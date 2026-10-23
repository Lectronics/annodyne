#include "cli/commands/audition.h"
#include "cli/commands/render.h"
#include "cli/scenario_json/scenario_json.h"
#include "core/edits/effective_state.h"
#include "core/io/wav.h"
#include "support/synth_signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

using namespace anno;
using namespace anno::cli;
using namespace anno::test;
namespace fs = std::filesystem;

namespace {

/// A two-note voice in a fresh temp folder, with scenario files that drag its first note.
struct Fixture {
    fs::path dir;
    Fixture() {
        std::random_device entropy;
        dir = fs::temp_directory_path() / ("anno_cli_engines_" + std::to_string(entropy()));
        fs::create_directories(dir);
        const double rate = 44100.0;
        std::vector<float> x(static_cast<std::size_t>(1.6 * rate), 0.0F);
        const auto tone = [&](double f0, double start, double length) {
            const auto s =
                make_harmonic([f0](double) { return f0; }, length, rate, harmonic_amps(8, 0.4));
            const auto first = static_cast<std::size_t>(std::llround(start * rate));
            for (std::size_t i = 0; i < s.size(); ++i) {
                const double g = std::min({1.0, static_cast<double>(i) / 880.0,
                                           static_cast<double>(s.size() - i) / 880.0});
                x[first + i] += static_cast<float>(g * s[i]);
            }
        };
        tone(262.0, 0.2, 0.5);
        tone(330.0, 0.9, 0.5);
        REQUIRE(io::write_wav(dir / "voice.wav", to_buffer(x, rate)).has_value());
    }
    ~Fixture() { fs::remove_all(dir); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] fs::path scenario(const std::string& edits) const {
        const fs::path path = dir / "scenario.json";
        std::ofstream out{path};
        out << R"({"name": "drag", "source": "voice.wav", "analysis": "analyse", "edits": )"
            << edits << "}";
        return path;
    }
};

}  // namespace

TEST_CASE("engine_names_round_trip_and_unknown_names_are_rejected", "[cli_engines]") {
    for (const auto choice :
         {EngineChoice::passthrough, EngineChoice::harmonic_noise, EngineChoice::psola}) {
        CHECK(parse_engine(to_string(choice)).value() == choice);
        CHECK(make_engine(choice)->name() == to_string(choice));
    }
    CHECK(parse_engine("sinc").error().code == ErrorCode::invalid_argument);
}

TEST_CASE("a_pitch_drag_scenario_renders_through_each_pitch_engine", "[cli_engines]") {
    const Fixture fx;
    const auto sc = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 200}])");
    for (const auto choice : {EngineChoice::harmonic_noise, EngineChoice::psola}) {
        const fs::path out = fx.dir / (std::string{to_string(choice)} + ".wav");
        const auto done = run_render(sc, out, choice, fx.dir);
        INFO(to_string(choice));
        INFO((done.has_value() ? std::string{} : done.error().message));
        REQUIRE(done.has_value());
        const auto audio = io::read_wav(out).value();
        const auto original = io::read_wav(fx.dir / "voice.wav").value();
        REQUIRE(audio.frame_count() == original.frame_count());
        bool changed = false;
        for (std::int64_t i = 0; i < audio.frame_count().value(); ++i) {
            const auto k = static_cast<std::size_t>(i);
            changed = changed || audio.channel(0)[k] != original.channel(0)[k];
        }
        CHECK(changed);
    }
}

TEST_CASE("the_passthrough_engine_returns_the_source_whatever_the_drag", "[cli_engines]") {
    const Fixture fx;
    const auto sc = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 200}])");
    const fs::path out = fx.dir / "pass.wav";
    REQUIRE(run_render(sc, out, EngineChoice::passthrough, fx.dir).has_value());
    const auto audio = io::read_wav(out).value();
    const auto original = io::read_wav(fx.dir / "voice.wav").value();
    REQUIRE(audio.frame_count() == original.frame_count());
    CHECK(
        std::equal(audio.channel(0).begin(), audio.channel(0).end(), original.channel(0).begin()));
}

TEST_CASE("drags_resolve_to_the_requested_target_offset_snapped_or_not", "[cli_engines]") {
    const Fixture fx;
    const auto center_of = [&](const std::string& edit) {
        const auto run = load_run(fx.scenario("[" + edit + "]"), fx.dir).value();
        const Note& note = run.analysis->notes[0];
        const EffectiveNote eff = effective_state(note, *run.edits);
        return std::pair{(eff.center - note.pitch_center).value(),
                         note.pitch_target.target.value()};
    };
    const auto [offset, target] =
        center_of(R"({"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 123.5})");
    CHECK(offset == 123.5);
    const auto abs_offset =
        center_of(R"({"tool": "pitch_drag", "target": {"note_index": 0}, "target_cents": 7000})");
    CHECK(abs_offset.first == 7000.0 - target);
    const auto snapped = center_of(
        R"({"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 123.5, "snap": "chromatic"})");
    const double want = std::floor((target + 123.5 - 6900.0) / 100.0 + 0.5) * 100.0 + 6900.0;
    CHECK(snapped.first == Catch::Approx(want - target).margin(1e-9));
}

TEST_CASE("an_analysed_scenario_loads_and_renders_identically_twice", "[cli_engines]") {
    const Fixture fx;
    const auto sc = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 100}])");
    const auto a = load_run(sc, fx.dir).value();
    const auto b = load_run(sc, fx.dir).value();
    REQUIRE(a.analysis->notes.size() == b.analysis->notes.size());
    for (std::size_t i = 0; i < a.analysis->notes.size(); ++i) {
        CHECK(a.analysis->notes[i].id == b.analysis->notes[i].id);
    }
    CHECK(a.edits->active().size() == b.edits->active().size());
    const fs::path o1 = fx.dir / "r1.wav";
    const fs::path o2 = fx.dir / "r2.wav";
    REQUIRE(run_render(sc, o1, EngineChoice::harmonic_noise, fx.dir).has_value());
    REQUIRE(run_render(sc, o2, EngineChoice::harmonic_noise, fx.dir).has_value());
    const auto x = io::read_wav(o1).value();
    const auto y = io::read_wav(o2).value();
    CHECK(std::equal(x.channel(0).begin(), x.channel(0).end(), y.channel(0).begin()));
}

TEST_CASE("every_phase4_scenario_file_parses_names_a_real_source_and_known_engines",
          "[cli_engines]") {
    const fs::path root = ANNO_REPO_ROOT;
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(root / "tests/audio/scenarios/phase4")) {
        const auto parsed = load_scenario(entry.path(), root);
        INFO(entry.path().string());
        REQUIRE(parsed.has_value());
        CHECK(fs::exists(parsed->source));
        CHECK(parsed->analysis == ScenarioAnalysis::analyse);
        const auto expect = nlohmann::json::parse(parsed->expect_json);
        for (const auto& engine : expect.at("engines")) {
            CHECK(parse_engine(engine.get<std::string>()).has_value());
        }
        ++count;
    }
    CHECK(count >= 10);
}

TEST_CASE("a_drag_past_the_analysed_notes_or_beyond_the_limit_is_refused", "[cli_engines]") {
    const Fixture fx;
    const fs::path out = fx.dir / "x.wav";
    const auto past = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 9}, "offset_cents": 200}])");
    const auto r1 = run_render(past, out, EngineChoice::harmonic_noise, fx.dir);
    CHECK(r1.error().code == ErrorCode::invalid_argument);
    CHECK(r1.error().message.find("note_index 9") != std::string::npos);
    const auto far = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 3000}])");
    CHECK(run_render(far, out, EngineChoice::harmonic_noise, fx.dir).error().code ==
          ErrorCode::out_of_range);
}

TEST_CASE("psola_refuses_a_large_drag_while_the_harmonic_engine_renders_it", "[cli_engines]") {
    const Fixture fx;
    const auto sc = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 0}, "offset_cents": 1200}])");
    const fs::path out = fx.dir / "big.wav";
    const auto psola = run_render(sc, out, EngineChoice::psola, fx.dir);
    REQUIRE_FALSE(psola.has_value());
    INFO(psola.error().message);
    CHECK(psola.error().code == ErrorCode::not_implemented);
    const auto harmonic = run_render(sc, out, EngineChoice::harmonic_noise, fx.dir);
    INFO((harmonic.has_value() ? std::string{} : harmonic.error().message));
    CHECK(harmonic.has_value());
}

TEST_CASE("an_audition_package_is_written_for_a_pitch_engine", "[cli_engines]") {
    const Fixture fx;
    const auto sc = fx.scenario(
        R"([{"tool": "pitch_drag", "target": {"note_index": 1}, "offset_cents": -200, "snap": "chromatic"}])");
    const fs::path out = fx.dir / "package";
    const auto summary = run_audition(sc, out, EngineChoice::harmonic_noise, fx.dir);
    REQUIRE(summary.has_value());
    for (const char* file : {"original.wav", "edited.wav", "report.md", "INDEX.md", "edits.json"}) {
        CHECK(fs::exists(out / file));
    }
    CHECK(summary->report.find("harmonic_noise") != std::string::npos);
}
