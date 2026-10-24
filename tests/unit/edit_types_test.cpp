#include "core/types/edit.h"
#include "core/types/effective_gap.h"
#include "core/types/effective_note.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace anno;
using namespace anno::test;

namespace {
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
}

TEST_CASE("every_param_has_a_consistent_table_row", "[edit_types]") {
    for (int i = 0; i < k_edit_param_count; ++i) {
        const auto param = static_cast<EditParam>(i);
        const ParamInfo& info = param_info(param);
        CHECK(info.param == param);
        CHECK_FALSE((info.allows_add && info.allows_scale));
        if (info.kind == ValueKind::ratio && info.allows_scale) {
            CHECK(info.neutral == 1.0);
        }
        if (param == EditParam::within_note_flatten) {  // an amount: set only, neutral 0
            CHECK_FALSE(info.allows_scale);
            CHECK(info.neutral == 0.0);
        }
        if (info.kind == ValueKind::cents || info.kind == ValueKind::decibels ||
            info.kind == ValueKind::seconds) {
            CHECK(info.allows_add);
            CHECK(info.neutral == 0.0);
        }
    }
}

TEST_CASE("param_table_places_parameters_in_their_spec_categories", "[edit_types]") {
    CHECK(param_info(EditParam::center_offset).category == EditCategory::pitch);
    CHECK(param_info(EditParam::gain).category == EditCategory::amplitude);
    CHECK(param_info(EditParam::within_note_flatten).category == EditCategory::amplitude);
    CHECK(param_info(EditParam::noise_gain_breath).category == EditCategory::sibilant);
    CHECK(param_info(EditParam::time_handle).category == EditCategory::time);
    CHECK(param_info(EditParam::attack_time_scale).category == EditCategory::attack);
}

TEST_CASE("a_well_formed_spec_validates", "[edit_types]") {
    CHECK(validate_for_add(pitch_add("a", 20.0)).has_value());
    CHECK(validate_for_add(make_spec("a", EditCategory::amplitude, EditParam::within_note_flatten,
                                     EditOp::set, Ratio{0.5}))
              .has_value());
    CHECK(validate_for_add(make_spec("a", EditCategory::fade, EditParam::fade_shape, EditOp::set,
                                     FadeShape::log))
              .has_value());
    CHECK(validate_for_add(make_spec("a", EditCategory::time, EditParam::time_handle, EditOp::set,
                                     TimePair{src(1.0), out(1.2)}))
              .has_value());
}

TEST_CASE("spec_rejects_param_in_the_wrong_category", "[edit_types]") {
    auto spec = pitch_add("a", 1.0);
    spec.category = EditCategory::amplitude;
    CHECK(validate(spec).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("spec_rejects_ops_the_parameter_does_not_allow", "[edit_types]") {
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset,
                                   EditOp::scale, Cents{2.0}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch_mod, EditParam::mod_scale, EditOp::add,
                                   Ratio{0.1}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::amplitude, EditParam::within_note_flatten,
                                   EditOp::scale, Ratio{0.5}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::fade, EditParam::fade_shape, EditOp::add,
                                   FadeShape::linear))
                    .has_value());
}

TEST_CASE("spec_rejects_a_value_of_the_wrong_type", "[edit_types]") {
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset, EditOp::set,
                                   Ratio{1.0}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset, EditOp::set,
                                   std::monostate{}))
                    .has_value());
}

TEST_CASE("spec_rejects_non_finite_and_out_of_range_values", "[edit_types]") {
    CHECK_FALSE(validate(pitch_add("a", k_nan)).has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::amplitude, EditParam::within_note_flatten,
                                   EditOp::set, Ratio{1.5}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::time, EditParam::length_scale, EditOp::set,
                                   Ratio{0.0}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::time, EditParam::length_scale, EditOp::scale,
                                   Ratio{-1.0}))
                    .has_value());
    CHECK_FALSE(
        validate(make_spec("a", EditCategory::fade, EditParam::fade_in, EditOp::set, Seconds{-0.1}))
            .has_value());
    // mod_scale may be negative (inverted vibrato).
    CHECK(validate(make_spec("a", EditCategory::pitch_mod, EditParam::mod_scale, EditOp::set,
                             Ratio{-1.0}))
              .has_value());
}

TEST_CASE("reset_takes_no_value_except_a_handle_to_remove", "[edit_types]") {
    CHECK(
        validate(make_spec("a", EditCategory::pitch, std::nullopt, EditOp::reset, std::monostate{}))
            .has_value());
    CHECK(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset, EditOp::reset,
                             std::monostate{}))
              .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset,
                                   EditOp::reset, Cents{1.0}))
                    .has_value());
    CHECK(validate(make_spec("a", EditCategory::time, EditParam::time_handle, EditOp::reset,
                             TimePair{src(1.0), out(1.0)}))
              .has_value());
}

TEST_CASE("param_may_be_empty_only_for_reset", "[edit_types]") {
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, std::nullopt, EditOp::set, Cents{1}))
                    .has_value());
}

