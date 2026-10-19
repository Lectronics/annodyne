#include "core/edits/remap.h"
#include "support/test_data.h"

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
