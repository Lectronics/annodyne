#include "core/edits/edit_list.h"
#include "core/edits/effective_state.h"
#include "support/test_data.h"
#include "tools/main_tool/main_tool.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace anno;
using namespace anno::test;

namespace {

AnalysisResult analysis_of(std::vector<Note> notes) {
    AnalysisResult a;
    a.notes = std::move(notes);
    return a;
}

double offset_of(const EditSpec& spec) {
    return std::get<Cents>(spec.value).value();
}

}  // namespace

TEST_CASE("a_drag_sets_the_offset_from_the_analysed_target_to_the_dropped_pitch", "[main_tool]") {
    const auto a = analysis_of({make_note("n1", 0.0, 1.0, 6900.0)});
    const auto spec = pitch_drag_edit(a, {.note = NoteId{"n1"}, .target = Cents{7100.0}}).value();
    CHECK(spec.origin == "main_tool");
    CHECK(spec.category == EditCategory::pitch);
    CHECK(spec.param == EditParam::center_offset);
    CHECK(spec.op == EditOp::set);
    CHECK(offset_of(spec) == 200.0);
    CHECK(spec.target.segment_ids == segments_of({NoteId{"n1"}}));
    CHECK(validate_for_add(spec).has_value());
    // Down, and a drag back to the original: offset 0.
    CHECK(offset_of(pitch_drag_edit(a, {NoteId{"n1"}, Cents{6500.0}}).value()) == -400.0);
    CHECK(offset_of(pitch_drag_edit(a, {NoteId{"n1"}, Cents{6900.0}}).value()) == 0.0);
}

TEST_CASE("two_drags_in_a_row_land_on_the_second_target", "[main_tool]") {
    // pitch_center (6930) differs from the analysed target (6900): the baseline is the target.
    Note note = make_note("n1", 0.0, 1.0, 6930.0);
    note.pitch_target = make_whole_target(Cents{6900.0});
    const auto a = analysis_of({note});
    EditList edits{TrackId{"t1"}};
    for (const double target : {7100.0, 6700.0}) {
        const std::vector<EditSpec> group{
            pitch_drag_edit(a, {.note = NoteId{"n1"}, .target = Cents{target}}).value()};
        edits = edits.add(group, WallTime{}).value();
    }
    const EffectiveNote eff = effective_state(a.notes[0], edits);
    // The shift the engines apply is eff.center - pitch_center = drag.target - pitch_target.target.
    CHECK((eff.center - note.pitch_center).value() == -200.0);
}

TEST_CASE("glide_and_ornament_notes_use_their_main_target_and_shift_as_a_whole", "[main_tool]") {
    Note glide = make_note("g", 0.0, 1.0, 6950.0);
    glide.pitch_target.kind = PitchTargetKind::glide;
    glide.pitch_target.target = Cents{7000.0};
    glide.pitch_target.secondary = Cents{6900.0};
    Note orn = make_note("o", 1.0, 2.0, 6950.0);
    orn.pitch_target.kind = PitchTargetKind::ornament;
    orn.pitch_target.target = Cents{6900.0};
    orn.pitch_target.secondary = Cents{7000.0};
    const auto a = analysis_of({glide, orn});
    // One uniform center offset: the start of a glide and the other pitch of an ornament follow by
    // the same amount (the engines shift the whole curve; the tool writes one number).
    CHECK(offset_of(pitch_drag_edit(a, {NoteId{"g"}, Cents{7100.0}}).value()) == 100.0);
    CHECK(offset_of(pitch_drag_edit(a, {NoteId{"o"}, Cents{7100.0}}).value()) == 200.0);
}

TEST_CASE("notes_without_a_single_target_are_refused", "[main_tool]") {
    Note multi = make_note("m", 0.0, 1.0);
    multi.pitch_target.kind = PitchTargetKind::multi;
    Note none = make_note("u", 1.0, 2.0);
    none.pitch_target.kind = PitchTargetKind::unpitched;
    Note off = make_note("i", 2.0, 3.0);
    off.is_active = false;
    const auto a = analysis_of({multi, none, off});
    for (const char* id : {"m", "u", "i", "missing"}) {
        CHECK(pitch_drag_edit(a, {NoteId{id}, Cents{7000.0}}).error().code ==
              ErrorCode::invalid_argument);
    }
    // The UI suggests a split from this message.
    CHECK(pitch_drag_edit(a, {NoteId{"m"}, Cents{7000.0}}).error().message.find("split") !=
          std::string::npos);
    // A malformed note (non-finite target) is refused too, not turned into an edit.
    Note bad = make_note("b", 3.0, 4.0);
    bad.pitch_target.target = Cents{std::nan("")};
    CHECK_FALSE(pitch_drag_edit(analysis_of({bad}), {NoteId{"b"}, Cents{7000.0}}).has_value());
}

TEST_CASE("a_non_finite_target_is_refused", "[main_tool]") {
    const auto a = analysis_of({make_note("n1", 0.0, 1.0)});
    for (const double t :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        CHECK(pitch_drag_edit(a, {NoteId{"n1"}, Cents{t}}).error().code ==
              ErrorCode::invalid_argument);
    }
}

TEST_CASE("a_move_beyond_the_offset_limit_is_out_of_range_and_the_limit_itself_is_allowed",
          "[main_tool]") {
    const auto a = analysis_of({make_note("n1", 0.0, 1.0, 6900.0)});
    const double k = k_max_pitch_offset.value();
    CHECK(pitch_drag_edit(a, {NoteId{"n1"}, Cents{6900.0 + k + 0.5}}).error().code ==
          ErrorCode::out_of_range);
    CHECK(pitch_drag_edit(a, {NoteId{"n1"}, Cents{6900.0 - k - 0.5}}).error().code ==
          ErrorCode::out_of_range);
    CHECK(offset_of(pitch_drag_edit(a, {NoteId{"n1"}, Cents{6900.0 + k}}).value()) == k);
    CHECK(offset_of(pitch_drag_edit(a, {NoteId{"n1"}, Cents{6900.0 - k}}).value()) == -k);
}
