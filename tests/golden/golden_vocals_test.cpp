// Golden vocals (ROADMAP Phase 3): every recording in tests/golden/vocals/ is analysed and compared
// with its hand-checked note list and pitch spots. The gate is not vacuous: with
// ANNO_REQUIRE_GOLDEN=<n> the inventory test fails unless at least n complete recordings exist (the
// Phase 3 close uses 3).

#include "core/analysis/orchestrator/analyse.h"
#include "core/io/wav.h"
#include "support/analysis_metrics.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace anno;
using namespace anno::test;
namespace fs = std::filesystem;

namespace {

struct Recording {
    fs::path wav;
    fs::path notes;
    fs::path pitch;
};

std::vector<Recording> recordings() {
    std::vector<Recording> out;
    const fs::path dir{ANNO_GOLDEN_DIR};
    if (!fs::exists(dir)) {
        return out;
    }
    for (const auto& entry : fs::directory_iterator{dir}) {
        if (entry.path().extension() != ".wav") {
            continue;
        }
        Recording r{.wav = entry.path(),
                    .notes = fs::path{entry.path()}.replace_extension(".notes.json"),
                    .pitch = fs::path{entry.path()}.replace_extension(".pitch.json")};
        if (fs::exists(r.notes) && fs::exists(r.pitch)) {
            out.push_back(std::move(r));
        }
    }
    std::ranges::sort(out, {}, [](const Recording& r) { return r.wav; });
    return out;
}

nlohmann::json load(const fs::path& p) {
    std::ifstream in{p};
    return nlohmann::json::parse(in);
}

double sec(SourceTime t) {
    return (t - SourceTime{}).value();
}

}  // namespace

TEST_CASE("the_golden_inventory_meets_the_required_count", "[golden]") {
    const auto found = recordings();
    const char* required = std::getenv("ANNO_REQUIRE_GOLDEN");
    if (required != nullptr) {
        CHECK(found.size() >= static_cast<std::size_t>(std::strtoul(required, nullptr, 10)));
    } else if (found.empty()) {
        WARN("no golden recordings in tests/golden/vocals (see its README); the golden checks "
             "below did not run");
    }
}

TEST_CASE("every_golden_recording_matches_its_hand_checked_notes_and_pitch_spots", "[golden]") {
    for (const Recording& r : recordings()) {
        INFO("recording " << r.wav.filename().string());
        auto audio = io::read_wav(r.wav);
        REQUIRE(audio.has_value());
        const AudioSource source = AudioSource::from_buffer(std::move(audio).value());
        const auto analysis = analyse(source, AnalyseParams{});
        REQUIRE(analysis.has_value());

        const auto notes = load(r.notes)["notes"];
        std::vector<double> reference_starts;
        for (const auto& n : notes) {
            reference_starts.push_back(n["start_s"].get<double>());
        }
        std::vector<double> estimated_starts;
        for (const Note& n : (*analysis)->notes) {
            estimated_starts.push_back(sec(n.source_start));
        }
        const auto onset =
            score_onsets(estimated_starts, reference_starts, tolerance::k_onset_golden.value());
        INFO("onset F " << onset.f_measure() << " precision " << onset.precision() << " recall "
                        << onset.recall());
        CHECK(onset.f_measure() >= tolerance::k_onset_golden_f_min);

        // matched notes: the perceived pitch is within the hand-checked tolerance
        for (const auto& ref : notes) {
            for (const Note& n : (*analysis)->notes) {
                if (std::abs(sec(n.source_start) - ref["start_s"].get<double>()) <=
                    tolerance::k_onset_golden.value()) {
                    CHECK(std::abs(n.perceived_pitch.value() - ref["midi"].get<double>() * 100.0) <=
                          tolerance::k_golden_note_pitch.value());
                }
            }
        }

        // spot checks: no octave errors (and a voiced estimate) at hand-checked voiced instants
        const PitchTrack& pitch = *(*analysis)->pitch;
        for (const auto& spot : load(r.pitch)["spots"]) {
            const double t = spot["time_s"].get<double>();
            const auto frame = static_cast<std::size_t>(std::llround(t / pitch.grid.hop.value()));
            REQUIRE(frame < pitch.f0.size());
            const double est = pitch.f0[frame].value();
            INFO("spot at " << t << " s, midi " << spot["midi"].get<double>());
            REQUIRE_FALSE(std::isnan(est));
            const double error = est - spot["midi"].get<double>() * 100.0;
            CHECK(std::abs(error) <= tolerance::k_golden_note_pitch.value());
            CHECK(std::abs(std::abs(error) - 1200.0) >
                  tolerance::k_golden_note_pitch.value());  // never an octave off
        }
    }
}
