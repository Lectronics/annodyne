#include "core/analysis/orchestrator/analyse.h"
#include "support/voicing_probes.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

// Voicing probes: every row of docs/design/phase3-voicing-review.md section 3 with the target of
// section 4. Measured history (2026-10-20): on the annodyne pYIN four rows failed (the tail below
// -52 dB was 52 % voiced and a note; the voiced 'z' became a note; a lone 50 Hz hum at -55, -65 and
// -75 dBFS was a voiced note; a lone -80 dBFS tone was a note of confidence 0.995), while the
// review's breathy-voice and hum-between-notes findings did not reproduce (the activity gate
// removes hum between notes; beta_mean was 0.3). With the vendored c4dm/pyin (ADR-0014, the
// plugin's low-amplitude rule and per-bin unvoiced states) every row passes. The rows stay as
// regression tests for any future tracker.

using namespace anno;
using namespace anno::test;

namespace {

double sec(SourceTime t) {
    return (t - SourceTime{}).value();
}

std::shared_ptr<const AnalysisResult> analyse_samples(const std::vector<float>& x) {
    return analyse(AudioSource::from_buffer(to_buffer(x, k_probe_rate)), AnalyseParams{}).value();
}

double voiced_share(const AnalysisResult& r, double from_s, double to_s) {
    const PitchTrack& p = *r.pitch;
    const double hop = p.grid.hop.value();
    int voiced = 0;
    int total = 0;
    for (auto i = static_cast<std::size_t>(from_s / hop);
         i < static_cast<std::size_t>(to_s / hop) && i < p.f0.size(); ++i) {
        voiced += is_voiced(p, i) ? 1 : 0;
        ++total;
    }
    return total == 0 ? 0.0 : static_cast<double>(voiced) / total;
}

std::vector<const Note*> notes_in(const AnalysisResult& r, double from_s, double to_s) {
    std::vector<const Note*> out;
    for (const Note& n : r.notes) {
        if (sec(n.source_start) < to_s && sec(n.source_end) > from_s) {
            out.push_back(&n);
        }
    }
    return out;
}

/// One probe row: the scene is analysed once and the region judged against its expectation.
void check_row(const Probe& probe, std::size_t row) {
    static std::map<const void*, std::shared_ptr<const AnalysisResult>> cache;
    const auto* const key = static_cast<const void*>(probe.samples.data());
    if (!cache.contains(key)) {
        cache[key] = analyse_samples(probe.samples);
    }
    const AnalysisResult& r = *cache.at(key);
    const ProbeRegion& g = probe.regions.at(row);
    INFO(g.what << " [" << g.from_s << ", " << g.to_s << "] voiced share "
                << voiced_share(r, g.from_s, g.to_s));
    const auto notes = notes_in(r, g.from_s, g.to_s);
    switch (g.expect) {
    case Expect::voiced_note:
        CHECK(voiced_share(r, g.from_s, g.to_s) >= tolerance::k_probe_voiced_share_min);
        CHECK_FALSE(notes.empty());
        break;
    case Expect::unvoiced:
        CHECK(voiced_share(r, g.from_s, g.to_s) <= tolerance::k_probe_unvoiced_share_max);
        CHECK(notes.empty());
        break;
    case Expect::voiced_not_note: CHECK(notes.empty()); break;
    case Expect::voiced:
        CHECK(voiced_share(r, g.from_s, g.to_s) >= tolerance::k_probe_voiced_share_min);
        break;
    case Expect::either: break;
    }
}

/// The probe scenes are built once; their sample buffers key the analysis cache.
const Probe& scene(const std::string& name) {
    static const std::map<std::string, Probe> scenes{{"p1", probe_reverb_tail()},
                                                     {"p2", probe_hum()},
                                                     {"p3", probe_breath_sibilant()},
                                                     {"p4", probe_bleed()},
                                                     {"p5", probe_contours()}};
    return scenes.at(name);
}

}  // namespace

// ---- p1 reverb tail -------------------------------------------------------------------------

TEST_CASE("p1_sung_note_before_a_tail_is_a_note", "[voicing][probe]") {
    check_row(scene("p1"), 0);
}
TEST_CASE("p1_tail_below_minus_52_db_is_unvoiced", "[voicing][probe]") {
    check_row(scene("p1"), 2);
}

