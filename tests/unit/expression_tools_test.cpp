#include "core/edits/edit_list.h"
#include "core/edits/effective_state.h"
#include "support/test_data.h"
#include "tools/formant/formant.h"
#include "tools/pitch_drift/pitch_drift.h"
#include "tools/pitch_modulation/pitch_modulation.h"
#include "tools/sibilant/sibilant.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

AnalysisResult two_notes() {
    AnalysisResult a;
    a.notes = {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)};
    return a;
}

EffectiveNote fold(const AnalysisResult& a, const EditSpec& spec, const char* id) {
    const EditList list = EditList{TrackId{"t1"}}.add(std::vector{spec}, k_t0).value();
    return effective_state(*find_note(a, NoteId{id}), list);
}

}  // namespace

TEST_CASE("the_modulation_tool_sets_mod_scale", "[expression_tools]") {
    const AnalysisResult a = two_notes();
    const auto spec = modulation_edit(a, NoteId{"a"}, Ratio{0.5}).value();
    CHECK(spec.origin == "pitch_modulation_tool");
    CHECK(spec.param == EditParam::mod_scale);
    CHECK(fold(a, spec, "a").mod_scale.value() == 0.5);
    CHECK_FALSE(fold(a, spec, "b").is_edited);
    CHECK(fold(a, modulation_edit(a, NoteId{"a"}, Ratio{0.0}).value(), "a").mod_scale.value() ==
          0.0);
    CHECK(fold(a, modulation_edit(a, NoteId{"a"}, Ratio{-1.0}).value(), "a").mod_scale.value() ==
          -1.0);
    CHECK_FALSE(fold(a, modulation_edit(a, NoteId{"a"}, Ratio{1.0}).value(), "a").is_edited);
}

TEST_CASE("the_drift_tool_sets_drift_scale", "[expression_tools]") {
    const AnalysisResult a = two_notes();
    const auto spec = drift_edit(a, NoteId{"b"}, Ratio{0.0}).value();
    CHECK(spec.origin == "pitch_drift_tool");
    CHECK(spec.param == EditParam::drift_scale);
    CHECK(fold(a, spec, "b").drift_scale.value() == 0.0);
}

TEST_CASE("the_formant_tool_sets_the_formant_offset", "[expression_tools]") {
    const AnalysisResult a = two_notes();
    const auto spec = formant_edit(a, NoteId{"a"}, Cents{-250.0}).value();
    CHECK(spec.origin == "formant_tool");
    CHECK(spec.param == EditParam::formant_offset);
    CHECK(fold(a, spec, "a").formant_offset.value() == -250.0);
    CHECK_FALSE(fold(a, formant_edit(a, NoteId{"a"}, Cents{0.0}).value(), "a").is_edited);
}

TEST_CASE("the_expression_tools_refuse_unknown_notes_and_non_finite_values", "[expression_tools]") {
    AnalysisResult a = two_notes();
    CHECK(modulation_edit(a, NoteId{"zz"}, Ratio{1.0}).error().code == ErrorCode::invalid_argument);
    CHECK(drift_edit(a, NoteId{"a"}, Ratio{std::nan("")}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(formant_edit(a, NoteId{"a"}, Cents{std::nan("")}).error().code ==
          ErrorCode::invalid_argument);
    a.notes[0].is_active = false;
    CHECK(formant_edit(a, NoteId{"a"}, Cents{10.0}).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("the_sibilant_tool_sets_the_three_class_gains", "[expression_tools]") {
    const AnalysisResult a = two_notes();
    const auto specs = sibilant_edits(a, {.note = NoteId{"a"},
                                          .sibilant = Decibels{-9.0},
                                          .breath = Decibels{3.0},
                                          .plosive = Decibels{0.0}})
                           .value();
    REQUIRE(specs.size() == 3);
    CHECK(specs[0].param == EditParam::noise_gain_sibilant);
    CHECK(specs[1].param == EditParam::noise_gain_breath);
    CHECK(specs[2].param == EditParam::noise_gain_plosive);
    const EditList list = EditList{TrackId{"t1"}}.add(specs, k_t0).value();
    const EffectiveNote e = effective_state(a.notes[0], list);
    CHECK(e.noise_gain.sibilant.value() == -9.0);
    CHECK(e.noise_gain.breath.value() == 3.0);
    CHECK(e.noise_gain.plosive.value() == 0.0);
    CHECK(sibilant_edits(a, {.note = NoteId{"zz"}, .sibilant = {}, .breath = {}, .plosive = {}})
              .error()
              .code == ErrorCode::invalid_argument);
    CHECK(
        sibilant_edits(
            a,
            {.note = NoteId{"a"}, .sibilant = Decibels{std::nan("")}, .breath = {}, .plosive = {}})
            .error()
            .code == ErrorCode::invalid_argument);
}
