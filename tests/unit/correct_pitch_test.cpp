#include "core/edits/edit_list.h"
#include "core/edits/effective_state.h"
#include "macros/correct_pitch/correct_pitch.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

// C4 = 6000 cents. Notes: a 30 cents sharp of C4, b exactly E4, c 40 cents flat of F4 (A major
// would pull it to F#4/E4 differently), d unpitched.
AnalysisResult analysis_of_pitches() {
    AnalysisResult a;
    a.notes = {make_note("a", 0.0, 1.0, 6030.0), make_note("b", 1.0, 2.0, 6400.0),
               make_note("c", 2.0, 3.0, 6460.0), make_note("d", 3.0, 4.0, 6000.0)};
    a.notes[3].pitch_target.kind = PitchTargetKind::unpitched;
    return a;
}

CorrectPitchParams c_major() {
    return {.grid = make_scale_grid(ScaleType::major, 0).value()};
}

double offset_of(const EditSpec& s) {
    return std::get<Cents>(s.value).value();
}

}  // namespace

TEST_CASE("each_out_of_tune_note_gets_a_centre_offset_toward_the_scale", "[correct_pitch]") {
    const auto specs = correct_pitch_edits(analysis_of_pitches(), c_major()).value();
    // a: 6030 -> C4 6000 (-30); b is on E4 (no edit); c: 6460 -> F4 6500 (+40); d is unpitched
    REQUIRE(specs.size() == 2);
    CHECK(specs[0].origin == "correct_pitch");
    CHECK(specs[0].param == EditParam::center_offset);
    CHECK(specs[0].op == EditOp::set);
    CHECK(specs[0].target.segment_ids == segments_of({NoteId{"a"}}));
    CHECK(offset_of(specs[0]) == -30.0);
    CHECK(specs[1].target.segment_ids == segments_of({NoteId{"c"}}));
    CHECK(offset_of(specs[1]) == 40.0);
}

TEST_CASE("the_amount_scales_the_correction_and_zero_makes_no_edit", "[correct_pitch]") {
    CorrectPitchParams p = c_major();
    p.center_amount = Ratio{0.5};
    const auto half = correct_pitch_edits(analysis_of_pitches(), p).value();
    REQUIRE(half.size() == 2);
    CHECK(offset_of(half[0]) == -15.0);
    CHECK(offset_of(half[1]) == 20.0);
    p.center_amount = Ratio{0.0};
    CHECK(correct_pitch_edits(analysis_of_pitches(), p).value().empty());
}

TEST_CASE("notes_closer_than_min_deviation_are_left_alone", "[correct_pitch]") {
    CorrectPitchParams p = c_major();
    p.min_deviation = Cents{35.0};
    const auto specs = correct_pitch_edits(analysis_of_pitches(), p).value();
    REQUIRE(specs.size() == 1);  // only c (40 cents) qualifies
    CHECK(specs[0].target.segment_ids == segments_of({NoteId{"c"}}));
}

TEST_CASE("the_edits_fold_into_a_target_on_the_grid", "[correct_pitch]") {
    const AnalysisResult a = analysis_of_pitches();
    const auto specs = correct_pitch_edits(a, c_major()).value();
    const EditList list = EditList{TrackId{"t1"}}.add(specs, k_t0).value();
    const EffectiveNote ea = effective_state(a.notes[0], list);
    CHECK(std::abs((a.notes[0].pitch_center + Cents{-30.0} - ea.center).value()) < 1e-9);
    const EffectiveNote eb = effective_state(a.notes[1], list);
    CHECK_FALSE(eb.is_edited);
}

TEST_CASE("a_drift_amount_adds_a_drift_scale_per_pitched_note", "[correct_pitch]") {
    CorrectPitchParams p = c_major();
    p.drift_amount = Ratio{0.25};
    const auto specs = correct_pitch_edits(analysis_of_pitches(), p).value();
    int drift = 0;
    for (const EditSpec& s : specs) {
        if (s.param == EditParam::drift_scale) {
            ++drift;
            CHECK(std::get<Ratio>(s.value).value() == 0.75);
        }
    }
    CHECK(drift == 3);  // a, b, c; the unpitched d has no single target
}

TEST_CASE("bad_parameters_and_inactive_notes", "[correct_pitch]") {
    CorrectPitchParams p = c_major();
    p.center_amount = Ratio{1.5};
    CHECK(correct_pitch_edits(analysis_of_pitches(), p).error().code ==
          ErrorCode::invalid_argument);
    p = c_major();
    p.drift_amount = Ratio{std::nan("")};
    CHECK(correct_pitch_edits(analysis_of_pitches(), p).error().code ==
          ErrorCode::invalid_argument);
    p = c_major();
    p.min_deviation = Cents{-1.0};
    CHECK(correct_pitch_edits(analysis_of_pitches(), p).error().code ==
          ErrorCode::invalid_argument);
    p = c_major();
    p.grid.degrees = {};
    CHECK(correct_pitch_edits(AnalysisResult{}, p).error().code == ErrorCode::invalid_argument);
    AnalysisResult a = analysis_of_pitches();
    a.notes[0].is_active = false;
    CHECK(correct_pitch_edits(a, c_major()).value().size() == 1);
}
