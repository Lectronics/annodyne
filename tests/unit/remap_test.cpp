#include "core/edits/remap.h"
#include "support/test_data.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

const RemapEntry& entry(const NoteMatching& m, const char* id) {
    for (const auto& e : m.entries) {
        if (e.old_id == NoteId{id}) {
            return e;
        }
    }
    FAIL("no entry for " << id);
    return m.entries.front();
}

std::vector<NoteId> ids(std::initializer_list<const char*> list) {
    std::vector<NoteId> out;
    for (const char* s : list) {
        out.push_back(NoteId{s});
    }
    return out;
}

}  // namespace

TEST_CASE("identical_notes_are_kept", "[remap]") {
    const std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 3.0, 4.0)};
    const auto m = match_notes(notes, notes, MatchParams{});
    CHECK(entry(m, "a").outcome == RemapOutcome::kept);
    CHECK(entry(m, "a").new_ids == ids({"a"}));
}

TEST_CASE("a_slightly_moved_note_is_kept_under_its_new_id", "[remap]") {
    const std::vector<Note> old_notes{make_note("a", 1.0, 2.0)};
    const std::vector<Note> new_notes{make_note("n1", 1.05, 2.05)};
    const auto m = match_notes(old_notes, new_notes, MatchParams{});
    CHECK(entry(m, "a").outcome == RemapOutcome::kept);
    CHECK(entry(m, "a").new_ids == ids({"n1"}));
}

TEST_CASE("a_note_cut_in_two_is_a_split", "[remap]") {
    const std::vector<Note> old_notes{make_note("a", 1.0, 3.0)};
    const std::vector<Note> new_notes{make_note("n1", 1.0, 2.0), make_note("n2", 2.0, 3.0)};
    const auto m = match_notes(old_notes, new_notes, MatchParams{});
    CHECK(entry(m, "a").outcome == RemapOutcome::split);
    CHECK(entry(m, "a").new_ids == ids({"n1", "n2"}));
}

TEST_CASE("two_notes_joined_are_a_merge_and_the_longer_parent_keeps_the_edits", "[remap]") {
    const std::vector<Note> old_notes{make_note("short", 1.0, 1.5), make_note("long", 1.5, 3.0)};
    const std::vector<Note> new_notes{make_note("m", 1.0, 3.0)};
    const auto m = match_notes(old_notes, new_notes, MatchParams{});
    CHECK(entry(m, "long").outcome == RemapOutcome::merged_kept);
    CHECK(entry(m, "long").new_ids == ids({"m"}));
    CHECK(entry(m, "short").outcome == RemapOutcome::merged_away);
    CHECK(entry(m, "short").new_ids.empty());
}

TEST_CASE("merge_ties_go_to_the_earlier_note", "[remap]") {
    const std::vector<Note> old_notes{make_note("first", 1.0, 2.0), make_note("second", 2.0, 3.0)};
    const std::vector<Note> new_notes{make_note("m", 1.0, 3.0)};
    const auto m = match_notes(old_notes, new_notes, MatchParams{});
    CHECK(entry(m, "first").outcome == RemapOutcome::merged_kept);
    CHECK(entry(m, "second").outcome == RemapOutcome::merged_away);
}

TEST_CASE("a_note_with_nothing_nearby_is_orphaned", "[remap]") {
    const std::vector<Note> old_notes{make_note("a", 1.0, 2.0)};
    const std::vector<Note> new_notes{make_note("n", 5.0, 6.0)};
    const auto m = match_notes(old_notes, new_notes, MatchParams{});
    CHECK(entry(m, "a").outcome == RemapOutcome::orphaned);
    CHECK(m.id_map().at(NoteId{"a"}).empty());
}

TEST_CASE("inactive_notes_take_no_part", "[remap]") {
    auto old_notes = std::vector<Note>{make_note("a", 1.0, 2.0)};
    auto new_notes = std::vector<Note>{make_note("n", 1.0, 2.0)};
    new_notes[0].is_active = false;
    CHECK(entry(match_notes(old_notes, new_notes, MatchParams{}), "a").outcome ==
          RemapOutcome::orphaned);
    old_notes[0].is_active = false;
    CHECK(match_notes(old_notes, new_notes, MatchParams{}).entries.empty());
}

