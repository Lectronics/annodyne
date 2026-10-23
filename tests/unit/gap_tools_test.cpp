#include "core/edits/edit_list.h"
#include "core/edits/effective_gap.h"
#include "macros/strip_silence/strip_silence.h"
#include "support/test_data.h"
#include "support/timeline_data.h"
#include "tools/gap/gap_tool.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
using K = GapRegionKind;

SegmentId gap_id(const char* before, const char* after) {
    return gap_segment_id(before != nullptr ? std::optional<NoteId>{NoteId{before}} : std::nullopt,
                          after != nullptr ? std::optional<NoteId>{NoteId{after}} : std::nullopt);
}

AnalysisResult analysis_with_timeline() {
    AnalysisResult a;
    a.source_id = SourceId{"s"};
    a.notes = {make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)};
    a.timeline = timeline_of(a.notes, 5.0);  // gaps [0,1], [2,3], [4,5]
    return a;
}

EditList edits_of(const std::vector<EditSpec>& group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

}  // namespace

TEST_CASE("a_gain_gesture_is_one_gap_gain_edit", "[gap_tool]") {
    const AnalysisResult a = analysis_with_timeline();
    const EditSpec set =
        gap_gain_edit(a, {.gap = gap_id("a", "b"), .gain = Decibels{-6.0}}).value();
    CHECK(set.origin == "gap_tool");
    CHECK(set.category == EditCategory::gap);
    CHECK(set.param == EditParam::gap_gain);
    CHECK(set.op == EditOp::set);
    CHECK(set.target.segment_ids == std::vector<SegmentId>{gap_id("a", "b")});
    CHECK(validate_for_add(set).has_value());
    const EditSpec add =
        gap_gain_edit(a, {.gap = gap_id("a", "b"), .gain = Decibels{-3.0}, .op = GapGainOp::add})
            .value();
    CHECK(add.op == EditOp::add);
}

TEST_CASE("mute_and_unmute_fold_to_the_mute_and_back", "[gap_tool]") {
    const AnalysisResult a = analysis_with_timeline();
    const SegmentId gap = gap_id("a", "b");
    const EditSpec mute = gap_mute_edit(a, gap).value();
    CHECK(mute.op == EditOp::set);
    const EditSpec unmute = gap_unmute_edit(a, gap).value();
    CHECK(unmute.op == EditOp::reset);
    CHECK(unmute.param == EditParam::gap_gain);
    CHECK(effective_gap(gap, edits_of({mute})).gain == k_gap_mute_db);
    EditList list = edits_of({mute});
    list = list.add(std::vector{unmute}, k_t0).value();
    const EffectiveGap back = effective_gap(gap, list);
    CHECK(back.gain == Decibels{0.0});
    CHECK_FALSE(back.is_edited);
    CHECK(list.active().size() == 2);  // history kept
}

TEST_CASE("gap_tool_errors_are_named", "[gap_tool]") {
    AnalysisResult a = analysis_with_timeline();
    const auto code = [&](const GapGain& g) { return gap_gain_edit(a, g).error().code; };
    CHECK(code({.gap = gap_id("x", "y"), .gain = Decibels{-6.0}}) == ErrorCode::invalid_argument);
    CHECK(code({.gap = segment_id(NoteId{"a"}), .gain = Decibels{-6.0}}) ==
          ErrorCode::invalid_argument);
    CHECK(code({.gap = gap_id("a", "b"), .gain = Decibels{std::nan("")}}) ==
          ErrorCode::invalid_argument);
    CHECK(code({.gap = gap_id("a", "b"), .gain = Decibels{-200.0}}) == ErrorCode::invalid_argument);
    CHECK(code({.gap = gap_id("a", "b"), .gain = Decibels{30.0}}) == ErrorCode::invalid_argument);
    CHECK(
        gap_gain_edit(a, {.gap = gap_id("a", "b"), .gain = Decibels{-500.0}, .op = GapGainOp::add})
            .has_value());  // an add may be any finite change
    a.timeline.reset();
    CHECK(code({.gap = gap_id("a", "b"), .gain = Decibels{-6.0}}) == ErrorCode::invalid_argument);
    CHECK(gap_mute_edit(a, gap_id("a", "b")).error().code == ErrorCode::invalid_argument);
    CHECK(gap_unmute_edit(a, gap_id("a", "b")).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("strip_silence_mutes_every_gap_with_enough_silence_in_one_edit", "[strip_silence]") {
    const AnalysisResult a = analysis_with_timeline();
    const auto edits = strip_silence_edits(a, {}).value();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].origin == "strip_silence");
    CHECK(edits[0].op == EditOp::set);
    CHECK(edits[0].target.segment_ids ==
          std::vector<SegmentId>{gap_id(nullptr, "a"), gap_id("a", "b"), gap_id("b", nullptr)});
    CHECK(validate_for_add(edits[0]).has_value());
    const EditList list = edits_of(edits);
    for (const Gap& g : a.timeline->gaps()) {
        CHECK(effective_gap(g.id, list).gain == k_gap_mute_db);
    }
}

