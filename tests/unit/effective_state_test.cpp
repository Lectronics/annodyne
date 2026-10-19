#include "core/edits/effective_state.h"

#include "support/test_data.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

const WallTime k_t0{};

EditList list_with(std::vector<std::vector<EditSpec>> groups) {
    EditList list{TrackId{"t1"}};
    for (const auto& g : groups) {
        list = list.add(g, k_t0).value();
    }
    return list;
}

EditSpec spec(EditCategory c, EditParam p, EditOp op, EditValue v, const char* id = "a") {
    return make_spec(id, c, p, op, std::move(v));
}

EditSpec gain_add(double db) {
    return spec(EditCategory::amplitude, EditParam::gain, EditOp::add, Decibels{db});
}

}  // namespace

TEST_CASE("no_edits_gives_the_analysed_values", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0, 7100.0);
    const EffectiveNote eff = effective_state(note, list_with({}));
    CHECK(eff.note_id == NoteId{"a"});
    CHECK(eff.center == Cents{7100.0});
    CHECK(eff.mod_scale == Ratio{1.0});
    CHECK(eff.drift_scale == Ratio{1.0});
    CHECK(eff.gain == Decibels{0.0});
    CHECK(eff.length_scale == Ratio{1.0});
    CHECK(eff.handles.empty());
    CHECK_FALSE(eff.is_edited);
}

TEST_CASE("add_edits_sum", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0, 7000.0);
    const auto eff = effective_state(note, list_with({{pitch_add("a", 20)}, {pitch_add("a", 20)}}));
    CHECK(eff.center == Cents{7040.0});
    CHECK(eff.is_edited);
}

TEST_CASE("set_overrides_earlier_edits_of_the_same_parameter", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0, 7000.0);
    const auto eff = effective_state(note, list_with({{pitch_add("a", 20)}, {pitch_set("a", 5)},
                                                      {pitch_add("a", 1)}}));
    CHECK(eff.center == Cents{7006.0});
}

TEST_CASE("set_does_not_touch_sibling_parameters_of_the_category", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const auto eff = effective_state(
        note, list_with({{gain_add(3.0)},
                         {spec(EditCategory::amplitude, EditParam::within_note_flatten,
                               EditOp::set, Ratio{0.5})},
                         {spec(EditCategory::amplitude, EditParam::gain, EditOp::set,
                               Decibels{-2.0})}}));
    CHECK(eff.gain == Decibels{-2.0});
    CHECK(eff.within_note_flatten == Ratio{0.5});
}

TEST_CASE("scale_edits_multiply_and_set_overrides_them", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    auto mod = [](EditOp op, double v) {
        return spec(EditCategory::pitch_mod, EditParam::mod_scale, op, Ratio{v});
    };
    CHECK(effective_state(note, list_with({{mod(EditOp::scale, 0.5)}, {mod(EditOp::scale, 0.5)}}))
              .mod_scale == Ratio{0.25});
    CHECK(effective_state(note, list_with({{mod(EditOp::scale, 0.5)}, {mod(EditOp::set, 2.0)},
                                           {mod(EditOp::scale, 0.5)}}))
              .mod_scale == Ratio{1.0});
}

TEST_CASE("edits_for_other_notes_are_ignored", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    CHECK_FALSE(effective_state(note, list_with({{pitch_add("b", 50)}})).is_edited);
}

TEST_CASE("one_edit_can_target_several_notes", "[effective_state]") {
    auto s = pitch_add("a", 10);
    s.target.note_ids.push_back(NoteId{"b"});
    const auto list = list_with({{s}});
    CHECK(effective_state(make_note("a", 0, 1, 0), list).center == Cents{10});
    CHECK(effective_state(make_note("b", 1, 2, 0), list).center == Cents{10});
}

TEST_CASE("only_active_edits_are_folded", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0, 7000.0);
    EditList list = list_with({{pitch_add("a", 20)}, {pitch_add("a", 30)}});
    list = list.undo().value();
    CHECK(effective_state(note, list).center == Cents{7020.0});
    list = list.redo().value();
    CHECK(effective_state(note, list).center == Cents{7050.0});
}

