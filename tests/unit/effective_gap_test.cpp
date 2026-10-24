#include "core/edits/effective_gap.h"
#include "core/types/effective_note.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

SegmentId gap_id(const char* text) {
    return SegmentId{.kind = SegmentKind::gap, .value = text};
}

EditSpec gap_edit(EditOp op, EditValue v, std::vector<SegmentId> targets = {gap_id("g")}) {
    EditSpec s = make_spec("unused", EditCategory::gap, EditParam::gap_gain, op, v);
    s.target.segment_ids = std::move(targets);
    return s;
}

EditSpec gap_reset(std::optional<EditParam> param, const SegmentId& target = gap_id("g")) {
    EditSpec s = make_spec("unused", EditCategory::gap, param, EditOp::reset, EditValue{});
    s.target.segment_ids = {target};
    return s;
}

EditList list_with(const std::vector<std::vector<EditSpec>>& groups) {
    EditList list{TrackId{"t1"}};
    for (const auto& g : groups) {
        list = list.add(g, k_t0).value();
    }
    return list;
}

}  // namespace

TEST_CASE("a_gap_without_edits_is_neutral", "[effective_gap]") {
    const EffectiveGap eff = effective_gap(gap_id("g"), list_with({}));
    CHECK(eff.gap == gap_id("g"));
    CHECK(eff.gain == Decibels{0.0});
    CHECK_FALSE(eff.is_edited);
}

TEST_CASE("set_and_add_fold_in_edit_order", "[effective_gap]") {
    CHECK(effective_gap(gap_id("g"), list_with({{gap_edit(EditOp::set, Decibels{-6.0})}})).gain ==
          Decibels{-6.0});
    const EditList list = list_with(
        {{gap_edit(EditOp::add, Decibels{-3.0})}, {gap_edit(EditOp::add, Decibels{-3.0})}});
    const EffectiveGap eff = effective_gap(gap_id("g"), list);
    CHECK(eff.gain == Decibels{-6.0});
    CHECK(eff.is_edited);
    const EditList later_set = list_with({{gap_edit(EditOp::add, Decibels{-3.0})},
                                          {gap_edit(EditOp::set, Decibels{-9.0})},
                                          {gap_edit(EditOp::add, Decibels{1.0})}});
    CHECK(effective_gap(gap_id("g"), later_set).gain == Decibels{-8.0});
}

TEST_CASE("the_fold_is_clamped_once_after_all_edits", "[effective_gap]") {
    // set mute, then add 6 dB: -114 dB, not muted (the clamp is on the final value only).
    const EditList lifted =
        list_with({{gap_edit(EditOp::set, k_gap_mute_db)}, {gap_edit(EditOp::add, Decibels{6.0})}});
    const EffectiveGap eff = effective_gap(gap_id("g"), lifted);
    CHECK(eff.gain == Decibels{-114.0});
    CHECK(eff.gain > k_gap_mute_db);
    // Pushed past either end the result stops at the limit.
    const EditList too_quiet = list_with(
        {{gap_edit(EditOp::set, Decibels{-100.0})}, {gap_edit(EditOp::add, Decibels{-100.0})}});
    CHECK(effective_gap(gap_id("g"), too_quiet).gain == k_gap_mute_db);
    const EditList too_loud = list_with(
        {{gap_edit(EditOp::set, Decibels{24.0})}, {gap_edit(EditOp::add, Decibels{10.0})}});
    CHECK(effective_gap(gap_id("g"), too_loud).gain == Decibels{24.0});
    // 0 + 200 - 100 = 100 dB: clamped once at the end to 24 (a clamp after each edit would give
    // 24 - 100 = -76 dB).
    const EditList overshoot = list_with({{gap_edit(EditOp::set, Decibels{0.0})},
                                          {gap_edit(EditOp::add, Decibels{200.0})},
                                          {gap_edit(EditOp::add, Decibels{-100.0})}});
    CHECK(effective_gap(gap_id("g"), overshoot).gain == Decibels{24.0});
}

TEST_CASE("reset_returns_the_gain_to_neutral", "[effective_gap]") {
    for (const auto& reset : {gap_reset(EditParam::gap_gain), gap_reset(std::nullopt)}) {
        const EditList list = list_with({{gap_edit(EditOp::set, Decibels{-6.0})}, {reset}});
        const EffectiveGap eff = effective_gap(gap_id("g"), list);
        CHECK(eff.gain == Decibels{0.0});
        CHECK_FALSE(eff.is_edited);
    }
}

TEST_CASE("float_dust_around_zero_db_is_not_an_edit", "[effective_gap]") {
    const EditList list = list_with({{gap_edit(EditOp::set, Decibels{k_neutral_snap_offset / 2})}});
    const EffectiveGap eff = effective_gap(gap_id("g"), list);
    CHECK(eff.gain == Decibels{0.0});
    CHECK_FALSE(eff.is_edited);
    const EditList real = list_with({{gap_edit(EditOp::set, Decibels{1e-6})}});
    CHECK(effective_gap(gap_id("g"), real).is_edited);
}

TEST_CASE("only_edits_that_target_the_gap_apply", "[effective_gap]") {
    const EditList list =
        list_with({{gap_edit(EditOp::set, Decibels{-6.0}, {gap_id("g1")})},
                   {gap_edit(EditOp::add, Decibels{-1.0}, {gap_id("g1"), gap_id("g2")})}});
    CHECK(effective_gap(gap_id("g1"), list).gain == Decibels{-7.0});
    CHECK(effective_gap(gap_id("g2"), list).gain == Decibels{-1.0});
    CHECK_FALSE(effective_gap(gap_id("g3"), list).is_edited);
}

TEST_CASE("a_note_edit_with_the_same_text_does_not_touch_the_gap", "[effective_gap]") {
    const EditList list =
        list_with({{pitch_add("g", 100.0)}, {gap_reset(std::nullopt, gap_id("other"))}});
    CHECK_FALSE(effective_gap(gap_id("g"), list).is_edited);
}

TEST_CASE("undone_and_orphaned_edits_are_ignored", "[effective_gap]") {
    EditList list = list_with({{gap_edit(EditOp::set, Decibels{-6.0})}});
    CHECK(effective_gap(gap_id("g"), list.undo().value()).gain == Decibels{0.0});
    CHECK(effective_gap(gap_id("g"), list.undo().value().redo().value()).gain == Decibels{-6.0});

    // Orphans only arise from re-analysis, so a recorded edit with one is built from its parts.
    EditSpec orphaned = gap_edit(EditOp::set, Decibels{-9.0}, {gap_id("live")});
    orphaned.target.orphaned_segment_ids = {gap_id("g")};
    const EditList with_orphan =
        EditList::from_parts(
            {.track = TrackId{"t1"},
             .edits = {Edit{
                 .seq = EditSeq{1}, .group = GroupId{1}, .created_at = k_t0, .spec = orphaned}},
             .cursor = 1,
             .discarded = {},
             .next_seq = EditSeq{2}})
            .value();
    CHECK_FALSE(effective_gap(gap_id("g"), with_orphan).is_edited);
    CHECK(effective_gap(gap_id("live"), with_orphan).gain == Decibels{-9.0});
}
