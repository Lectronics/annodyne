#include "core/edits/edit_list.h"

#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

const WallTime k_t0{};

EditList empty_list() { return EditList{TrackId{"t1"}}; }

EditList added(const EditList& list, std::vector<EditSpec> group) {
    return list.add(group, k_t0).value();
}

}  // namespace

TEST_CASE("a_new_list_is_empty", "[edit_list]") {
    const EditList list = empty_list();
    CHECK(list.active().empty());
    CHECK_FALSE(list.can_undo());
    CHECK_FALSE(list.can_redo());
    CHECK(list.track_id() == TrackId{"t1"});
}

TEST_CASE("add_assigns_increasing_seq_and_one_group_per_call", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1), pitch_add("b", 2)});
    list = added(list, {pitch_add("a", 3)});
    const auto edits = list.active();
    REQUIRE(edits.size() == 3);
    CHECK(edits[0].seq == EditSeq{1});
    CHECK(edits[1].seq == EditSeq{2});
    CHECK(edits[2].seq == EditSeq{3});
    CHECK(edits[0].group == GroupId{1});
    CHECK(edits[1].group == GroupId{1});
    CHECK(edits[2].group == GroupId{3});
}

TEST_CASE("add_does_not_modify_the_original_list", "[edit_list]") {
    const EditList before = empty_list();
    const EditList after = added(before, {pitch_add("a", 1)});
    CHECK(before.active().empty());
    CHECK(after.active().size() == 1);
}

TEST_CASE("add_rejects_an_empty_group_and_invalid_specs", "[edit_list]") {
    const EditList list = empty_list();
    CHECK(list.add({}, k_t0).error().code == ErrorCode::invalid_argument);
    auto bad = pitch_add("a", 1);
    bad.target.note_ids.clear();
    const std::vector<EditSpec> group{pitch_add("a", 1), bad};
    CHECK_FALSE(list.add(group, k_t0).has_value());
}

TEST_CASE("undo_and_redo_move_over_whole_groups", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1), pitch_add("b", 1)});
    list = added(list, {pitch_add("a", 2)});
    list = list.undo().value();
    CHECK(list.active().size() == 2);
    CHECK(list.redo_tail().size() == 1);
    list = list.undo().value();
    CHECK(list.active().empty());  // the two-edit group went in one step
    CHECK_FALSE(list.can_undo());
    list = list.redo().value();
    CHECK(list.active().size() == 2);
    list = list.redo().value();
    CHECK(list.active().size() == 3);
    CHECK_FALSE(list.can_redo());
}

TEST_CASE("undo_and_redo_report_out_of_range_when_there_is_nothing_to_do", "[edit_list]") {
    const EditList list = empty_list();
    CHECK(list.undo().error().code == ErrorCode::out_of_range);
    CHECK(list.redo().error().code == ErrorCode::out_of_range);
}

TEST_CASE("add_after_undo_keeps_the_old_branch_as_history", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1)});    // seq 1
    list = added(list, {pitch_add("a", 2)});                     // seq 2
    list = list.undo().value();
    list = added(list, {pitch_add("a", 3)});                     // seq 3, seq 2 discarded
    REQUIRE(list.active().size() == 2);
    CHECK(list.active()[1].seq == EditSeq{3});
    REQUIRE(list.discarded().size() == 1);
    CHECK(list.discarded()[0].seq == EditSeq{2});
    CHECK_FALSE(list.can_redo());
}

TEST_CASE("seqs_are_never_reused_across_double_branching", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1)});                   // 1 (A)
    list = added(list, {pitch_add("a", 2)});                                    // 2 (B)
    list = added(list, {pitch_add("a", 3)});                                    // 3 (C)
    list = list.undo().value().undo().value();                                  // cursor at A
    list = added(list, {pitch_add("a", 4)});                                    // 4 (D)
    list = added(list, {pitch_add("a", 5), pitch_add("a", 6)});                 // 5,6 (E F)
    list = list.undo().value();                                                 // cursor at D
    list = added(list, {pitch_add("a", 7)});                                    // 7 (G)
    std::vector<std::uint64_t> active;
    for (const Edit& e : list.active()) active.push_back(e.seq.value);
    std::vector<std::uint64_t> dead;
    for (const Edit& e : list.discarded()) dead.push_back(e.seq.value);
    CHECK(active == std::vector<std::uint64_t>{1, 4, 7});
    CHECK(dead == std::vector<std::uint64_t>{2, 3, 5, 6});
    // The result must round-trip through from_parts (all invariants hold).
    CHECK(EditList::from_parts(list.to_parts()).has_value());
}

TEST_CASE("reset_records_one_edit_per_category_in_one_group", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1)});
    const std::vector<NoteId> notes{NoteId{"a"}, NoteId{"b"}};
    const std::vector<EditCategory> cats{EditCategory::pitch, EditCategory::amplitude};
    list = list.reset(notes, cats, k_t0).value();
    REQUIRE(list.active().size() == 3);  // history kept
    const Edit& r1 = list.active()[1];
    const Edit& r2 = list.active()[2];
    CHECK(r1.spec.op == EditOp::reset);
    CHECK(r1.spec.category == EditCategory::pitch);
    CHECK_FALSE(r1.spec.param.has_value());
    CHECK(r1.spec.origin == "reset");
    CHECK(r1.spec.target.note_ids == notes);
    CHECK(r2.spec.category == EditCategory::amplitude);
    CHECK(r1.group == r2.group);
    list = list.undo().value();
    CHECK(list.active().size() == 1);
}