TEST_CASE("reset_of_a_parameter_returns_it_to_neutral", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0, 7000.0);
    const auto eff = effective_state(
        note, list_with({{pitch_add("a", 20)},
                         {spec(EditCategory::pitch, EditParam::center_offset, EditOp::reset,
                               std::monostate{})},
                         {pitch_add("a", 3)}}));
    CHECK(eff.center == Cents{7003.0});
}

TEST_CASE("reset_without_a_param_clears_the_whole_category_only", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const auto eff = effective_state(
        note, list_with({{gain_add(3.0)},
                         {spec(EditCategory::amplitude, EditParam::within_note_flatten,
                               EditOp::set, Ratio{0.5})},
                         {pitch_add("a", 9)},
                         {make_spec("a", EditCategory::amplitude, std::nullopt, EditOp::reset,
                                    std::monostate{})}}));
    CHECK(eff.gain == Decibels{0.0});
    CHECK(eff.within_note_flatten == Ratio{0.0});
    CHECK(eff.center == Cents{6909.0});  // pitch category untouched
}

TEST_CASE("an_edit_followed_by_its_reset_is_not_edited", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    EditList list = list_with({{pitch_add("a", 20)}});
    const std::vector<NoteId> ids{NoteId{"a"}};
    const std::vector<EditCategory> cats{EditCategory::pitch};
    list = list.reset(ids, cats, k_t0).value();
    CHECK_FALSE(effective_state(note, list).is_edited);
}

TEST_CASE("float_dust_is_snapped_to_neutral", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0, 7000.0);
    const auto eff = effective_state(note, list_with({{pitch_add("a", 0.1)}, {pitch_add("a", 0.2)},
                                                      {pitch_add("a", -0.3)}}));
    CHECK(eff.center == Cents{7000.0});
    CHECK_FALSE(eff.is_edited);
    const auto ratio = effective_state(
        note, list_with({{spec(EditCategory::pitch_mod, EditParam::mod_scale, EditOp::scale,
                               Ratio{3.0})},
                         {spec(EditCategory::pitch_mod, EditParam::mod_scale, EditOp::scale,
                               Ratio{1.0 / 3.0})}}));
    CHECK(ratio.mod_scale == Ratio{1.0});
    CHECK_FALSE(ratio.is_edited);
}

TEST_CASE("final_values_are_clamped_to_legal_ranges", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    auto eff = effective_state(
        note, list_with({{spec(EditCategory::fade, EditParam::fade_in, EditOp::add, Seconds{0.1})},
                         {spec(EditCategory::fade, EditParam::fade_in, EditOp::add, Seconds{-0.5})},
                         {spec(EditCategory::amplitude, EditParam::within_note_flatten,
                               EditOp::set, Ratio{0.8})},
                         {spec(EditCategory::amplitude, EditParam::within_note_flatten,
                               EditOp::set, Ratio{0.8})}}));
    CHECK(eff.fade_in == Seconds{0.0});
    CHECK(eff.within_note_flatten == Ratio{0.8});
    // Tiny scales cannot push length_scale below k_min_time_scale.
    EditList list = list_with({});
    for (int i = 0; i < 5; ++i) {
        list = list.add(std::vector{spec(EditCategory::time, EditParam::length_scale,
                                         EditOp::scale, Ratio{0.1})},
                        k_t0)
                   .value();
    }
    CHECK(effective_state(note, list).length_scale == k_min_time_scale);
}

TEST_CASE("fade_shape_is_neutral_when_both_fades_are_zero", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const auto only_shape = effective_state(
        note, list_with({{spec(EditCategory::fade, EditParam::fade_shape, EditOp::set,
                               FadeShape::log)}}));
    CHECK(only_shape.fade_shape == FadeShape::linear);
    CHECK_FALSE(only_shape.is_edited);
    const auto with_fade = effective_state(
        note, list_with({{spec(EditCategory::fade, EditParam::fade_shape, EditOp::set,
                               FadeShape::log)},
                         {spec(EditCategory::fade, EditParam::fade_out, EditOp::set,
                               Seconds{0.05})}}));
    CHECK(with_fade.fade_shape == FadeShape::log);
    CHECK(with_fade.is_edited);
}

