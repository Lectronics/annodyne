// TimeMap contracts (ROADMAP Phase 1): strictly monotonic, M^-1(M(s)) == s within 1e-9 s, identity
// when there are no time edits.

#include "core/timemap/build_time_map.h"
#include "support/random_scenarios.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <variant>

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

double t_of(const TimeMap& map, SourceTime s) {
    return raw(*map.to_output(s));
}

/// The recording's end for a random note set: a little after the last note.
TimeMapParams params_for(const std::vector<Note>& notes) {
    return TimeMapParams{.source_end = src(raw(notes.back().source_end) + 1.0)};
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
        const auto built = build_time_map(notes, random_edits(rng, notes, true), params_for(notes));
        INFO((built ? "" : built.error().message));
        REQUIRE(built.has_value());  // never an error for valid input, whatever the edits
        const auto spans = built->map.spans();
        for (std::size_t i = 1; i < spans.size(); ++i) {
            CHECK(raw(spans[i].source_start) == raw(spans[i - 1].source_end));  // tiling
            CHECK(raw(spans[i].output_start) == raw(spans[i - 1].output_end));
            CHECK(raw(spans[i].source_end) > raw(spans[i].source_start));  // warps only
            CHECK(raw(spans[i].output_end) > raw(spans[i].output_start));
        }
        // and so is the function, sampled over the whole domain
        double previous = -1e300;
        const double length = built->map.source_length().value();
        for (int i = 0; i <= 400; ++i) {
            const double t = t_of(built->map, src(i >= 400 ? length : length * i / 400.0));
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
        const auto built = build_time_map(notes, random_edits(rng, notes, true), params_for(notes));
        REQUIRE(built.has_value());
        for (int i = 0; i < 50; ++i) {
            const double s = uniform(rng, 0.0, built->map.source_length().value() * 0.999);
            const double back =
                raw(std::get<SourceTime>(built->map.to_source(*built->map.to_output(src(s)))));
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
        const auto built = build_time_map(notes, list, params_for(notes));
        REQUIRE(built.has_value());
        CHECK(built->map.is_identity());
        CHECK(built->repaired_notes == 0);
        for (int i = 0; i < 20; ++i) {
            const double s = uniform(rng, 0.0, built->map.source_length().value());
            CHECK(t_of(built->map, src(s)) == s);
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
        CHECK(build_time_map(notes, list, params_for(notes))->map.is_identity());
    }
}

TEST_CASE("unedited_audio_before_an_edit_is_stretched_uniformly_back_to_the_origin",
          "[time_map_contract]") {
    // Flex-style handles: a note shifted by d moves its start from s to s + d, the origin and the
    // end of the file stay fixed, and the unedited notes before it are scaled by (s + d) / s.
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto notes = random_notes(rng, 4);
        const std::string last = notes.back().id.value;
        const double d = uniform(rng, 0.0, 1.0);
        EditList list{TrackId{"t1"}};
        list = list.add(std::vector{make_spec(last, EditCategory::time, EditParam::onset_shift,
                                              EditOp::add, Seconds{d})},
                        k_t0)
                   .value();
        const double file_end = raw(notes.back().source_end) + 2.0;
        const auto built = build_time_map(notes, list, TimeMapParams{.source_end = src(file_end)});
        REQUIRE(built.has_value());
        CHECK(built->repaired_notes == 0);
        CHECK(t_of(built->map, src(0.0)) == 0.0);
        CHECK(t_of(built->map, src(file_end)) == file_end);
        const double last_start = raw(notes.back().source_start);
        const double ratio = (last_start + d) / last_start;
        for (const Note& n : notes) {
            if (raw(n.source_end) > last_start) {
                continue;
            }
            CHECK(std::abs(t_of(built->map, n.source_start) - raw(n.source_start) * ratio) < 1e-9);
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
        CHECK(std::abs(t_of(shifted.map, src(start)) - (start + shift)) < k_eps);
        CHECK(std::abs(t_of(shifted.map, src(end)) - (end + shift)) < k_eps);
        CHECK(shifted.repaired_notes == 0);

        const double scale = uniform(rng, 0.5, 3.0);
        const auto scaled = build_with({make_spec("n0", EditCategory::time, EditParam::length_scale,
                                                  EditOp::set, Ratio{scale})});
        CHECK(std::abs(t_of(scaled.map, src(attack_end)) - attack_end) < k_eps);
        CHECK(std::abs(t_of(scaled.map, src(end)) - (attack_end + (end - attack_end) * scale)) <
              k_eps);

        const double attack_scale = uniform(rng, 0.5, 3.0);
        const auto attack =
            build_with({make_spec("n0", EditCategory::attack, EditParam::attack_time_scale,
                                  EditOp::set, Ratio{attack_scale})});
        CHECK(std::abs(t_of(attack.map, src(attack_end)) -
                       (start + (attack_end - start) * attack_scale)) < k_eps);

        const double h_s = uniform(rng, attack_end + 0.02, end - 0.02);
        const double h_t = h_s + uniform(rng, -0.01, 0.01);
        const auto handled =
            build_with({make_spec("n0", EditCategory::time, EditParam::time_handle, EditOp::set,
                                  TimePair{.source = src(h_s), .output = out(h_t)})});
        CHECK(std::abs(t_of(handled.map, src(h_s)) - h_t) < k_eps);
        CHECK(std::abs(t_of(handled.map, src(end)) - end) < k_eps);
    }
}

// ---- span invariants (ADR-0008) ----

namespace {

/// The i-th of 200 evenly spaced instants of the source domain (the last is exactly its end).
double sample_of(const TimeMap& map, int i) {
    return i >= 200 ? map.source_length().value() : map.source_length().value() * i / 200.0;
}

/// A random valid span list: warps (slope 0.5..2), cuts and inserts, possibly leading/trailing
/// ones.
std::vector<TimeSpan> random_spans(Rng& rng) {
    std::vector<TimeSpan> spans;
    double s = 0.0;
    double t = 0.0;
    for (int i = 0, n = uniform_int(rng, 1, 12); i < n; ++i) {
        const double len = uniform(rng, 0.01, 1.0);
        TimeSpan sp;
        switch (uniform_int(rng, 0, 2)) {
        case 0: {
            const double out_len = len * uniform(rng, 0.5, 2.0);
            sp = {.kind = SpanKind::warp,
                  .source_start = src(s),
                  .source_end = src(s + len),
                  .output_start = out(t),
                  .output_end = out(t + out_len),
                  .segment = std::nullopt};
            s += len;
            t += out_len;
            break;
        }
        case 1:
            sp = {.kind = SpanKind::cut,
                  .source_start = src(s),
                  .source_end = src(s + len),
                  .output_start = out(t),
                  .output_end = out(t),
                  .segment = std::nullopt};
            s += len;
            break;
        default:
            sp = {.kind = SpanKind::insert,
                  .source_start = src(s),
                  .source_end = src(s),
                  .output_start = out(t),
                  .output_end = out(t + len),
                  .segment = std::nullopt};
            t += len;
            break;
        }
        spans.push_back(sp);
    }
    return spans;
}

}  // namespace

TEST_CASE("random_span_lists_tile_both_axes_and_are_monotonic", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto map = TimeMap::make(random_spans(rng));
        REQUIRE(map.has_value());
        const auto spans = map->spans();
        CHECK(raw(spans.front().source_start) == 0.0);
        CHECK(raw(spans.front().output_start) == 0.0);
        for (std::size_t i = 1; i < spans.size(); ++i) {
            CHECK(raw(spans[i].source_start) == raw(spans[i - 1].source_end));
            CHECK(raw(spans[i].output_start) == raw(spans[i - 1].output_end));
        }
        CHECK(raw(spans.back().source_end) == map->source_length().value());
        CHECK(raw(spans.back().output_end) == map->output_length().value());
        double previous = -1.0;
        for (int i = 0; i <= 200; ++i) {  // source axis: output never decreases where defined
            const auto t = map->to_output(src(sample_of(*map, i)));
            if (t) {
                CHECK(raw(*t) >= previous);
                previous = raw(*t);
            }
        }
    }
}

TEST_CASE(
    "to_output_is_empty_exactly_inside_cuts_and_to_source_is_generated_exactly_inside_inserts",
    "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto map = TimeMap::make(random_spans(rng)).value();
        for (const TimeSpan& sp : map.spans()) {
            const double s0 = raw(sp.source_start);
            const double s1 = raw(sp.source_end);
            const double t0 = raw(sp.output_start);
            const double t1 = raw(sp.output_end);
            if (sp.kind == SpanKind::cut) {
                for (double f : {0.0, 0.25, 0.5, 0.999}) {  // [start, end) is the cut
                    CHECK_FALSE(map.to_output(src(s0 + f * (s1 - s0))).has_value());
                }
            } else if (sp.kind == SpanKind::warp) {
                for (double f : {0.0, 0.25, 0.5, 0.999}) {
                    CHECK(map.to_output(src(s0 + f * (s1 - s0))).has_value());
                }
            }
            if (sp.kind == SpanKind::insert) {
                for (double f : {0.0, 0.25, 0.5, 0.999}) {
                    CHECK(std::holds_alternative<GeneratedTime>(
                        map.to_source(out(t0 + f * (t1 - t0)))));
                }
                CHECK(map.slope_at(out(t0 + 0.5 * (t1 - t0))) == Ratio{0.0});
            } else if (sp.kind == SpanKind::warp) {
                for (double f : {0.0, 0.25, 0.5, 0.999}) {
                    CHECK(
                        std::holds_alternative<SourceTime>(map.to_source(out(t0 + f * (t1 - t0)))));
                }
            }
        }
    }
}

