#include "core/edits/edit_list.h"
#include "core/edits/effective_state.h"
#include "core/timemap/build_time_map.h"
#include "support/test_data.h"
#include "tools/attack_speed/attack_speed.h"
#include "tools/time/time_tool.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

// make_note: attack = the first 10 % of the note. Note "a" is 1.0 .. 2.0 (attack to 1.1).
AnalysisResult analysis_of(std::vector<Note> notes) {
    AnalysisResult a;
    a.notes = std::move(notes);
    return a;
}

AnalysisResult two_notes() {
    return analysis_of({make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)});
}

EditList list_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

double out_of(const TimeMap& m, double s) {
    return (m.to_output(src(s)).value() - OutputTime{}).value();
}

/// The map of `notes` under `edits`: no known end (the tail shifts), no rounding: exact.
TimeMapBuild exact_map(const AnalysisResult& a, const EditList& edits) {
    return build_time_map(a.notes, edits, TimeMapParams{}).value();
}

NoteMove mv(const char* id, double shift_s) {
    return {.note = NoteId{id}, .shift = Seconds{shift_s}};
}
NoteLength ln(const char* id, double body_s) {
    return {.note = NoteId{id}, .body_length = Seconds{body_s}};
}
AttackSpeed atk(const char* id, double scale) {
    return {.note = NoteId{id}, .time_scale = Ratio{scale}};
}
NoteHandle hd(const char* id, double source_s, double output_s) {
    return {.note = NoteId{id},
            .handle = {.source = src(source_s),
                       .output = OutputTime::unchecked_from_origin(Seconds{output_s})}};
}

ErrorCode code_of(const Result<EditSpec>& r) {
    return r.error().code;
}

}  // namespace

TEST_CASE("a_move_is_an_onset_shift_set_on_the_note", "[time_tools]") {
    const auto a = two_notes();
    const auto spec = note_move_edit(a, mv("a", 0.25)).value();
    CHECK(spec.origin == "time_tool");
    CHECK(spec.category == EditCategory::time);
    CHECK(spec.param == EditParam::onset_shift);
    CHECK(spec.op == EditOp::set);
    CHECK(std::get<Seconds>(spec.value) == Seconds{0.25});
    CHECK(spec.target.segment_ids == segments_of({NoteId{"a"}}));
    CHECK(validate_for_add(spec).has_value());
}

TEST_CASE("a_length_edit_scales_the_body_only", "[time_tools]") {
    const auto a = two_notes();
    // the body of "a" is 1.1 .. 2.0 = 0.9 s; asking for 1.8 s is a scale of 2
    const auto spec = note_length_edit(a, ln("a", 1.8)).value();
    CHECK(spec.param == EditParam::length_scale);
    CHECK(spec.op == EditOp::set);
    CHECK(std::abs(std::get<Ratio>(spec.value).value() - 2.0) < 1e-12);
}

TEST_CASE("a_handle_is_a_set_time_handle_and_its_removal_a_reset_with_the_source", "[time_tools]") {
    const auto a = two_notes();
    const TimePair pair{.source = src(1.5),
                        .output = OutputTime::unchecked_from_origin(Seconds{1.8})};
    const auto set = note_handle_edit(a, {.note = NoteId{"a"}, .handle = pair}).value();
    CHECK(set.param == EditParam::time_handle);
    CHECK(set.op == EditOp::set);
    CHECK(std::get<TimePair>(set.value) == pair);
    const auto reset =
        note_handle_remove_edit(a, {.note = NoteId{"a"}, .source = src(1.5)}).value();
    CHECK(reset.op == EditOp::reset);
    CHECK(std::get<TimePair>(reset.value).source == src(1.5));
    CHECK(validate_for_add(reset).has_value());
}

TEST_CASE("an_attack_speed_edit_sets_the_attack_time_scale", "[time_tools]") {
    const auto a = two_notes();
    const auto spec = attack_speed_edit(a, atk("a", 0.5)).value();
    CHECK(spec.origin == "attack_speed_tool");
    CHECK(spec.category == EditCategory::attack);
    CHECK(spec.param == EditParam::attack_time_scale);
    CHECK(std::get<Ratio>(spec.value) == Ratio{0.5});
}

TEST_CASE("the_tools_refuse_unknown_inactive_and_non_finite_input", "[time_tools]") {
    auto a = two_notes();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(code_of(note_move_edit(a, mv("zz", 0.1))) == ErrorCode::invalid_argument);
    CHECK(code_of(note_move_edit(a, mv("a", nan))) == ErrorCode::invalid_argument);
    CHECK(code_of(note_length_edit(a, ln("a", nan))) == ErrorCode::invalid_argument);
    CHECK(code_of(note_length_edit(a, ln("a", 0.0))) == ErrorCode::invalid_argument);
    CHECK(code_of(note_length_edit(a, ln("a", -1.0))) == ErrorCode::invalid_argument);
    CHECK(code_of(attack_speed_edit(a, atk("a", nan))) == ErrorCode::invalid_argument);
    CHECK(code_of(attack_speed_edit(a, atk("a", 0.0))) == ErrorCode::invalid_argument);
    CHECK(code_of(attack_speed_edit(a, atk("zz", 1.0))) == ErrorCode::invalid_argument);
    CHECK(
        code_of(note_handle_remove_edit(
            a, {.note = NoteId{"a"}, .source = SourceTime::unchecked_from_origin(Seconds{nan})})) ==
        ErrorCode::invalid_argument);
    a.notes[0].is_active = false;
    CHECK(code_of(note_move_edit(a, mv("a", 0.1))) == ErrorCode::invalid_argument);
    CHECK(code_of(attack_speed_edit(a, atk("a", 1.5))) == ErrorCode::invalid_argument);
}