TEST_CASE("strip_silence_respects_scope_and_the_minimum", "[strip_silence]") {
    const AnalysisResult a = analysis_with_timeline();
    const auto between = strip_silence_edits(a, {.scope = StripScope::between_notes}).value();
    REQUIRE(between.size() == 1);
    CHECK(between[0].target.segment_ids == std::vector<SegmentId>{gap_id("a", "b")});
    // every gap is 1 s: a minimum above that qualifies none
    CHECK(strip_silence_edits(a, {.min_silence = Seconds{1.5}}).value().empty());
    CHECK(strip_silence_edits(a, {.min_silence = Seconds{1.0}}).value().size() == 1);
}

TEST_CASE("strip_silence_counts_only_silence_regions", "[strip_silence]") {
    AnalysisResult a = analysis_with_timeline();
    // gap [2,3]: 0.2 s tail, 0.1 s silence, 0.7 s sibilant: 0.1 s of silence only
    std::vector<Segment> segments;
    std::vector<Gap> gaps;
    gaps.push_back(gap_between(nullptr, "a", 0.0, 1.0));
    Gap mid = gap_between("a", "b", 2.0, 3.0);
    mid.regions = {{.kind = K::tail, .start = src(2.0), .end = src(2.2)},
                   {.kind = K::silence, .start = src(2.2), .end = src(2.3)},
                   {.kind = K::sibilant, .start = src(2.3), .end = src(3.0)}};
    gaps.push_back(mid);
    gaps.push_back(gap_between("b", nullptr, 4.0, 5.0));
    segments = {std::size_t{0}, note_seg("a", 1.0, 2.0), std::size_t{1}, note_seg("b", 3.0, 4.0),
                std::size_t{2}};
    a.timeline = SegmentTimeline::make(segments, gaps, src(5.0)).value();
    const auto edits = strip_silence_edits(a, {.min_silence = Seconds{0.5}}).value();
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].target.segment_ids ==
          std::vector<SegmentId>{gap_id(nullptr, "a"), gap_id("b", nullptr)});
}

TEST_CASE("strip_silence_takes_a_gain_and_names_its_errors", "[strip_silence]") {
    AnalysisResult a = analysis_with_timeline();
    const auto soft = strip_silence_edits(a, {.gain = Decibels{-20.0}}).value();
    REQUIRE(soft.size() == 1);
    CHECK(effective_gap(gap_id("a", "b"), edits_of(soft)).gain == Decibels{-20.0});
    CHECK(strip_silence_edits(a, {.min_silence = Seconds{-1.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(strip_silence_edits(a, {.gain = Decibels{-300.0}}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(strip_silence_edits(a, {.gain = Decibels{std::nan("")}}).error().code ==
          ErrorCode::invalid_argument);
    a.timeline.reset();
    CHECK(strip_silence_edits(a, {}).error().code == ErrorCode::invalid_argument);
    AnalysisResult none;
    none.timeline = SegmentTimeline::make({}, {}, src(0.0)).value();
    CHECK(strip_silence_edits(none, {}).value().empty());
}
