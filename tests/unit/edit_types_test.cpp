#include "core/types/edit.h"
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
        if (info.kind == ValueKind::ratio) {
            CHECK(info.allows_scale);
            CHECK(info.neutral == 1.0);
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
    CHECK_FALSE(validate(make_spec("a", EditCategory::fade, EditParam::fade_in, EditOp::set,
                                   Seconds{-0.1}))
                    .has_value());
    // mod_scale may be negative (inverted vibrato).
    CHECK(validate(make_spec("a", EditCategory::pitch_mod, EditParam::mod_scale, EditOp::set,
                             Ratio{-1.0}))
              .has_value());
}

TEST_CASE("reset_takes_no_value_except_a_handle_to_remove", "[edit_types]") {
    CHECK(validate(make_spec("a", EditCategory::pitch, std::nullopt, EditOp::reset,
                             std::monostate{}))
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
    spec.target.note_ids.clear();
    CHECK_FALSE(validate(spec).has_value());  // nothing targeted at all
    spec.target.orphaned_note_ids = {NoteId{"gone"}};
    CHECK(validate(spec).has_value());               // recorded edit with only orphans
    CHECK_FALSE(validate_for_add(spec).has_value()); // but not accepted by add
    spec = pitch_add("a", 1.0);
    spec.target.note_ids.push_back(NoteId{"a"});
    CHECK_FALSE(validate(spec).has_value());  // duplicate id
    spec = pitch_add("a", 1.0);
    spec.origin.clear();
    CHECK_FALSE(validate(spec).has_value());
}

TEST_CASE("noise_gain_for_silence_is_zero_db", "[edit_types]") {
    const NoiseGains gains{Decibels{1}, Decibels{2}, Decibels{3}, Decibels{4}};
    CHECK(noise_gain(gains, FrameClass::voiced) == Decibels{1});
    CHECK(noise_gain(gains, FrameClass::sibilant) == Decibels{2});
    CHECK(noise_gain(gains, FrameClass::breath) == Decibels{3});
    CHECK(noise_gain(gains, FrameClass::plosive) == Decibels{4});
    CHECK(noise_gain(gains, FrameClass::silence) == Decibels{0});
}
