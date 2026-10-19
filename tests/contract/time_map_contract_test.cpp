// TimeMap contracts (ROADMAP Phase 1): strictly monotonic, M^-1(M(s)) == s within 1e-9 s, identity
// when there are no time edits.

#include "core/timemap/build_time_map.h"
#include "support/random_scenarios.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

double raw(SourceTime t) {
    return t.unchecked_since_origin().value();
}
double raw(OutputTime t) {
    return t.unchecked_since_origin().value();
}

EditList random_edits(Rng& rng, const std::vector<Note>& notes, bool timing_only) {
    EditList list{TrackId{"t1"}};
    for (int i = 0, n = uniform_int(rng, 1, 20); i < n; ++i) {
        EditSpec spec;
        do {
            spec = random_spec(rng, notes);
        } while (timing_only && !is_timing(*spec.param));
        list = list.add(std::vector{spec}, k_t0).value();
    }
    return list;
}

}  // namespace

TEST_CASE("the_time_map_is_strictly_monotonic_for_random_timing_edits", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, uniform_int(rng, 1, 8));
        const auto built = build_time_map(notes, random_edits(rng, notes, true), TimeMapParams{});
        REQUIRE(built.has_value());  // never an error for valid input, whatever the edits
        const auto anchors = built->map.anchors();
        for (std::size_t i = 1; i < anchors.size(); ++i) {
            CHECK(raw(anchors[i].source) > raw(anchors[i - 1].source));
            CHECK(raw(anchors[i].output) > raw(anchors[i - 1].output));
        }
        // and so is the function, sampled
        double previous = -1e300;
        const double last_s = raw(notes.back().source_end) + 2.0;
        for (int i = 0; i < 400; ++i) {
            const double s = -1.0 + (last_s + 1.0) * i / 400.0;
            const double t = raw(built->map.to_output(src(s)));
            REQUIRE(t > previous);
            previous = t;
        }
    }
}

TEST_CASE("the_inverse_undoes_the_map_within_1e9_seconds", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, uniform_int(rng, 1, 8));
        const auto built = build_time_map(notes, random_edits(rng, notes, true), TimeMapParams{});
        REQUIRE(built.has_value());
        for (int i = 0; i < 50; ++i) {
            const double s = uniform(rng, -1.0, raw(notes.back().source_end) + 3.0);
            const double back = raw(built->map.to_source(built->map.to_output(src(s))));
            CHECK(std::abs(back - s) <= tolerance::k_timemap_roundtrip.value());
        }
    }
}

TEST_CASE("without_time_edits_the_map_is_the_exact_identity", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, uniform_int(rng, 1, 8));
        EditList list{TrackId{"t1"}};
        for (int i = 0, n = uniform_int(rng, 0, 10); i < n; ++i) {
            EditSpec spec;
            do {
                spec = random_spec(rng, notes);
            } while (is_timing(*spec.param));  // pitch, level, formant, ... but no time edits
            list = list.add(std::vector{spec}, k_t0).value();
        }
        const auto built = build_time_map(notes, list, TimeMapParams{});
        REQUIRE(built.has_value());
        CHECK(built->map.is_identity());
        CHECK(built->repaired_notes == 0);
        for (int i = 0; i < 20; ++i) {
            const double s = uniform(rng, 0.0, 10.0);
            CHECK(raw(built->map.to_output(src(s))) == s);
        }
    }
}

TEST_CASE("timing_that_is_undone_gives_the_identity_again", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, uniform_int(rng, 1, 6));
        EditList list = random_edits(rng, notes, true);
        while (list.can_undo()) {
            list = list.undo().value();
        }
        CHECK(build_time_map(notes, list, TimeMapParams{})->map.is_identity());
    }
}

TEST_CASE("an_unedited_note_keeps_its_length_unless_pushed", "[time_map_contract]") {
    // Edit only the last note: everything before it must be untouched (consonant protection and
    // "nothing else moves" for earlier audio).
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 4);
        const std::string last = notes.back().id.value;
        EditList list{TrackId{"t1"}};
        list = list.add(std::vector{make_spec(last, EditCategory::time, EditParam::onset_shift,
                                              EditOp::add, Seconds{uniform(rng, 0.0, 1.0)})},
                        k_t0)
                   .value();
        const auto built = build_time_map(notes, list, TimeMapParams{});
        REQUIRE(built.has_value());
        const double last_start = raw(notes.back().source_start);
        for (const Note& n : notes) {
            if (raw(n.source_end) >= last_start) {
                continue;  // an abutting boundary is shared
            }
            CHECK(raw(built->map.to_output(n.source_start)) == raw(n.source_start));
            CHECK(raw(built->map.to_output(n.source_end)) == raw(n.source_end));
        }
    }
}

// The properties above also hold for a builder that ignores every edit, so these pin the effect.
TEST_CASE("a_single_note_edit_moves_the_anchors_by_exactly_the_requested_amounts",
          "[time_map_contract]") {
    constexpr double k_eps = 1e-9;
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const double start = uniform(rng, 1.0, 3.0);
        const double length = uniform(rng, 0.2, 1.5);
        const std::vector<Note> notes{make_note("n0", start, start + length)};
        const double attack_end = raw(notes[0].attack_end);
        const double end = start + length;
        const auto build_with = [&](std::vector<EditSpec> group) {
            return build_time_map(notes, EditList{TrackId{"t1"}}.add(group, k_t0).value(),
                                  TimeMapParams{})
                .value();
        };

        const double shift = uniform(rng, -0.5, 1.5);
        const auto shifted = build_with({make_spec("n0", EditCategory::time, EditParam::onset_shift,
                                                   EditOp::set, Seconds{shift})});
        CHECK(std::abs(raw(shifted.map.to_output(src(start))) - (start + shift)) < k_eps);
        CHECK(std::abs(raw(shifted.map.to_output(src(end))) - (end + shift)) < k_eps);
        CHECK(shifted.repaired_notes == 0);

        const double scale = uniform(rng, 0.5, 3.0);
        const auto scaled = build_with({make_spec("n0", EditCategory::time, EditParam::length_scale,
                                                  EditOp::set, Ratio{scale})});
        CHECK(std::abs(raw(scaled.map.to_output(src(attack_end))) - attack_end) < k_eps);
        CHECK(std::abs(raw(scaled.map.to_output(src(end))) -
                       (attack_end + (end - attack_end) * scale)) < k_eps);

        const double attack_scale = uniform(rng, 0.5, 3.0);
        const auto attack =
            build_with({make_spec("n0", EditCategory::attack, EditParam::attack_time_scale,
                                  EditOp::set, Ratio{attack_scale})});
        CHECK(std::abs(raw(attack.map.to_output(src(attack_end))) -
                       (start + (attack_end - start) * attack_scale)) < k_eps);

        const double h_s = uniform(rng, attack_end + 0.02, end - 0.02);
        const double h_t = h_s + uniform(rng, -0.01, 0.01);
        const auto handled =
            build_with({make_spec("n0", EditCategory::time, EditParam::time_handle, EditOp::set,
                                  TimePair{.source = src(h_s), .output = out(h_t)})});
        CHECK(std::abs(raw(handled.map.to_output(src(h_s))) - h_t) < k_eps);
        CHECK(std::abs(raw(handled.map.to_output(src(end))) - end) < k_eps);
    }
}