TEST_CASE("thresholds_decide_between_keep_and_orphan", "[remap]") {
    const std::vector<Note> old_notes{make_note("a", 1.0, 2.0)};
    const std::vector<Note> new_notes{make_note("n", 1.6, 2.6)};  // 40% overlap
    CHECK(entry(match_notes(old_notes, new_notes, MatchParams{}), "a").outcome ==
          RemapOutcome::orphaned);
    MatchParams loose;
    loose.keep_overlap = Ratio{0.3};
    CHECK(entry(match_notes(old_notes, new_notes, loose), "a").outcome == RemapOutcome::kept);
}

namespace {

AnalysisResult analysis_of(std::vector<Note> notes, const char* source = "src1") {
    AnalysisResult a;
    a.source_id = SourceId{source};
    a.notes = std::move(notes);
    return a;
}

}  // namespace

TEST_CASE("remap_moves_edits_to_the_matching_notes_and_reports_each_old_note", "[remap]") {
    EditList edits{TrackId{"t1"}};
    edits = edits.add(std::vector{pitch_add("a", 10), pitch_add("b", 20), pitch_add("c", 30)}, k_t0)
                .value();
    const auto old_a =
        analysis_of({make_note("a", 1, 2), make_note("b", 3, 5), make_note("c", 7, 8)});
    const auto new_a =
        analysis_of({make_note("a", 1, 2), make_note("b1", 3, 4), make_note("b2", 4, 5)});
    const auto result = remap_after_reanalysis(edits, old_a, new_a, MatchParams{}).value();

    const auto& targets = result.edits.active();
    CHECK(targets[0].spec.target.segment_ids == segments_of(ids({"a"})));
    CHECK(targets[1].spec.target.segment_ids == segments_of(ids({"b1", "b2"})));
    CHECK(targets[2].spec.target.segment_ids.empty());
    CHECK(targets[2].spec.target.orphaned_segment_ids == segments_of(ids({"c"})));
    REQUIRE(result.report.size() == 3);
    CHECK(result.report[0].old_id == NoteId{"a"});
    CHECK(result.report[1].outcome == RemapOutcome::split);
    CHECK(result.report[2].outcome == RemapOutcome::orphaned);
}

TEST_CASE("remap_never_drops_an_edit", "[remap]") {
    EditList edits{TrackId{"t1"}};
    edits = edits.add(std::vector{pitch_add("a", 10), pitch_add("ghost", 5)}, k_t0).value();
    const auto old_a = analysis_of({make_note("a", 1, 2)});
    const auto new_a = analysis_of({make_note("z", 9, 10)});
    const auto result = remap_after_reanalysis(edits, old_a, new_a, MatchParams{}).value();
    CHECK(result.edits.active().size() == edits.active().size());
    CHECK(result.edits.active()[1].spec.target.orphaned_segment_ids == segments_of(ids({"ghost"})));
    // an id unknown to the old analysis is reported as orphaned
    bool found = false;
    for (const auto& e : result.report) {
        if (e.old_id == NoteId{"ghost"}) {
            found = true;
            CHECK(e.outcome == RemapOutcome::orphaned);
        }
    }
    CHECK(found);
}

TEST_CASE("remap_rejects_analyses_of_different_sources", "[remap]") {
    const EditList edits{TrackId{"t1"}};
    const auto a = analysis_of({}, "one");
    const auto b = analysis_of({}, "two");
    CHECK(remap_after_reanalysis(edits, a, b, MatchParams{}).error().code ==
          ErrorCode::invalid_argument);
}

TEST_CASE("remapped_effective_state_follows_the_new_note", "[remap]") {
    EditList edits{TrackId{"t1"}};
    edits = edits.add(std::vector{pitch_add("a", 25)}, k_t0).value();
    const auto old_a = analysis_of({make_note("a", 1, 2)});
    const auto new_a = analysis_of({make_note("n", 1.02, 2.02)});
    const auto result = remap_after_reanalysis(edits, old_a, new_a, MatchParams{}).value();
    CHECK(result.edits.active()[0].spec.target.segment_ids == segments_of(ids({"n"})));
}