TEST_CASE("noise_gains_are_folded_per_frame_class", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const auto eff = effective_state(
        note, list_with({{spec(EditCategory::sibilant, EditParam::noise_gain_sibilant,
                               EditOp::add, Decibels{-6.0})},
                         {spec(EditCategory::sibilant, EditParam::noise_gain_breath, EditOp::set,
                               Decibels{-12.0})}}));
    CHECK(noise_gain(eff.noise_gain, FrameClass::sibilant) == Decibels{-6.0});
    CHECK(noise_gain(eff.noise_gain, FrameClass::breath) == Decibels{-12.0});
    CHECK(noise_gain(eff.noise_gain, FrameClass::voiced) == Decibels{0.0});
}

TEST_CASE("timing_edits_are_part_of_the_effective_state", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const auto eff = effective_state(
        note, list_with({{spec(EditCategory::time, EditParam::onset_shift, EditOp::add,
                               Seconds{0.1})},
                         {spec(EditCategory::time, EditParam::length_scale, EditOp::set,
                               Ratio{2.0})},
                         {spec(EditCategory::attack, EditParam::attack_time_scale, EditOp::set,
                               Ratio{0.5})}}));
    CHECK(eff.onset_shift == Seconds{0.1});
    CHECK(eff.length_scale == Ratio{2.0});
    CHECK(eff.attack_time_scale == Ratio{0.5});
    CHECK(eff.is_edited);
}

TEST_CASE("time_handles_are_keyed_by_source_time", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    auto handle = [](double s, double o, EditOp op = EditOp::set) {
        return spec(EditCategory::time, EditParam::time_handle, op, TimePair{src(s), out(o)});
    };
    auto eff = effective_state(note, list_with({{handle(1.6, 1.7)}, {handle(1.3, 1.3)}}));
    REQUIRE(eff.handles.size() == 2);
    CHECK(eff.handles[0].source == src(1.3));  // sorted by source
    CHECK(eff.handles[1].output == out(1.7));

    eff = effective_state(note, list_with({{handle(1.6, 1.7)}, {handle(1.6 + 1e-8, 1.9)}}));
    REQUIRE(eff.handles.size() == 1);          // same key: replaced
    CHECK(eff.handles[0].output == out(1.9));

    eff = effective_state(note, list_with({{handle(1.6, 1.7)}, {handle(1.3, 1.3)},
                                           {handle(1.6, 0, EditOp::reset)}}));
    REQUIRE(eff.handles.size() == 1);          // one handle removed
    CHECK(eff.handles[0].source == src(1.3));

    eff = effective_state(note, list_with({{handle(1.6, 1.7)},
                                           {spec(EditCategory::time, EditParam::time_handle,
                                                 EditOp::reset, std::monostate{})}}));
    CHECK(eff.handles.empty());
    CHECK_FALSE(eff.is_edited);
}

TEST_CASE("orphaned_targets_are_ignored_by_the_fold", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const EditList list = list_with({{pitch_add("a", 20)}}).retargeted(NoteIdMap{});
    CHECK_FALSE(effective_state(note, list).is_edited);
}

TEST_CASE("creation_time_never_influences_the_fold", "[effective_state]") {
    const Note note = make_note("a", 1.0, 2.0);
    const std::vector<EditSpec> g{pitch_add("a", 20)};
    const EditList one = EditList{TrackId{"t1"}}.add(g, WallTime{}).value();
    const EditList two = EditList{TrackId{"t1"}}.add(g, WallTime{} + std::chrono::hours(5)).value();
    CHECK(effective_state(note, one) == effective_state(note, two));
}