TEST_CASE("a_scale_below_the_smallest_is_out_of_range_not_invalid", "[time_tools]") {
    const auto a = two_notes();
    CHECK(code_of(note_length_edit(a, ln("a", 0.9e-4))) == ErrorCode::out_of_range);
    CHECK(code_of(attack_speed_edit(a, atk("a", 1e-4))) == ErrorCode::out_of_range);
    CHECK(note_length_edit(a, ln("a", 1e6)).has_value());  // huge is accepted
}

TEST_CASE("a_note_without_a_body_or_an_attack_refuses_the_matching_tool", "[time_tools]") {
    auto a = two_notes();
    a.notes[0].attack_end = a.notes[0].source_end;
    a.notes[0].release_start = a.notes[0].source_end;
    CHECK(code_of(note_length_edit(a, ln("a", 1.0))) == ErrorCode::invalid_argument);
    auto b = two_notes();
    b.notes[0].attack_end = b.notes[0].source_start;
    CHECK(code_of(attack_speed_edit(b, atk("a", 2.0))) == ErrorCode::invalid_argument);
}

TEST_CASE("a_handle_must_lie_inside_the_body_and_have_a_valid_output", "[time_tools]") {
    const auto a = two_notes();
    const auto handle = [&](double source, double output) {
        return code_of(note_handle_edit(a, hd("a", source, output)));
    };
    CHECK(handle(1.0, 1.0) == ErrorCode::invalid_argument);         // at the note start
    CHECK(handle(1.1, 1.1) == ErrorCode::invalid_argument);         // at the attack end
    CHECK(handle(1.1 + 5e-7, 1.1) == ErrorCode::invalid_argument);  // within k_handle_key of it
    CHECK(handle(2.0, 2.0) == ErrorCode::invalid_argument);         // at the end
    CHECK(handle(2.5, 2.5) == ErrorCode::invalid_argument);         // outside
    CHECK(handle(1.5, -0.1) == ErrorCode::invalid_argument);        // negative output
    CHECK(handle(1.5, std::nan("")) == ErrorCode::invalid_argument);
    CHECK(note_handle_edit(a, hd("a", 1.5, 1.6)).has_value());
}

TEST_CASE("a_removal_of_a_stale_handle_outside_the_body_is_accepted", "[time_tools]") {
    const auto a = two_notes();
    CHECK(note_handle_remove_edit(a, {.note = NoteId{"a"}, .source = src(2.5)}).has_value());
}

TEST_CASE("a_second_move_or_length_replaces_the_first_through_the_edit_list", "[time_tools]") {
    const auto a = two_notes();
    const auto m1 = note_move_edit(a, mv("a", 0.2)).value();
    const auto m2 = note_move_edit(a, mv("a", 0.5)).value();
    const auto l1 = note_length_edit(a, ln("a", 1.8)).value();
    const auto l2 = note_length_edit(a, ln("a", 0.45)).value();
    const auto edits =
        list_of({m1}).add(std::vector{m2, l1}, k_t0).value().add(std::vector{l2}, k_t0).value();
    const EffectiveNote eff = effective_state(a.notes[0], edits);
    CHECK(eff.onset_shift == Seconds{0.5});
    CHECK(std::abs(eff.length_scale.value() - 0.5) < 1e-12);
}

TEST_CASE("handles_are_keyed_by_source_sorted_and_removable", "[time_tools]") {
    const auto a = two_notes();
    const auto handle = [&](double s, double o) {
        return note_handle_edit(a, hd("a", s, o)).value();
    };
    EditList edits = list_of({handle(1.7, 1.9)});
    edits = edits.add(std::vector{handle(1.4, 1.5)}, k_t0).value();
    edits = edits.add(std::vector{handle(1.7 + 5e-7, 2.0)}, k_t0).value();  // same key: replaces
    EffectiveNote eff = effective_state(a.notes[0], edits);
    REQUIRE(eff.handles.size() == 2);
    CHECK(eff.handles[0].source == src(1.4));  // sorted by source
    CHECK(eff.handles[1].output == OutputTime::unchecked_from_origin(Seconds{2.0}));
    edits =
        edits
            .add(
                std::vector{
                    note_handle_remove_edit(a, {.note = NoteId{"a"}, .source = src(1.4)}).value()},
                k_t0)
            .value();
    CHECK(effective_state(a.notes[0], edits).handles.size() == 1);
    // removing a handle that is not there is a no-op
    edits =
        edits
            .add(
                std::vector{
                    note_handle_remove_edit(a, {.note = NoteId{"a"}, .source = src(1.95)}).value()},
                k_t0)
            .value();
    CHECK(effective_state(a.notes[0], edits).handles.size() == 1);
}

