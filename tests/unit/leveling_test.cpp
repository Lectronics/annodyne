#include "macros/leveling/leveling.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

AnalysisResult loudnesses(std::initializer_list<double> values) {
    AnalysisResult a;
    int i = 0;
    for (const double v : values) {
        Note n = make_note("n" + std::to_string(i), i * 1.0, i * 1.0 + 0.8);
        n.loudness = LoudnessUnits{v};
        a.notes.push_back(n);
        ++i;
    }
    return a;
}

double gain_of(const EditSpec& s) {
    return std::get<Decibels>(s.value).value();
}

}  // namespace

TEST_CASE("notes_move_toward_the_mean_loudness_by_the_amount", "[leveling]") {
    const auto a = loudnesses({-20.0, -30.0, -25.0});  // mean -25
    LevelingParams p;
    p.between_notes = Ratio{1.0};
    auto specs = leveling_edits(a, p).value();
    REQUIRE(specs.size() == 2);  // the note at the mean needs nothing
    CHECK(specs[0].param == EditParam::gain);
    CHECK(specs[0].origin == "leveling");
    CHECK(specs[0].target.segment_ids == segments_of({NoteId{"n0"}}));
    CHECK(gain_of(specs[0]) == -5.0);
    CHECK(specs[1].target.segment_ids == segments_of({NoteId{"n1"}}));
    CHECK(gain_of(specs[1]) == 5.0);
    p.between_notes = Ratio{0.5};
    specs = leveling_edits(a, p).value();
    REQUIRE(specs.size() == 2);
    CHECK(gain_of(specs[0]) == -2.5);
    CHECK(gain_of(specs[1]) == 2.5);
    p.between_notes = Ratio{0.0};
    CHECK(leveling_edits(a, p).value().empty());
}

TEST_CASE("an_explicit_target_and_the_gain_limits_apply", "[leveling]") {
    const auto a = loudnesses({-20.0, -40.0});
    LevelingParams p;
    p.between_notes = Ratio{1.0};
    p.target = LoudnessUnits{-23.0};
    p.max_gain = Decibels{12.0};
    auto specs = leveling_edits(a, p).value();
    REQUIRE(specs.size() == 2);
    CHECK(gain_of(specs[0]) == -3.0);
    CHECK(gain_of(specs[1]) == 12.0);  // 17 dB wanted, limited to 12
    p.min_gain = Decibels{5.0};
    specs = leveling_edits(a, p).value();
    REQUIRE(specs.size() == 1);  // the 3 dB correction is below min_gain
    CHECK(specs[0].target.segment_ids == segments_of({NoteId{"n1"}}));
}

TEST_CASE("a_within_amount_flattens_every_active_note", "[leveling]") {
    auto a = loudnesses({-20.0, -30.0, -25.0});
    a.notes[2].is_active = false;
    LevelingParams p;
    p.between_notes = Ratio{0.0};
    p.within_notes = Ratio{0.7};
    const auto specs = leveling_edits(a, p).value();
    REQUIRE(specs.size() == 2);
    for (const EditSpec& s : specs) {
        CHECK(s.param == EditParam::within_note_flatten);
        CHECK(std::get<Ratio>(s.value).value() == 0.7);
    }
}

TEST_CASE("silent_notes_with_no_loudness_are_left_alone_and_bad_parameters_fail", "[leveling]") {
    auto a = loudnesses({-20.0, -30.0});
    a.notes[1].loudness = LoudnessUnits{-std::numeric_limits<double>::infinity()};
    LevelingParams p;
    p.between_notes = Ratio{1.0};
    CHECK(leveling_edits(a, p).value().empty());  // one finite note: it is the mean itself
    CHECK(leveling_edits(AnalysisResult{}, p).value().empty());
    p.between_notes = Ratio{2.0};
    CHECK(leveling_edits(a, p).error().code == ErrorCode::invalid_argument);
    p = {};
    p.max_gain = Decibels{0.0};
    CHECK(leveling_edits(a, p).error().code == ErrorCode::invalid_argument);
    p = {};
    p.target = LoudnessUnits{std::nan("")};
    CHECK(leveling_edits(a, p).error().code == ErrorCode::invalid_argument);
}