TEST_CASE("target_rules_differ_between_add_and_recorded", "[edit_types]") {
    auto spec = pitch_add("a", 1.0);
    spec.target.segment_ids.clear();
    CHECK_FALSE(validate(spec).has_value());  // nothing targeted at all
    spec.target.orphaned_segment_ids = {segment_id(NoteId{"gone"})};
    CHECK(validate(spec).has_value());                // recorded edit with only orphans
    CHECK_FALSE(validate_for_add(spec).has_value());  // but not accepted by add
    spec = pitch_add("a", 1.0);
    spec.target.segment_ids.push_back(segment_id(NoteId{"a"}));
    CHECK_FALSE(validate(spec).has_value());  // duplicate id
    spec = pitch_add("a", 1.0);
    spec.origin.clear();
    CHECK_FALSE(validate(spec).has_value());
}

TEST_CASE("noise_gain_for_silence_is_zero_db", "[edit_types]") {
    const NoiseGains gains{.voiced = Decibels{1},
                           .sibilant = Decibels{2},
                           .breath = Decibels{3},
                           .plosive = Decibels{4}};
    CHECK(noise_gain(gains, FrameClass::voiced) == Decibels{1});
    CHECK(noise_gain(gains, FrameClass::sibilant) == Decibels{2});
    CHECK(noise_gain(gains, FrameClass::breath) == Decibels{3});
    CHECK(noise_gain(gains, FrameClass::plosive) == Decibels{4});
    CHECK(noise_gain(gains, FrameClass::silence) == Decibels{0});
    // ADR-0017: breathy and rough are voiced phonation and follow the voiced gain.
    CHECK(noise_gain(gains, FrameClass::breathy) == Decibels{1});
    CHECK(noise_gain(gains, FrameClass::rough) == Decibels{1});
}

TEST_CASE("edit_names_round_trip_for_every_enumerator", "[edit_types]") {
    for (int i = 0; i < k_edit_param_count; ++i) {
        const auto param = static_cast<EditParam>(i);
        CHECK(parse_edit_param(to_string(param)) == param);
        CHECK(to_string(param) != "unknown");
    }
    for (int i = 0; i <= static_cast<int>(EditCategory::breath_assignment); ++i) {
        const auto category = static_cast<EditCategory>(i);
        CHECK(parse_edit_category(to_string(category)) == category);
    }
    for (const EditOp op : {EditOp::set, EditOp::add, EditOp::scale, EditOp::reset}) {
        CHECK(parse_edit_op(to_string(op)) == op);
    }
    for (const FadeShape shape : {FadeShape::equal_power, FadeShape::linear, FadeShape::log}) {
        CHECK(parse_fade_shape(to_string(shape)) == shape);
    }
    CHECK_FALSE(parse_edit_param("pitch").has_value());
    CHECK_FALSE(parse_edit_category("").has_value());
    CHECK_FALSE(parse_edit_op("Set").has_value());
}

TEST_CASE("segment_ids_keep_note_and_gap_apart", "[edit_types]") {
    const SegmentId note = segment_id(NoteId{"x"});
    const SegmentId gap{.kind = SegmentKind::gap, .value = "x"};
    CHECK(note != gap);
    CHECK(as_note_id(note) == NoteId{"x"});
    CHECK_FALSE(as_note_id(gap).has_value());
}

namespace {

SegmentId gap_id(const std::string& text) {
    return SegmentId{.kind = SegmentKind::gap, .value = text};
}

EditSpec gap_spec(EditOp op, EditValue value, const std::string& gap = "g") {
    EditSpec spec = make_spec("unused", EditCategory::gap, EditParam::gap_gain, op, value);
    spec.target.segment_ids = {gap_id(gap)};
    return spec;
}

}  // namespace

TEST_CASE("the_new_categories_accept_only_a_parameterless_reset", "[edit_types]") {
    for (const EditCategory category :
         {EditCategory::gap, EditCategory::layout, EditCategory::breath, EditCategory::breath_chain,
          EditCategory::breath_assignment}) {
        // No pitch parameter belongs to them; gap_gain belongs to `gap` only (below).
        EditSpec set = make_spec("a", category, EditParam::center_offset, EditOp::set, Cents{1.0});
        CHECK_FALSE(validate_for_add(set).has_value());
        EditSpec none = make_spec("a", category, std::nullopt, EditOp::set, Cents{1.0});
        CHECK_FALSE(validate_for_add(none).has_value());
        EditSpec reset = make_spec("a", category, std::nullopt, EditOp::reset, EditValue{});
        CHECK(validate_for_add(reset).has_value());  // any target, as before ADR-0024
    }
    EditSpec gap_reset = gap_spec(EditOp::reset, EditValue{});
    gap_reset.param = std::nullopt;
    CHECK(validate_for_add(gap_reset).has_value());
}