namespace {

SegmentId gap_for(const char* before, const char* after) {
    return gap_segment_id(before != nullptr ? std::optional<NoteId>{NoteId{before}} : std::nullopt,
                          after != nullptr ? std::optional<NoteId>{NoteId{after}} : std::nullopt);
}

/// An analysis with its timeline: gaps between the notes and at both ends of a 10 s recording.
AnalysisResult analysis_with_timeline(std::vector<Note> notes, const char* source = "src1") {
    AnalysisResult a = analysis_of(std::move(notes), source);
    a.timeline = timeline_of(a.notes, 10.0);
    return a;
}

EditSpec gap_gain_on(const SegmentId& gap, double db) {
    EditSpec spec =
        make_spec("unused", EditCategory::gap, EditParam::gap_gain, EditOp::set, Decibels{db});
    spec.target.segment_ids = {gap};
    return spec;
}

}  // namespace

TEST_CASE("gap_edits_survive_when_only_the_note_ids_change", "[remap]") {
    const auto old_a = analysis_with_timeline({make_note("a", 1, 2), make_note("b", 3, 4)});
    const auto new_a =
        analysis_with_timeline({make_note("n1", 1.0, 2.0), make_note("n2", 3.0, 4.0)});
    const GapIdMap map = gap_id_map(*old_a.timeline, *new_a.timeline,
                                    match_notes(old_a.notes, new_a.notes, MatchParams{}).id_map());
    CHECK(map.at(gap_for(nullptr, "a")) == gap_for(nullptr, "n1"));
    CHECK(map.at(gap_for("a", "b")) == gap_for("n1", "n2"));
    CHECK(map.at(gap_for("b", nullptr)) == gap_for("n2", nullptr));

    EditList edits{TrackId{"t1"}};
    edits = edits.add(std::vector{gap_gain_on(gap_for("a", "b"), -12.0)}, k_t0).value();
    const auto result = remap_after_reanalysis(edits, old_a, new_a, MatchParams{}).value();
    CHECK(result.edits.active()[0].spec.target.segment_ids ==
          std::vector<SegmentId>{gap_for("n1", "n2")});
    REQUIRE(result.gap_report.size() == 1);
    CHECK(result.gap_report[0].old_gap == gap_for("a", "b"));
    CHECK(result.gap_report[0].new_gap == gap_for("n1", "n2"));
}

TEST_CASE("a_split_note_gives_its_last_child_to_the_gap_after_and_its_first_to_the_gap_before",
          "[remap]") {
    const auto old_a = analysis_with_timeline({make_note("a", 1, 3), make_note("b", 4, 5)});
    const auto new_a = analysis_with_timeline(
        {make_note("a1", 1, 2), make_note("a2", 2, 3), make_note("b", 4, 5)});
    const GapIdMap map = gap_id_map(*old_a.timeline, *new_a.timeline,
                                    match_notes(old_a.notes, new_a.notes, MatchParams{}).id_map());
    CHECK(map.at(gap_for(nullptr, "a")) == gap_for(nullptr, "a1"));
    CHECK(map.at(gap_for("a", "b")) == gap_for("a2", "b"));
}

TEST_CASE("a_gap_between_notes_that_were_merged_is_orphaned", "[remap]") {
    // a is the shorter parent: merged_away (no image); b is longer and keeps the merged note m.
    const auto old_a = analysis_with_timeline({make_note("a", 1, 1.5), make_note("b", 2.2, 3)});
    const auto new_a = analysis_with_timeline({make_note("m", 1, 3)});
    const GapIdMap map = gap_id_map(*old_a.timeline, *new_a.timeline,
                                    match_notes(old_a.notes, new_a.notes, MatchParams{}).id_map());
    CHECK_FALSE(map.contains(gap_for("a", "b")));      // no gap inside the merged note
    CHECK_FALSE(map.contains(gap_for(nullptr, "a")));  // a has no continuation
    CHECK(map.at(gap_for("b", nullptr)) == gap_for("m", nullptr));

    EditList edits{TrackId{"t1"}};
    edits = edits.add(std::vector{gap_gain_on(gap_for("a", "b"), -12.0)}, k_t0).value();
    const auto result = remap_after_reanalysis(edits, old_a, new_a, MatchParams{}).value();
    CHECK(result.edits.active()[0].spec.target.segment_ids.empty());
    CHECK(result.edits.active()[0].spec.target.orphaned_segment_ids ==
          std::vector<SegmentId>{gap_for("a", "b")});
    REQUIRE(result.gap_report.size() == 1);
    CHECK_FALSE(result.gap_report[0].new_gap.has_value());
}