TEST_CASE("the_round_trip_holds_for_every_non_cut_source_time", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto map = TimeMap::make(random_spans(rng)).value();
        if (map.source_length().value() == 0.0) {
            continue;  // only inserts: s == 0 is the closing point
        }
        for (int i = 0; i < 200; ++i) {  // s < source_length: the closing point is not a round trip
            const double s = sample_of(map, i);
            const auto t = map.to_output(src(s));
            if (!t) {
                continue;  // inside a cut
            }
            const auto back = map.to_source(*t);
            if (const auto* generated = std::get_if<GeneratedTime>(&back)) {
                // s lies within an ulp of a warp's end, where the output rounds onto the start of
                // the insert that follows: the boundary is ambiguous at that precision.
                CHECK(generated->offset.value() < 1e-9);
                continue;
            }
            CHECK(std::abs(raw(std::get<SourceTime>(back)) - s) <=
                  tolerance::k_timemap_roundtrip.value());
        }
    }
}

TEST_CASE("every_span_boundary_answers_as_documented", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto map = TimeMap::make(random_spans(rng)).value();
        const double length = map.source_length().value();
        for (const TimeSpan& sp : map.spans()) {
            const double s0 = raw(sp.source_start);
            const double s1 = raw(sp.source_end);
            if (sp.kind != SpanKind::insert && s0 < length) {  // right-continuous at a start
                const auto t = map.to_output(src(s0));
                CHECK(t.has_value() == (sp.kind == SpanKind::warp));
                if (t) {
                    CHECK(raw(*t) == raw(sp.output_start));
                }
            }
            if (sp.kind != SpanKind::insert && s1 > 0.0) {  // left-continuous at an end
                const auto t = map.to_output_end(src(s1));
                CHECK(t.has_value() == (sp.kind == SpanKind::warp));
                if (t) {
                    CHECK(std::abs(raw(*t) - raw(sp.output_end)) < 1e-12);
                }
            }
        }
    }
}

TEST_CASE("make_rejects_a_random_violation_of_each_invariant", "[time_map_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        auto spans = random_spans(rng);
        const auto count = spans.size();
        const auto i = static_cast<std::size_t>(uniform_int(rng, 0, static_cast<int>(count) - 1));
        switch (uniform_int(rng, 0, 2)) {
        case 0:
            spans[i].source_start = src(raw(spans[i].source_start) + 0.001);
            break;  // gap/overlap
        case 1: spans[i].output_start = out(raw(spans[i].output_start) + 0.001); break;
        default:
            spans[i].output_end = out(raw(spans[i].output_end) - 1e6);
            break;  // shape/negative
        }
        CHECK_FALSE(TimeMap::make(spans).has_value());
    }
}