TEST_CASE("gap_gain_is_a_decibel_offset_of_the_gap_category", "[edit_types]") {
    const ParamInfo& info = param_info(EditParam::gap_gain);
    CHECK(info.category == EditCategory::gap);
    CHECK(info.kind == ValueKind::decibels);
    CHECK(info.allows_add);
    CHECK_FALSE(info.allows_scale);
    CHECK(info.neutral == 0.0);
    CHECK(info.set_min == k_gap_mute_db.value());
    CHECK(info.set_max == 24.0);
    CHECK(parse_edit_param("gap_gain") == EditParam::gap_gain);
    CHECK(to_string(EditParam::gap_gain) == "gap_gain");
}

TEST_CASE("gap_gain_set_is_limited_to_mute_and_plus_24_db", "[edit_types]") {
    CHECK(validate(gap_spec(EditOp::set, Decibels{-120.0})).has_value());
    CHECK(validate(gap_spec(EditOp::set, Decibels{24.0})).has_value());
    CHECK_FALSE(validate(gap_spec(EditOp::set, Decibels{-120.5})).has_value());
    CHECK_FALSE(validate(gap_spec(EditOp::set, Decibels{24.5})).has_value());
    CHECK(validate(gap_spec(EditOp::add, Decibels{-6.0})).has_value());
    CHECK_FALSE(validate(gap_spec(EditOp::scale, Ratio{2.0})).has_value());
    CHECK_FALSE(validate(gap_spec(EditOp::set, Cents{1.0})).has_value());  // wrong value type
}

TEST_CASE("note_categories_refuse_gap_targets", "[edit_types]") {
    EditSpec on_gap = pitch_add("a", 1.0);
    on_gap.target.segment_ids = {gap_id("a")};
    CHECK_FALSE(validate(on_gap).has_value());

    EditSpec mixed = pitch_add("a", 1.0);
    mixed.target.segment_ids.push_back(gap_id("a"));  // same text, different kind
    CHECK_FALSE(validate(mixed).has_value());

    EditSpec orphaned = pitch_add("a", 1.0);
    orphaned.target.orphaned_segment_ids = {gap_id("b")};
    CHECK_FALSE(validate(orphaned).has_value());

    // A category reset follows the same rule (symmetric): a note category on a gap id.
    EditSpec reset = make_spec("a", EditCategory::pitch, std::nullopt, EditOp::reset, EditValue{});
    reset.target.segment_ids = {gap_id("a")};
    CHECK_FALSE(validate(reset).has_value());
    reset.target.segment_ids = {segment_id(NoteId{"a"})};
    CHECK(validate(reset).has_value());
}

TEST_CASE("gap_gain_refuses_note_targets", "[edit_types]") {
    for (const EditOp op : {EditOp::set, EditOp::add}) {
        EditSpec on_note = gap_spec(op, Decibels{-3.0});
        on_note.target.segment_ids = {segment_id(NoteId{"a"})};
        CHECK_FALSE(validate(on_note).has_value());
    }
    EditSpec mixed = gap_spec(EditOp::set, Decibels{-3.0});
    mixed.target.segment_ids.push_back(segment_id(NoteId{"a"}));
    CHECK_FALSE(validate(mixed).has_value());

    EditSpec orphaned_note = gap_spec(EditOp::set, Decibels{-3.0});
    orphaned_note.target.orphaned_segment_ids = {segment_id(NoteId{"a"})};
    CHECK_FALSE(validate(orphaned_note).has_value());

    // A reset of the parameter (not the whole category) follows the rule too.
    EditSpec reset_param = gap_spec(EditOp::reset, EditValue{});
    reset_param.target.segment_ids = {segment_id(NoteId{"a"})};
    CHECK_FALSE(validate(reset_param).has_value());
    reset_param.target.segment_ids = {gap_id("g")};
    CHECK(validate(reset_param).has_value());
}

TEST_CASE("an_orphaned_gap_id_is_legal_in_a_recorded_edit_but_not_a_new_one", "[edit_types]") {
    EditSpec spec = gap_spec(EditOp::set, Decibels{-3.0});
    spec.target.orphaned_segment_ids = {gap_id("old")};
    CHECK(validate(spec).has_value());
    CHECK_FALSE(validate_for_add(spec).has_value());
}

TEST_CASE("center_offset_set_is_limited_to_the_pitch_offset_gate", "[edit_types]") {
    const auto& info = param_info(EditParam::center_offset);
    CHECK(info.set_min == -k_max_pitch_offset.value());
    CHECK(info.set_max == k_max_pitch_offset.value());
    CHECK(k_max_pitch_offset == Cents{2400.0});
    CHECK(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset, EditOp::set,
                             k_max_pitch_offset))
              .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset, EditOp::set,
                                   Cents{2400.5}))
                    .has_value());
    CHECK_FALSE(validate(make_spec("a", EditCategory::pitch, EditParam::center_offset, EditOp::set,
                                   Cents{-2400.5}))
                    .has_value());
}
