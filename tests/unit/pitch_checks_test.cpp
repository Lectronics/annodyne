#include "cli/checks/pitch_checks.h"
#include "core/analysis/orchestrator/analyse.h"
#include "support/synth_signals.h"
#include "support/test_data.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace anno;
using namespace anno::cli;
using namespace anno::test;

namespace {

Note note_with_mod(double rate_hz, double extent_cents) {
    Note note = make_note("v", 0.0, 2.0);
    note.grid = FrameGrid{.hop = Seconds{0.005}, .start = SourceTime{}, .n_frames = 400};
    for (int i = 0; i < 400; ++i) {
        note.pitch_mod.emplace_back(extent_cents *
                                    std::sin(2.0 * std::numbers::pi * rate_hz * 0.005 * i));
        note.pitch_drift.emplace_back(0.0);
    }
    return note;
}

}  // namespace

TEST_CASE("the_pitch_check_limits_match_the_pinned_tolerances", "[pitch_checks]") {
    const PitchCheckParams p;
    CHECK(p.pitch_roundtrip == tolerance::k_pitch_roundtrip);
    CHECK(p.vibrato_rate == tolerance::k_vibrato_rate);
    CHECK(p.vibrato_extent_rel == tolerance::k_vibrato_extent_rel);
    CHECK(p.formant_preservation_rel == tolerance::k_formant_preservation_rel);
    CHECK(p.noise_centroid_rel == tolerance::k_noise_centroid_rel);
}

TEST_CASE("vibrato_of_reads_rate_and_extent_from_the_modulation_curve", "[pitch_checks]") {
    const auto v = vibrato_of(note_with_mod(5.5, 40.0)).value();
    CHECK(std::abs(v.rate.value() - 5.5) < 0.2);
    CHECK(std::abs(v.extent.value() - 40.0) < 2.0);
    Note flat = note_with_mod(5.5, 0.0);
    CHECK_FALSE(vibrato_of(flat).has_value());
    Note nan_note = note_with_mod(5.5, 40.0);
    for (Cents& c : nan_note.pitch_mod) {
        c = Cents{std::nan("")};
    }
    CHECK_FALSE(vibrato_of(nan_note).has_value());
}

TEST_CASE("first_formant_finds_a_refined_peak_between_the_bins", "[pitch_checks]") {
    Note note = make_note("f", 0.0, 1.0);
    note.envelope.grid = FrameGrid{.hop = Seconds{0.005}, .start = SourceTime{}, .n_frames = 3};
    const std::size_t bins = 513;
    for (std::size_t b = 0; b < bins; ++b) {
        note.envelope.freqs.emplace_back(22050.0 * static_cast<double>(b) / 512.0);
    }
    note.envelope.log_mag_db = {.rows = 3, .cols = static_cast<int>(bins), .data = {}};
    for (int row = 0; row < 3; ++row) {
        for (std::size_t b = 0; b < bins; ++b) {
            const double f = note.envelope.freqs[b].value();
            const double x = (f - 713.0) / 120.0;  // peak at 713 Hz, between two 43 Hz bins
            note.envelope.log_mag_db.data.push_back(static_cast<float>(-x * x));
        }
    }
    const auto f1 = first_formant(note).value();
    CHECK(std::abs(f1.value() - 713.0) < 3.0);
    CHECK_FALSE(first_formant(make_note("none", 0.0, 1.0)).has_value());
}

TEST_CASE("spectral_centroid_follows_the_content", "[pitch_checks]") {
    const Hz rate{44100.0};
    CHECK(std::abs(spectral_centroid(make_sine(3000.0, 0.5, 44100.0), rate).value() - 3000.0) <
          100.0);
    CHECK(spectral_centroid(make_sine(6000.0, 0.5, 44100.0), rate).value() >
          spectral_centroid(make_sine(3000.0, 0.5, 44100.0), rate).value());
    CHECK(spectral_centroid(std::vector<float>(100, 0.1F), rate).value() == 0.0);
    CHECK(spectral_centroid(std::vector<float>(20000, 0.0F), rate).value() == 0.0);
}

TEST_CASE("matching_note_picks_the_largest_overlap", "[pitch_checks]") {
    AnalysisResult a;
    a.notes = {make_note("a", 0.0, 1.0), make_note("b", 1.0, 3.0)};
    CHECK(matching_note(a, make_note("x", 0.8, 2.5))->id == NoteId{"b"});
    CHECK(matching_note(a, make_note("y", 5.0, 6.0)) == nullptr);
}

TEST_CASE("median_pitch_shift_measures_a_shifted_tone_and_refuses_a_missing_track",
          "[pitch_checks]") {
    const double rate = 44100.0;
    const auto tone = [&](double f0) {
        auto x = make_harmonic([f0](double) { return f0; }, 1.2, rate, harmonic_amps(6, 0.4));
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double t = static_cast<double>(i) / rate;
            x[i] *= static_cast<float>(std::clamp(std::min(t - 0.1, 1.1 - t) / 0.02, 0.0, 1.0));
        }
        return AudioSource::from_buffer(to_buffer(x, rate));
    };
    const auto a = analyse(tone(220.0), AnalyseParams{}).value();
    const auto b = analyse(tone(220.0 * std::exp2(300.0 / 1200.0)), AnalyseParams{}).value();
    REQUIRE_FALSE(a->notes.empty());
    const auto shift = median_pitch_shift(*a, *b, a->notes.front());
    REQUIRE(shift.has_value());
    CHECK(std::abs(shift->value() - 300.0) < 3.0);
    AnalysisResult no_track = *b;
    no_track.pitch.reset();
    CHECK_FALSE(median_pitch_shift(*a, no_track, a->notes.front()).has_value());
}
