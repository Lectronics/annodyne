// Golden vocals (ROADMAP Phase 3): the committed Vocadito subset (tests/golden/vocals/vocadito, see
// its ATTRIBUTION.md) is analysed and scored against the human annotations: f0 per frame, and note
// starts and pitches from two annotators. The thresholds are a REGRESSION GUARD set from the
// measured result of the held-out tracks with a margin (tests/tolerances.h), not a quality claim;
// the annotators themselves agree on only ~75 % of onsets. With ANNO_REQUIRE_GOLDEN=<n> the
// inventory test fails unless at least n complete tracks exist (the Phase 3 close uses 3).

#include "core/analysis/orchestrator/analyse.h"
#include "core/io/wav.h"
#include "support/vocadito_eval.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>

using namespace anno;
using namespace anno::test;
namespace fs = std::filesystem;

namespace {

std::vector<VocaditoTrack> committed_tracks() {
    std::vector<VocaditoTrack> out;
    for (int id = 1; id <= 40; ++id) {
        if (auto t = load_vocadito(fs::path{ANNO_GOLDEN_DIR}, id)) {
            out.push_back(std::move(*t));
        }
    }
    return out;
}

}  // namespace

TEST_CASE("the_golden_inventory_meets_the_required_count", "[golden]") {
    const auto found = committed_tracks();
    const char* required = std::getenv("ANNO_REQUIRE_GOLDEN");
    if (required != nullptr) {
        CHECK(found.size() >= static_cast<std::size_t>(std::strtoul(required, nullptr, 10)));
    } else if (found.empty()) {
        WARN("no golden recordings in tests/golden/vocals/vocadito (see its ATTRIBUTION.md); the "
             "golden checks below did not run");
    }
}

TEST_CASE("the_loader_reads_the_annotations", "[golden]") {
    for (const VocaditoTrack& t : committed_tracks()) {
        INFO("track " << t.id);
        CHECK(t.f0_hop_s > 0.005);
        CHECK(t.f0_hop_s < 0.006);
        CHECK(!t.notes_a1.empty());
        CHECK(!t.notes_a2.empty());
        for (const RefNote& n : t.notes_a1) {
            CHECK(n.pitch_hz > 40.0);
            CHECK(n.duration_s > 0.0);
        }
    }
}

namespace {

struct Outcome {
    bool analysed = false;
    VocaditoScores scores;
};

Outcome score_track(const VocaditoTrack& t) {
    Outcome o;
    auto audio = io::read_wav(t.wav);
    if (!audio) {
        return o;
    }
    const auto result = analyse(AudioSource::from_buffer(std::move(audio).value()), {});
    if (!result || !validate(**result)) {
        return o;
    }
    o.analysed = true;
    o.scores = evaluate(t, **result);
    return o;
}

}  // namespace

TEST_CASE("the_committed_vocals_meet_the_regression_guard", "[golden]") {
    const auto tracks = committed_tracks();
    std::vector<std::future<Outcome>> jobs;  // analyse() is thread-safe: one track per thread
    jobs.reserve(tracks.size());
    for (const VocaditoTrack& t : tracks) {
        jobs.push_back(std::async(std::launch::async, score_track, std::cref(t)));
    }
    VocaditoScores total;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const Outcome o = jobs[i].get();
        INFO("track " << tracks[i].id);
        REQUIRE(o.analysed);
        CHECK(o.scores.notes > 0);
        accumulate(total, o.scores);
    }
    if (total.voiced_reference == 0) {
        return;  // nothing committed: the inventory test warns
    }
    INFO("voicing recall " << total.voicing_recall() << " rpa " << total.pitch.rpa() << " octave "
                           << total.octave_rate() << " F20 " << total.best_f20() << " F50 "
                           << total.best_f50() << " note pitch " << total.note_pitch_ok());
    CHECK(total.voicing_recall() >= tolerance::k_golden_voicing_recall_min);
    CHECK(total.pitch.rpa() >= tolerance::k_golden_rpa_min);
    CHECK(total.octave_rate() <= tolerance::k_golden_octave_rate_max);
    CHECK(total.best_f20() >= tolerance::k_golden_onset_f20_min);
    CHECK(total.best_f50() >= tolerance::k_golden_onset_f50_min);
    CHECK(total.note_pitch_ok() >= tolerance::k_golden_note_pitch_ok_min);
}