// ---- p2 hum ---------------------------------------------------------------------------------

TEST_CASE("p2_notes_over_hum_are_notes", "[voicing][probe]") {
    check_row(scene("p2"), 0);
    check_row(scene("p2"), 2);
}
TEST_CASE("p2_hum_between_notes_is_unvoiced", "[voicing][probe]") {
    check_row(scene("p2"), 1);
}

// ---- p3 breath, sibilant, breathy -----------------------------------------------------------

TEST_CASE("p3_inhale_and_sibilant_are_unvoiced", "[voicing][probe]") {
    check_row(scene("p3"), 0);
    check_row(scene("p3"), 2);
}
TEST_CASE("p3_clean_note_is_a_note", "[voicing][probe]") {
    check_row(scene("p3"), 1);
}
TEST_CASE("p3_voiced_z_is_not_a_note", "[voicing][probe]") {
    check_row(scene("p3"), 3);
}
TEST_CASE("p3_breathy_e4_at_3_db_hnr_is_a_note", "[voicing][probe]") {
    check_row(scene("p3"), 4);
}

// ---- p4 bleed -------------------------------------------------------------------------------

TEST_CASE("p4_notes_over_bleed_are_notes", "[voicing][probe]") {
    check_row(scene("p4"), 0);
    check_row(scene("p4"), 2);
}
TEST_CASE("p4_chord_bleed_at_minus_48_dbfs_is_unvoiced", "[voicing][probe]") {
    check_row(scene("p4"), 1);
}

// ---- p5 contours ----------------------------------------------------------------------------

TEST_CASE("p5_scooped_note_fall_off_and_next_note_are_voiced", "[voicing][probe]") {
    check_row(scene("p5"), 0);
    check_row(scene("p5"), 1);
    check_row(scene("p5"), 3);
}
TEST_CASE("p5_scoop_target_is_a3", "[voicing][probe]") {
    const auto r = analyse_samples(scene("p5").samples);
    const auto notes = notes_in(*r, 0.45, 1.0);
    REQUIRE_FALSE(notes.empty());
    CHECK(std::abs(notes.front()->pitch_target.target.value() - 5700.0) <=
          tolerance::k_probe_scoop_target.value());
}

// ---- sweeps ---------------------------------------------------------------------------------

TEST_CASE("breath_sweep_voiced_share_by_hnr", "[voicing][sweep]") {
    for (const double hnr : {30.0, 20.0, 15.0, 12.0, 10.0}) {
        const auto r = analyse_samples(breathy_tone(hnr));
        INFO("HNR " << hnr << " dB");
        CHECK(voiced_share(*r, 0.35, 1.65) >= tolerance::k_probe_breath_share_10db_min);
    }
}
TEST_CASE("breath_sweep_voiced_share_at_6_db", "[voicing][sweep]") {
    const auto r = analyse_samples(breathy_tone(6.0));
    CHECK(voiced_share(*r, 0.35, 1.65) >= tolerance::k_probe_breath_share_6db_min);
}

TEST_CASE("hum_up_to_20_db_over_the_floor_is_no_note", "[voicing][sweep]") {
    for (const double level : {-55.0, -65.0, -75.0, -85.0}) {
        const auto r = analyse_samples(hum_alone(level));
        INFO("hum " << level << " dBFS");
        CHECK(r->notes.empty());
        CHECK(voiced_share(*r, 0.1, 1.9) <= tolerance::k_probe_unvoiced_share_max);
    }
}

TEST_CASE("a_very_quiet_lone_tone_is_not_a_confident_note", "[voicing][sweep]") {
    const auto r = analyse_samples(lone_tone(-80.0));
    for (const Note& n : r->notes) {
        CHECK(n.confidence_prob <= tolerance::k_probe_unconfident_max);
    }
}
TEST_CASE("audible_lone_tones_stay_notes", "[voicing][sweep]") {
    for (const double level : {-20.0, -40.0}) {
        const auto r = analyse_samples(lone_tone(level));
        INFO("tone " << level << " dBFS");
        REQUIRE(r->notes.size() == 1);
        CHECK(r->notes.front().confidence_prob > tolerance::k_probe_unconfident_max);
    }
}