TEST_CASE("reset_rejects_empty_lists", "[edit_list]") {
    const EditList list = empty_list();
    const std::vector<NoteId> notes{NoteId{"a"}};
    const std::vector<EditCategory> cats{EditCategory::pitch};
    CHECK_FALSE(list.reset({}, cats, k_t0).has_value());
    CHECK_FALSE(list.reset(notes, {}, k_t0).has_value());
}

TEST_CASE("retargeted_rewrites_ids_and_orphans_the_rest", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1), pitch_add("b", 2), pitch_add("c", 3)});
    NoteIdMap map;
    map[NoteId{"a"}] = {NoteId{"a1"}, NoteId{"a2"}};  // split
    map[NoteId{"b"}] = {};                            // no continuation
    // "c" absent
    const EditList out = list.retargeted(map);
    REQUIRE(out.active().size() == 3);
    CHECK(out.active()[0].spec.target.note_ids == std::vector<NoteId>{NoteId{"a1"}, NoteId{"a2"}});
    CHECK(out.active()[1].spec.target.note_ids.empty());
    CHECK(out.active()[1].spec.target.orphaned_note_ids == std::vector<NoteId>{NoteId{"b"}});
    CHECK(out.active()[2].spec.target.orphaned_note_ids == std::vector<NoteId>{NoteId{"c"}});
    CHECK(out.active()[0].seq == list.active()[0].seq);
    CHECK(out.cursor() == list.cursor());
}

TEST_CASE("retargeted_removes_duplicates_and_covers_every_partition", "[edit_list]") {
    EditList list = added(empty_list(), {pitch_add("a", 1)});
    list = added(list, {pitch_add("a", 2)});
    list = list.undo().value();                 // seq 2 in the redo tail
    list = added(list, {pitch_add("a", 3)});    // seq 2 -> discarded
    list = added(list, {pitch_add("b", 4)});
    list = list.undo().value();                 // seq 4 in the redo tail
    NoteIdMap map;
    map[NoteId{"a"}] = {NoteId{"x"}};
    map[NoteId{"b"}] = {NoteId{"x"}};            // merge: both point at x
    const EditList out = list.retargeted(map);
    CHECK(out.discarded()[0].spec.target.note_ids == std::vector<NoteId>{NoteId{"x"}});
    CHECK(out.redo_tail()[0].spec.target.note_ids == std::vector<NoteId>{NoteId{"x"}});
}

TEST_CASE("an_edit_targeting_two_notes_that_merge_lists_the_new_id_once", "[edit_list]") {
    auto spec = pitch_add("a", 1);
    spec.target.note_ids.push_back(NoteId{"b"});
    const EditList list = added(empty_list(), {spec});
    NoteIdMap map;
    map[NoteId{"a"}] = {NoteId{"m"}};
    map[NoteId{"b"}] = {NoteId{"m"}};
    CHECK(list.retargeted(map).active()[0].spec.target.note_ids == std::vector<NoteId>{NoteId{"m"}});
}

TEST_CASE("a_retargeted_list_with_orphan_only_edits_round_trips_through_parts", "[edit_list]") {
    const EditList list = added(empty_list(), {pitch_add("a", 1)});
    const EditList orphaned = list.retargeted(NoteIdMap{});
    REQUIRE(orphaned.active()[0].spec.target.note_ids.empty());
    const auto back = EditList::from_parts(orphaned.to_parts());
    REQUIRE(back.has_value());
    CHECK(back->active().size() == 1);
}

TEST_CASE("from_parts_rejects_broken_invariants", "[edit_list]") {
    const EditList list = added(added(empty_list(), {pitch_add("a", 1), pitch_add("a", 2)}),
                                {pitch_add("a", 3)});
    const EditListParts good = list.to_parts();
    REQUIRE(EditList::from_parts(good).has_value());

    auto parts = good;
    parts.edits[1].seq = EditSeq{1};  // not increasing
    CHECK(EditList::from_parts(parts).error().code == ErrorCode::corrupt_data);

    parts = good;
    parts.next_seq = EditSeq{3};  // not above every seq
    CHECK_FALSE(EditList::from_parts(parts).has_value());

    parts = good;
    parts.cursor = 1;  // inside the two-edit group
    CHECK_FALSE(EditList::from_parts(parts).has_value());

    parts = good;
    parts.cursor = 9;
    CHECK_FALSE(EditList::from_parts(parts).has_value());

    parts = good;
    parts.edits[1].group = GroupId{7};  // group id must equal the first seq of its run
    CHECK_FALSE(EditList::from_parts(parts).has_value());

    parts = good;
    parts.discarded.push_back(parts.edits[0]);  // seq shared with edits
    CHECK_FALSE(EditList::from_parts(parts).has_value());

    parts = good;
    parts.edits[0].spec.origin.clear();  // invalid spec
    CHECK_FALSE(EditList::from_parts(parts).has_value());
}