TEST_CASE("a_gap_whose_neighbour_vanished_or_whose_notes_are_no_longer_adjacent_is_orphaned",
          "[remap]") {
    const auto old_a = analysis_with_timeline({make_note("a", 1, 2), make_note("b", 3, 4)});
    // b is gone; a new note c appears elsewhere
    const auto gone = analysis_with_timeline({make_note("a", 1, 2), make_note("c", 8, 9)});
    const GapIdMap map1 = gap_id_map(*old_a.timeline, *gone.timeline,
                                     match_notes(old_a.notes, gone.notes, MatchParams{}).id_map());
    CHECK_FALSE(map1.contains(gap_for("a", "b")));
    CHECK_FALSE(map1.contains(gap_for("b", nullptr)));
    // a new note d lies between a and b: the old gap's images are no longer neighbours
    const auto between = analysis_with_timeline(
        {make_note("a", 1, 2), make_note("d", 2.4, 2.6), make_note("b", 3, 4)});
    const GapIdMap map2 =
        gap_id_map(*old_a.timeline, *between.timeline,
                   match_notes(old_a.notes, between.notes, MatchParams{}).id_map());
    CHECK_FALSE(map2.contains(gap_for("a", "b")));
    CHECK(map2.contains(gap_for(nullptr, "a")));
}

TEST_CASE("ids_that_would_collide_without_the_length_prefix_stay_apart", "[remap]") {
    // Without the length prefix gap(a, 1b) and gap(a1, b) would both read "a1b".
    const auto old_a = analysis_with_timeline({make_note("a", 1, 2), make_note("1b", 3, 4)});
    const auto new_a = analysis_with_timeline({make_note("a", 1, 2), make_note("1b", 3, 4)});
    const GapIdMap map = gap_id_map(*old_a.timeline, *new_a.timeline,
                                    match_notes(old_a.notes, new_a.notes, MatchParams{}).id_map());
    CHECK(map.at(gap_for("a", "1b")) == gap_for("a", "1b"));
    CHECK(gap_for("a", "1b") != gap_for("a1", "b"));
    CHECK(gap_for("a", "1b").value != gap_for("a1", "b").value);
}

TEST_CASE("an_absent_timeline_orphans_every_gap_edit_but_note_edits_follow", "[remap]") {
    EditList edits{TrackId{"t1"}};
    edits = edits.add(std::vector{gap_gain_on(gap_for("a", "b"), -12.0), pitch_add("a", 10)}, k_t0)
                .value();
    const auto with = analysis_with_timeline({make_note("a", 1, 2), make_note("b", 3, 4)});
    const auto without = analysis_of({make_note("a", 1, 2), make_note("b", 3, 4)});
    for (const auto& pair :
         {std::pair{&with, &without}, std::pair{&without, &with}, std::pair{&without, &without}}) {
        const auto result =
            remap_after_reanalysis(edits, *pair.first, *pair.second, MatchParams{}).value();
        CHECK(result.edits.active()[0].spec.target.segment_ids.empty());
        CHECK(result.edits.active()[0].spec.target.orphaned_segment_ids ==
              std::vector<SegmentId>{gap_for("a", "b")});
        CHECK(result.edits.active()[1].spec.target.segment_ids ==
              std::vector<SegmentId>{segment_id(NoteId{"a"})});
        REQUIRE(result.gap_report.size() == 1);
        CHECK_FALSE(result.gap_report[0].new_gap.has_value());
    }
}
