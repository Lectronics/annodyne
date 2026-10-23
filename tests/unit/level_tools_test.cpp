#include "core/edits/edit_list.h"
#include "core/edits/effective_state.h"
#include "support/test_data.h"
#include "tools/amplitude/amplitude.h"
#include "tools/fade/fade.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

AnalysisResult two_notes() {
    AnalysisResult a;
    a.notes = {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)};
    return a;
}

EffectiveNote effective_of(const AnalysisResult& a, const std::vector<EditSpec>& specs,
                           const char* id) {
    const EditList list = EditList{TrackId{"t1"}}.add(specs, k_t0).value();
    return effective_state(*find_note(a, NoteId{id}), list);
}

}  // namespace

TEST_CASE("a_gain_edit_sets_the_notes_gain_and_a_mute_is_minus_120_db", "[level_tools]") {
    const AnalysisResult a = two_notes();
    const auto spec = gain_edit(a, NoteId{"a"}, Decibels{-4.0}).value();
    CHECK(spec.origin == "amplitude_tool");
    CHECK(spec.param == EditParam::gain);
    CHECK(spec.op == EditOp::set);
    const EffectiveNote e = effective_of(a, {spec}, "a");
    CHECK(e.gain.value() == -4.0);
    CHECK(e.is_edited);
    const EffectiveNote other = effective_of(a, {spec}, "b");
    CHECK_FALSE(other.is_edited);
    const EffectiveNote muted = effective_of(a, {mute_edit(a, NoteId{"a"}).value()}, "a");
    CHECK(muted.gain.value() == -120.0);
    // a second gain edit replaces the first (op set)
    const EffectiveNote replaced =
        effective_of(a, {spec, gain_edit(a, NoteId{"a"}, Decibels{2.0}).value()}, "a");
    CHECK(replaced.gain.value() == 2.0);
    // 0 dB folds to no edit
    CHECK_FALSE(effective_of(a, {gain_edit(a, NoteId{"a"}, Decibels{0.0}).value()}, "a").is_edited);
}

TEST_CASE("a_flatten_edit_sets_the_amount_between_zero_and_one", "[level_tools]") {
    const AnalysisResult a = two_notes();
    const EffectiveNote e =
        effective_of(a, {flatten_edit(a, NoteId{"a"}, Ratio{0.6}).value()}, "a");
    CHECK(e.within_note_flatten.value() == 0.6);
    CHECK(flatten_edit(a, NoteId{"a"}, Ratio{1.5}).error().code == ErrorCode::invalid_argument);
    CHECK(flatten_edit(a, NoteId{"a"}, Ratio{-0.1}).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("a_fade_makes_three_specs_that_fold_into_the_effective_state", "[level_tools]") {
    const AnalysisResult a = two_notes();
    const auto specs = fade_edits(a, {.note = NoteId{"b"},
                                      .in = Seconds{0.05},
                                      .out = Seconds{0.2},
                                      .shape = FadeShape::log})
                           .value();
    REQUIRE(specs.size() == 3);
    CHECK(specs[0].param == EditParam::fade_in);
    CHECK(specs[1].param == EditParam::fade_out);
    CHECK(specs[2].param == EditParam::fade_shape);
    const EffectiveNote e = effective_of(a, specs, "b");
    CHECK(e.fade_in.value() == 0.05);
    CHECK(e.fade_out.value() == 0.2);
    CHECK(e.fade_shape == FadeShape::log);
}

TEST_CASE("the_level_tools_refuse_unknown_notes_and_bad_numbers", "[level_tools]") {
    AnalysisResult a = two_notes();
    CHECK(gain_edit(a, NoteId{"zz"}, Decibels{1.0}).error().code == ErrorCode::invalid_argument);
    CHECK(gain_edit(a, NoteId{"a"}, Decibels{std::numeric_limits<double>::quiet_NaN()})
              .error()
              .code == ErrorCode::invalid_argument);
    CHECK(flatten_edit(a, NoteId{"a"}, Ratio{std::nan("")}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(fade_edits(a, {.note = NoteId{"a"}, .in = Seconds{-0.1}, .out = Seconds{0.0}})
              .error()
              .code == ErrorCode::invalid_argument);
    CHECK(fade_edits(a, {.note = NoteId{"zz"}, .in = Seconds{0.1}, .out = Seconds{0.1}})
              .error()
              .code == ErrorCode::invalid_argument);
    CHECK(fade_edits(a, {.note = NoteId{"a"},
                         .in = Seconds{0.1},
                         .out = Seconds{0.1},
                         .shape = static_cast<FadeShape>(9)})
              .error()
              .code == ErrorCode::invalid_argument);
    a.notes[0].is_active = false;
    CHECK(gain_edit(a, NoteId{"a"}, Decibels{1.0}).error().code == ErrorCode::invalid_argument);
}