// ---- Through build_time_map: no known end (the tail shifts) and no rounding, so the answers are
// exact.

TEST_CASE("a_move_shifts_the_note_in_the_map_exactly", "[time_tools]") {
    const auto a = two_notes();
    const auto built = exact_map(a, list_of({note_move_edit(a, mv("a", 0.25)).value()}));
    CHECK(std::abs(out_of(built.map, 1.0) - 1.25) < 1e-9);
    CHECK(std::abs(out_of(built.map, 2.0) - 2.25) < 1e-9);
    CHECK(std::abs(out_of(built.map, 3.0) - 3.25) < 1e-9);  // the tail is shifted, not squeezed
    CHECK(built.repaired_notes == 0);
}

TEST_CASE("a_length_edit_makes_the_body_that_long_and_keeps_the_attack", "[time_tools]") {
    const auto a = two_notes();
    const auto built = exact_map(a, list_of({note_length_edit(a, ln("a", 1.8)).value()}));
    CHECK(std::abs(out_of(built.map, 1.1) - 1.1) < 1e-9);  // the attack is unchanged
    CHECK(std::abs((out_of(built.map, 2.0) - out_of(built.map, 1.1)) - 1.8) < 1e-9);
    CHECK(built.repaired_notes == 0);
}

TEST_CASE("a_handle_puts_its_source_instant_at_its_output_time", "[time_tools]") {
    const auto a = two_notes();
    const auto handle = note_handle_edit(a, hd("a", 1.5, 1.7)).value();
    const auto built = exact_map(a, list_of({handle}));
    CHECK(std::abs(out_of(built.map, 1.5) - 1.7) < 1e-9);
    CHECK(built.repaired_notes == 0);
}

TEST_CASE("an_attack_speed_edit_changes_only_the_attack_segment", "[time_tools]") {
    const auto a = two_notes();
    const auto built = exact_map(a, list_of({attack_speed_edit(a, atk("a", 2.0)).value()}));
    CHECK(std::abs(out_of(built.map, 1.1) - 1.2) < 1e-9);  // the 0.1 s attack plays 0.2 s long
    CHECK(std::abs((out_of(built.map, 2.0) - out_of(built.map, 1.1)) - 0.9) < 1e-9);  // body as is
}

TEST_CASE("with_a_sample_rate_the_shift_lands_on_a_whole_sample", "[time_tools]") {
    const auto a = two_notes();
    const double rate = 48000.0;
    const auto built =
        build_time_map(a.notes, list_of({note_move_edit(a, mv("a", 0.2500123)).value()}),
                       TimeMapParams{.source_end = src(5.0),
                                     .tail = TailPolicy::shift,
                                     .sample_rate = Hz{rate}})
            .value();
    const double d = (out_of(built.map, 3.5) - 3.5) * rate;
    CHECK(std::abs(d - std::round(d)) < 1e-6);
    CHECK(std::abs(d - 0.2500123 * rate) <= 0.5 + 1e-9);
}

TEST_CASE("with_a_fixed_end_a_move_still_stretches_the_unedited_neighbour_until_the_layout_solver",
          "[time_tools]") {
    // Recorded behaviour (ADR-0005 §16, to be replaced by the shift-first layout of ADR-0008 §4).
    const auto a = two_notes();
    const auto built = build_time_map(a.notes, list_of({note_move_edit(a, mv("a", 0.25)).value()}),
                                      TimeMapParams{.source_end = src(5.0)})
                           .value();
    const TimeMap& m = built.map;
    CHECK(out_of(m, 5.0) == 5.0);
    CHECK(std::abs(m.slope_at(*m.to_output(src(3.5))).value() - 1.0) > 1e-3);  // b is squeezed
}

TEST_CASE("a_handle_set_before_a_move_is_repaired_and_counted", "[time_tools]") {
    // Handles are absolute output anchors; the move shifts the note's other anchors past it.
    const auto a = two_notes();
    const auto handle = note_handle_edit(a, hd("a", 1.5, 1.55)).value();
    const auto move = note_move_edit(a, mv("a", 0.5)).value();
    const auto built = exact_map(a, list_of({handle}).add(std::vector{move}, k_t0).value());
    CHECK(built.repaired_notes == 1);
}

TEST_CASE("a_sub_threshold_shift_and_a_unit_scale_fold_to_no_edit", "[time_tools]") {
    const auto a = two_notes();
    const auto tiny = list_of({note_move_edit(a, mv("a", 1e-10)).value()});
    CHECK_FALSE(effective_state(a.notes[0], tiny).is_edited);
    const auto unit = list_of({attack_speed_edit(a, atk("a", 1.0)).value()});
    CHECK_FALSE(effective_state(a.notes[0], unit).is_edited);
}
