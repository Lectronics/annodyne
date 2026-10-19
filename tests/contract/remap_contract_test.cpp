// Remap contract (ROADMAP Phase 7 verify, stated now): orphaned edits are reported, never dropped.

#include "core/edits/remap.h"
#include "support/random_scenarios.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

/// A re-analysis of `old_notes`: each note is kept (moved a little), split, dropped, or two are
/// merged.
std::vector<Note> reanalyse(Rng& rng, const std::vector<Note>& old_notes) {
    std::vector<Note> out;
    int next = 0;
    auto fresh = [&](double a, double b) { return make_note("r" + std::to_string(next++), a, b); };
    for (std::size_t i = 0; i < old_notes.size(); ++i) {
        const double a = old_notes[i].source_start.unchecked_since_origin().value();
        const double b = old_notes[i].source_end.unchecked_since_origin().value();
        switch (uniform_int(rng, 0, 3)) {
        case 0: out.push_back(fresh(a + 0.001, b + 0.001)); break;
        case 1:
            out.push_back(fresh(a, (a + b) / 2));
            out.push_back(fresh((a + b) / 2, b));
            break;
        case 2: break;
        default:
            if (i + 1 < old_notes.size()) {
                out.push_back(
                    fresh(a, old_notes[i + 1].source_end.unchecked_since_origin().value()));
                ++i;
            }
            break;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("remap_never_drops_or_loses_a_target", "[remap_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto old_notes = random_notes(rng, 6);
        EditList edits{TrackId{"t1"}};
        for (int i = 0, n = uniform_int(rng, 1, 10); i < n; ++i) {
            edits = edits.add(random_group(rng, old_notes), k_t0).value();
        }
        AnalysisResult old_a;
        old_a.source_id = SourceId{"s"};
        old_a.notes = old_notes;
        AnalysisResult new_a;
        new_a.source_id = SourceId{"s"};
        new_a.version = 1;
        new_a.notes = reanalyse(rng, old_notes);
        std::ranges::sort(new_a.notes, {}, &Note::source_start);

        const auto result = remap_after_reanalysis(edits, old_a, new_a, MatchParams{}).value();
        REQUIRE(result.edits.active().size() == edits.active().size());
        CHECK(result.edits.cursor() == edits.cursor());
        for (std::size_t i = 0; i < edits.active().size(); ++i) {
            CHECK(result.edits.active()[i].seq == edits.active()[i].seq);
            CHECK(result.edits.active()[i].group == edits.active()[i].group);
        }
        for (const auto& e : result.report) {
            for (const NoteId& id : e.new_ids) {
                CHECK(find_note(new_a, id) != nullptr);  // every target exists in the new analysis
            }
        }
        REQUIRE(EditList::from_parts(result.edits.to_parts()).has_value());

        std::map<NoteId, std::vector<NoteId>> mapped;
        for (const auto& e : result.report) {
            mapped[e.old_id] = e.new_ids;
        }
        for (std::size_t i = 0; i < edits.active().size(); ++i) {
            const auto& before = edits.active()[i].spec.target;
            const auto& after = result.edits.active()[i].spec.target;
            for (const NoteId& id : before.note_ids) {
                const auto& to = mapped.at(id);
                if (to.empty()) {
                    CHECK(std::ranges::find(after.orphaned_note_ids, id) !=
                          after.orphaned_note_ids.end());
                } else {
                    for (const NoteId& n : to) {
                        CHECK(std::ranges::find(after.note_ids, n) != after.note_ids.end());
                    }
                }
            }
            // no id lives in both lists
            for (const NoteId& id : after.note_ids) {
                CHECK(std::ranges::find(after.orphaned_note_ids, id) ==
                      after.orphaned_note_ids.end());
            }
        }
    }
}
