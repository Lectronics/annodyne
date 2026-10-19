// dirty_regions contract (ROADMAP Phase 1): covers every note whose effective state changed, and
// nothing else beyond the crossfade margins (timing changes excepted: they dirty the tail).

#include "core/edits/dirty_regions.h"
#include "core/edits/effective_state.h"
#include "core/timemap/build_time_map.h"
#include "support/random_scenarios.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

double raw(SourceTime t) {
    return t.unchecked_since_origin().value();
}

bool inside(const std::vector<SourceRange>& regions, double s) {
    return std::ranges::any_of(
        regions, [&](const SourceRange& r) { return raw(r.start()) <= s && s <= raw(r.end()); });
}

EditList random_list(Rng& rng, const std::vector<Note>& notes, bool allow_timing) {
    EditList list{TrackId{"t1"}};
    for (int i = 0, n = uniform_int(rng, 0, 10); i < n; ++i) {
        list = list.add(random_group(rng, notes, allow_timing), k_t0).value();
    }
    return list;
}

}  // namespace

TEST_CASE("every_changed_note_is_covered", "[dirty_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 6);
        const EditList old_list = random_list(rng, notes, true);
        EditList new_list = old_list;
        switch (uniform_int(rng, 0, 2)) {
        case 0: new_list = old_list.add(random_group(rng, notes), k_t0).value(); break;
        case 1:
            if (old_list.can_undo()) {
                new_list = old_list.undo().value();
            }
            break;
        default: break;
        }
        const DirtyParams params{.crossfade_margin = Seconds{0.05},
                                 .source_end = src(raw(notes.back().source_end) + 1.0)};
        const auto regions = dirty_regions(notes, old_list, new_list, params).value();
        for (const Note& n : notes) {
            if (effective_state(n, old_list) != effective_state(n, new_list)) {
                CHECK(inside(regions, raw(n.source_start)));
                CHECK(inside(regions, raw(n.source_end)));
                CHECK(inside(regions, (raw(n.source_start) + raw(n.source_end)) / 2));
            }
        }
        // sorted, disjoint
        for (std::size_t i = 1; i < regions.size(); ++i) {
            CHECK(raw(regions[i].start()) > raw(regions[i - 1].end()));
        }
    }
}

TEST_CASE("without_timing_changes_nothing_else_is_dirty_beyond_the_margin", "[dirty_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 6);
        const EditList old_list = random_list(rng, notes, false);
        const EditList new_list = old_list.add(random_group(rng, notes, false), k_t0).value();
        const DirtyParams params{.crossfade_margin = Seconds{0.05},
                                 .source_end = src(raw(notes.back().source_end) + 1.0)};
        const auto regions = dirty_regions(notes, old_list, new_list, params).value();
        for (const SourceRange& r : regions) {
            // Each end of a region must be at a changed note's expanded edge.
            bool start_ok = false;
            bool end_ok = false;
            for (const Note& n : notes) {
                if (effective_state(n, old_list) == effective_state(n, new_list)) {
                    continue;
                }
                start_ok = start_ok || std::max(raw(n.source_start) - 0.05, 0.0) == raw(r.start());
                end_ok = end_ok || raw(n.source_end) + 0.05 == raw(r.end());
            }
            CHECK(start_ok);
            CHECK(end_ok);
        }
        // Points far from every changed note are clean.
        for (int i = 0; i < 40; ++i) {
            const double s = uniform(rng, 0.0, raw(notes.back().source_end) + 1.0);
            bool near_changed = false;
            for (const Note& n : notes) {
                if (effective_state(n, old_list) != effective_state(n, new_list)) {
                    near_changed = near_changed || (s >= raw(n.source_start) - 0.05 - 1e-9 &&
                                                    s <= raw(n.source_end) + 0.05 + 1e-9);
                }
            }
            if (!near_changed) {
                CHECK_FALSE(inside(regions, s));
            }
        }
    }
}

TEST_CASE("every_source_instant_whose_output_position_changes_is_dirty", "[dirty_contract]") {
    // The renderer trusts dirty_regions; a source instant that the TimeMap moves must be inside it
    // (audit #1 B1: an abutting neighbour's last segment is stretched).
    for (int seed = 0; seed < 600; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 6);
        const EditList old_list = random_list(rng, notes, true);
        const EditList new_list = old_list.add(random_group(rng, notes, true), k_t0).value();
        const double end = raw(notes.back().source_end) + 1.0;
        const DirtyParams params{.crossfade_margin = Seconds{0.05}, .source_end = src(end)};
        const auto regions = dirty_regions(notes, old_list, new_list, params).value();
        const auto old_map = build_time_map(notes, old_list, TimeMapParams{}).value().map;
        const auto new_map = build_time_map(notes, new_list, TimeMapParams{}).value().map;
        for (int i = 0; i <= 2000; ++i) {
            const double s = end * i / 2000.0;
            const double moved =
                std::abs(old_map.to_output(src(s)).unchecked_since_origin().value() -
                         new_map.to_output(src(s)).unchecked_since_origin().value());
            if (moved > 1e-9) {
                CHECK(inside(regions, s));
            }
        }
    }
}
