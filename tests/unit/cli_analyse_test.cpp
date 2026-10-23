#include "cli/analysis_json/analysis_json.h"
#include "cli/commands/analyse.h"
#include "cli/plot/plot.h"
#include "core/io/wav.h"
#include "support/synth_signals.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

using namespace anno;
using namespace anno::test;
namespace fs = std::filesystem;

namespace {

/// A fixture melody written to a WAV in a fresh temp directory: two tones with a gap.
struct Fixture {
    fs::path dir;
    fs::path wav;
    Fixture() {
        // unique per process and per fixture: ctest runs the test cases as parallel processes
        std::random_device entropy;
        dir = fs::temp_directory_path() /
              ("anno_cli_analyse_" + std::to_string(entropy()) + "_" + std::to_string(counter()++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        wav = dir / "melody.wav";
        const double rate = 44100.0;
        std::vector<float> x(static_cast<std::size_t>(1.6 * rate), 0.0F);
        const auto tone = [&](double f0, double start, double length) {
            const auto s =
                make_harmonic([f0](double) { return f0; }, length, rate, harmonic_amps(6, 0.4));
            const auto first = static_cast<std::size_t>(std::llround(start * rate));
            for (std::size_t i = 0; i < s.size(); ++i) {
                const double g = std::min({1.0, static_cast<double>(i) / 44.0,
                                           static_cast<double>(s.size() - i) / 220.0});
                x[first + i] += static_cast<float>(g * s[i]);
            }
        };
        tone(262.0, 0.2, 0.5);
        tone(330.0, 0.9, 0.5);
        REQUIRE(io::write_wav(wav, to_buffer(x, rate)).has_value());
    }
    ~Fixture() { fs::remove_all(dir); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

private:
    static int& counter() {
        static int n = 0;
        return n;
    }
};

std::string read_all(const fs::path& p) {
    std::ifstream in{p, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("analyse_writes_json_with_the_documented_keys", "[cli_analyse]") {
    const Fixture f;
    cli::AnalyseArgs args;
    args.wav = f.wav;
    args.json = f.dir / "out.json";
    args.pitch_track_in_json = true;
    const auto summary = cli::run_analyse(args);
    REQUIRE(summary.has_value());
    CHECK(summary->find("melody.wav: 2 notes") != std::string::npos);

    const auto json = nlohmann::json::parse(read_all(*args.json));
    CHECK(json["version"] == 1);
    CHECK(json["algorithm"] == "melodic");
    CHECK(json["source"]["duration_s"].get<double>() > 1.5);
    REQUIRE(json["notes"].size() == 2);
    const auto& n0 = json["notes"][0];
    for (const char* key : {"id", "start_s", "attack_end_s", "release_start_s", "end_s",
                            "pitch_target", "pitch_center_cents", "loudness_lufs", "confidence",
                            "separation_in", "harmonic_count", "sound_classes"}) {
        CHECK(n0.contains(key));
    }
    REQUIRE_FALSE(n0["sound_classes"].empty());
    for (const char* key : {"class", "start_s", "end_s"}) {
        CHECK(n0["sound_classes"][0].contains(key));
    }
    CHECK(n0["harmonic_count"].get<int>() >= 1);
    CHECK(std::abs(n0["start_s"].get<double>() - 0.2) <= 0.02);
    CHECK(std::abs(n0["pitch_target"]["target_cents"].get<double>() -
                   to_cents(Hz{262.0}, Hz{440.0}).value()) <= 10.0);
    CHECK(json["onsets"].size() >= 2);
    REQUIRE(json.contains("pitch"));
    const auto frames = json["pitch"]["n_frames"].get<std::size_t>();
    CHECK(json["pitch"]["f0_cents"].size() == frames);
    CHECK(json["pitch"]["voicing"].size() == frames);
    CHECK(json["pitch"]["f0_cents"][0].is_null());  // silence at the start: unvoiced -> null
}

TEST_CASE("analyse_json_reports_the_gaps_and_the_noise_floor", "[cli_analyse]") {
    const Fixture f;
    cli::AnalyseArgs args;
    args.wav = f.wav;
    args.json = f.dir / "out.json";
    REQUIRE(cli::run_analyse(args).has_value());
    const auto json = nlohmann::json::parse(read_all(*args.json));

    REQUIRE(json.contains("gaps"));
    REQUIRE(json["gaps"].size() == 3);  // before the first tone, between the tones, after the last
    for (const auto& g : json["gaps"]) {
        for (const char* key :
             {"id", "before", "after", "start_s", "end_s", "floor_dbfs", "regions"}) {
            CHECK(g.contains(key));
        }
        REQUIRE_FALSE(g["regions"].empty());
        CHECK(g["regions"][0]["kind"] == "silence");
        CHECK(g["regions"][0]["start_s"] == g["start_s"]);
        CHECK(g["regions"].back()["end_s"] == g["end_s"]);
    }
    CHECK(json["gaps"][0]["before"].is_null());
    CHECK(json["gaps"][0]["start_s"].get<double>() == 0.0);
    CHECK_FALSE(json["gaps"][1]["before"].is_null());
    CHECK_FALSE(json["gaps"][1]["after"].is_null());
    CHECK(json["gaps"][2]["after"].is_null());

    REQUIRE(json.contains("noise_floor"));
    const auto& floor = json["noise_floor"];
    for (const char* key : {"hop_s", "start_s", "n_frames", "band_centre_hz", "band_dbfs"}) {
        CHECK(floor.contains(key));
    }
    CHECK(floor["band_centre_hz"].size() == floor["band_dbfs"].size());
    CHECK_FALSE(floor.contains("curve_dbfs"));  // per-frame arrays only with --pitch-track
}

TEST_CASE("the_floor_curve_is_written_with_the_pitch_track_option", "[cli_analyse]") {
    const Fixture f;
    cli::AnalyseArgs args;
    args.wav = f.wav;
    args.json = f.dir / "out.json";
    args.pitch_track_in_json = true;
    REQUIRE(cli::run_analyse(args).has_value());
    const auto floor = nlohmann::json::parse(read_all(*args.json))["noise_floor"];
    REQUIRE(floor.contains("curve_dbfs"));
    CHECK(floor["curve_dbfs"].size() == floor["n_frames"].get<std::size_t>());
}

TEST_CASE("the_pitch_track_is_left_out_unless_asked_for", "[cli_analyse]") {
    const Fixture f;
    cli::AnalyseArgs args;
    args.wav = f.wav;
    args.json = f.dir / "out.json";
    REQUIRE(cli::run_analyse(args).has_value());
    CHECK_FALSE(nlohmann::json::parse(read_all(*args.json)).contains("pitch"));
}

TEST_CASE("analyse_takes_the_onset_method_and_defaults_to_the_pipeline", "[cli_analyse]") {
    const Fixture f;
    const auto onsets_of = [&](std::optional<SpectralMethod> method) {
        cli::AnalyseArgs args;
        args.wav = f.wav;
        args.json = f.dir / "m.json";
        args.onset_method = method;
        REQUIRE(cli::run_analyse(args).has_value());
        return nlohmann::json::parse(read_all(*args.json))["onsets"];
    };
    const auto pipeline = onsets_of(SpectralMethod::windowed_peaks);
    CHECK(onsets_of(std::nullopt) == pipeline);  // the default is the pipeline
    const auto phase3 = onsets_of(SpectralMethod::adaptive_threshold);
    CHECK(phase3.is_array());
    CHECK(pipeline.size() >= 2);  // the two tones
    CHECK(phase3.size() >= 2);
}

TEST_CASE("analyse_writes_csv_and_an_svg_with_one_rect_per_note", "[cli_analyse]") {
    const Fixture f;
    cli::AnalyseArgs args;
    args.wav = f.wav;
    args.plot_dir = f.dir / "plots";
    REQUIRE(cli::run_analyse(args).has_value());
    const std::string pitch = read_all(*args.plot_dir / "pitch.csv");
    const std::string notes = read_all(*args.plot_dir / "notes.csv");
    const std::string svg = read_all(*args.plot_dir / "plot.svg");
    CHECK(pitch.starts_with("time_s,f0_cents,voicing,confidence\n"));
    CHECK(std::ranges::count(notes, '\n') == 3);  // header + two notes
    CHECK(notes.starts_with("id,start_s,"));
    std::size_t rects = 0;
    for (std::size_t at = svg.find("class=\"note\""); at != std::string::npos;
         at = svg.find("class=\"note\"", at + 1)) {
        ++rects;
    }
    CHECK(rects == 2);
    CHECK(svg.starts_with("<svg "));
    CHECK(svg.find("class=\"pitch\"") != std::string::npos);
    CHECK(svg.find("class=\"onset\"") != std::string::npos);
    CHECK(svg.ends_with("</svg>\n"));
}

TEST_CASE("analyse_reports_errors", "[cli_analyse]") {
    cli::AnalyseArgs args;
    args.wav = "/no/such/file.wav";
    const auto missing = cli::run_analyse(args);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::not_found);

    const Fixture f;
    args.wav = f.wav;
    args.json = f.dir / "no_such_dir" / "out.json";
    CHECK(cli::run_analyse(args).error().code == ErrorCode::io_error);
}

TEST_CASE("an_analysis_without_a_pitch_track_still_plots", "[cli_analyse]") {
    AnalysisResult empty;
    CHECK(cli::pitch_csv(empty) == "time_s,f0_cents,voicing,confidence\n");
    CHECK(cli::plot_svg(empty).find("<svg") == 0);
    CHECK(cli::analysis_to_json(empty, {}).find("\"notes\": []") != std::string::npos);
}
