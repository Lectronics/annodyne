#include "core/analysis/gaps/build_segment_timeline.h"
#include "support/test_data.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_hop = 0.005;

struct Env {
    ActivityTrack activity;
    NoiseFloor floor;
};

/// An activity track and a floor curve over [0, seconds] whose level is `level_at(t)`.
template <class F>
Env env_for(double seconds, F level_at) {
    FrameGrid grid;
    grid.hop = Seconds{k_hop};
    grid.start = SourceTime{};
    grid.n_frames = static_cast<std::int64_t>(std::floor(seconds / k_hop)) + 1;
    Env e;
    e.activity.grid = grid;
    e.floor.grid = grid;
    for (std::int64_t i = 0; i < grid.n_frames; ++i) {
        const double t = static_cast<double>(i) * k_hop;
        e.activity.level.emplace_back(level_at(t));
        e.activity.activity.push_back(FrameActivity::silent);
        e.floor.curve_dbfs.emplace_back(level_at(t));
    }
    e.floor.band_centre = {Hz{1000.0}};
    e.floor.band_dbfs = {Decibels{-80.0}};
    return e;
}

/// The floor curve is `floor_db`; the activity level is `level_at(t)`.
template <class F>
Env env_with_level(double seconds, double floor_db, F level_at) {
    Env e = env_for(seconds, [floor_db](double) { return floor_db; });
    for (std::size_t i = 0; i < e.activity.level.size(); ++i) {
        e.activity.level[i] = Decibels{level_at(static_cast<double>(i) * k_hop)};
    }
    return e;
}

Env flat_env(double seconds, double db = -70.0) {
    return env_for(seconds, [db](double) { return db; });
}

Result<SegmentTimeline> build_with(const std::vector<Note>& notes, double end, const Env& e,
                                   const GapEvidence& evidence, const GapParams& p = {}) {
    return build_segment_timeline(notes, src(end), e.activity, e.floor, evidence, p);
}

Result<SegmentTimeline> build(const std::vector<Note>& notes, double end, const Env& e,
                              const GapParams& p = {}) {
    return build_with(notes, end, e, neutral_gap_evidence(e.activity.grid), p);
}

/// Marks the frames in [from_s, to_s) with a class and an excess over the floor.
void mark(GapEvidence& ev, double from_s, double to_s, FrameClass c, double excess_db) {
    for (std::size_t i = 0; i < ev.frame_class.size(); ++i) {
        const double t = static_cast<double>(i) * k_hop;
        if (t >= from_s - 1e-9 && t < to_s - 1e-9) {
            ev.frame_class[i] = c;
            ev.excess_db[i] = Decibels{excess_db};
        }
    }
}

std::vector<GapRegionKind> kinds_of(const Gap& g) {
    std::vector<GapRegionKind> kinds;
    kinds.reserve(g.regions.size());
    for (const GapRegion& r : g.regions) {
        kinds.push_back(r.kind);
    }
    return kinds;
}

double start_of(const GapRegion& r) {
    return (r.start - SourceTime{}).value();
}
double end_of(const GapRegion& r) {
    return (r.end - SourceTime{}).value();
}

const Gap& gap_at(const SegmentTimeline& t, std::size_t i) {
    return t.gaps()[i];
}

SegmentId gap_id(const char* before, const char* after) {
    return gap_segment_id(before != nullptr ? std::optional<NoteId>{NoteId{before}} : std::nullopt,
                          after != nullptr ? std::optional<NoteId>{NoteId{after}} : std::nullopt);
}

}  // namespace

TEST_CASE("notes_and_the_gaps_between_them_tile_the_recording", "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    const auto t = build({make_note("a", 1, 2), make_note("b", 3, 4)}, 5.0, e).value();
    REQUIRE(t.gaps().size() == 3);
    REQUIRE(t.segments().size() == 5);
    CHECK(gap_at(t, 0).id == gap_id(nullptr, "a"));
    CHECK(gap_at(t, 0).start == src(0.0));
    CHECK(gap_at(t, 0).end == src(1.0));
    CHECK(gap_at(t, 1).id == gap_id("a", "b"));
    CHECK(gap_at(t, 1).start == src(2.0));
    CHECK(gap_at(t, 1).end == src(3.0));
    CHECK(gap_at(t, 2).id == gap_id("b", nullptr));
    CHECK(gap_at(t, 2).end == src(5.0));
    CHECK(t.source_end() == src(5.0));
    // A flat level at the floor: no tail, no audible stretch; the guard before a note is protected.
    using K = GapRegionKind;
    CHECK(kinds_of(gap_at(t, 0)) == std::vector<K>{K::silence, K::protected_preroll});
    CHECK(kinds_of(gap_at(t, 1)) == std::vector<K>{K::silence, K::protected_preroll});
    CHECK(kinds_of(gap_at(t, 2)) == std::vector<K>{K::silence});  // trailing: no note follows
    for (const Gap& g : t.gaps()) {
        CHECK(g.regions.front().start == g.start);  // the regions tile the gap
        CHECK(g.regions.back().end == g.end);
    }
    CHECK(end_of(gap_at(t, 1).regions[0]) == Catch::Approx(3.0 - 0.015).margin(1e-9));
}

TEST_CASE("touching_notes_have_no_gap_and_edge_notes_no_edge_gap", "[segment_timeline_builder]") {
    const Env e = flat_env(4.0);
    const auto t = build({make_note("a", 0, 2), make_note("b", 2, 4)}, 4.0, e).value();
    CHECK(t.gaps().empty());
    CHECK(t.segments().size() == 2);
    const auto one = build({make_note("a", 0, 1.5), make_note("b", 2, 4)}, 4.0, e).value();
    REQUIRE(one.gaps().size() == 1);
    CHECK(one.gaps()[0].id == gap_id("a", "b"));
}

TEST_CASE("a_recording_without_notes_is_one_gap", "[segment_timeline_builder]") {
    const Env e = flat_env(3.0);
    const auto t = build({}, 3.0, e).value();
    REQUIRE(t.gaps().size() == 1);
    CHECK(t.gaps()[0].id == gap_id(nullptr, nullptr));
    CHECK(t.gaps()[0].start == src(0.0));
    CHECK(t.gaps()[0].end == src(3.0));
    CHECK(build({}, 0.0, flat_env(0.0)).value().segments().empty());
}

TEST_CASE("ghost_notes_do_not_take_part", "[segment_timeline_builder]") {
    Note ghost = make_note("g", 1.2, 1.8);
    ghost.is_active = false;
    const Env e = flat_env(4.0);
    const auto t = build({make_note("a", 0.5, 1), ghost, make_note("b", 2, 3)}, 4.0, e).value();
    CHECK(t.gaps().size() == 3);
    CHECK(t.gaps()[1].id == gap_id("a", "b"));
}

TEST_CASE("a_gap_floor_is_the_median_of_the_curve_over_it", "[segment_timeline_builder]") {
    // -80 dB before 2 s, -60 dB after: the gap [1,3] holds equal shares of both -> mean of the two
    // middle values; the gap [4,5] is all -60.
    const Env e = env_for(5.0, [](double t) { return t < 2.0 ? -80.0 : -60.0; });
    const auto t = build({make_note("a", 0.5, 1.0), make_note("b", 3.0, 4.0)}, 5.0, e).value();
    REQUIRE(t.gaps().size() == 3);
    CHECK(t.gaps()[0].floor_dbfs.value() == Catch::Approx(-80.0));  // [0, 0.5]
    CHECK(t.gaps()[1].floor_dbfs.value() == Catch::Approx(-70.0));  // [1, 3]: half and half
    CHECK(t.gaps()[2].floor_dbfs.value() == Catch::Approx(-60.0));
}

TEST_CASE("a_gap_shorter_than_a_frame_takes_the_nearest_frame", "[segment_timeline_builder]") {
    const Env e = env_for(2.0, [](double t) { return -90.0 + 10.0 * t; });
    const auto t = build({make_note("a", 0.5, 1.0), make_note("b", 1.001, 1.5)}, 2.0, e).value();
    const Gap& g = t.gaps()[1];  // [1.0, 1.001]: no frame inside, frame 200 (1.0 s) is nearest
    CHECK(g.id == gap_id("a", "b"));
    CHECK(g.floor_dbfs.value() == Catch::Approx(-80.0).margin(0.1));
}

TEST_CASE("bad_notes_are_named_errors", "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    const auto code = [&](const std::vector<Note>& notes, double end = 5.0) {
        return build(notes, end, e).error().code;
    };
    CHECK(code({make_note("a", 1, 2.5), make_note("b", 2, 3)}) == ErrorCode::invalid_argument);
    CHECK(code({make_note("b", 3, 4), make_note("a", 1, 2)}) == ErrorCode::invalid_argument);
    CHECK(code({make_note("a", 1, 6)}) == ErrorCode::invalid_argument);
    CHECK(code({make_note("a", 2, 2)}) == ErrorCode::invalid_argument);
    CHECK(code({make_note("a", -1, 2)}) == ErrorCode::invalid_argument);
    CHECK(code({make_note("a", 1, 2)}, std::nan("")) == ErrorCode::invalid_argument);
}

TEST_CASE("bad_params_and_mismatched_grids_are_named_errors", "[segment_timeline_builder]") {
    const Env e = flat_env(3.0);
    GapParams p;
    p.max_tail = Seconds{0.0};
    CHECK(build({}, 3.0, e, p).error().code == ErrorCode::invalid_argument);
    p = {};
    p.preroll_guard = Seconds{-1.0};
    CHECK(build({}, 3.0, e, p).error().code == ErrorCode::invalid_argument);
    p = {};
    p.tail_margin = Decibels{std::nan("")};
    CHECK(build({}, 3.0, e, p).error().code == ErrorCode::invalid_argument);

    Env other = flat_env(3.0);
    other.floor.grid.hop = Seconds{0.01};
    CHECK(build({}, 3.0, other).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("the_timeline_agrees_with_the_notes_it_was_built_from", "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    const std::vector<Note> notes{make_note("a", 1, 2), make_note("b", 3, 4.5)};
    AnalysisResult a;
    a.source_id = SourceId{"s"};
    a.notes = notes;
    a.timeline = build(notes, 5.0, e).value();
    CHECK(validate(a).has_value());  // every active note is one note segment with its range
}

TEST_CASE("a_decaying_tail_follows_the_previous_note_while_it_is_above_the_floor",
          "[segment_timeline_builder]") {
    // Floor -70 dB; after the note ends at 2.0 the level falls from -40 dB at 400 dB/s: it is
    // above floor + 6 dB (-64) until 2.06 s.
    const Env e = env_with_level(4.0, -70.0, [](double t) {
        if (t < 1.0) {
            return -70.0;
        }
        return t < 2.0 ? -40.0 : std::max(-70.0, -40.0 - 400.0 * (t - 2.0));
    });
    const auto t = build({make_note("a", 1.0, 2.0), make_note("b", 3.0, 3.5)}, 4.0, e).value();
    const Gap& g = t.gaps()[1];
    using K = GapRegionKind;
    REQUIRE(kinds_of(g) == std::vector<K>{K::tail, K::silence, K::protected_preroll});
    CHECK(start_of(g.regions[0]) == Catch::Approx(2.0));
    CHECK(end_of(g.regions[0]) == Catch::Approx(2.06).margin(k_hop));
    CHECK(kinds_of(t.gaps()[0]) == std::vector<K>{K::silence, K::protected_preroll});
}

TEST_CASE("a_tail_is_capped_and_the_rest_of_a_long_decay_stays_protected",
          "[segment_timeline_builder]") {
    const Env e = env_with_level(6.0, -70.0, [](double t) { return t < 1.0 ? -20.0 : -40.0; });
    GapParams p;
    p.max_tail = Seconds{1.5};
    const auto t = build({make_note("a", 0.5, 1.0), make_note("b", 5.5, 6.0)}, 6.0, e, p).value();
    const Gap& g = t.gaps()[1];  // [1.0, 5.5]: the level stays high all through
    using K = GapRegionKind;
    REQUIRE(kinds_of(g) == std::vector<K>{K::tail, K::audible, K::protected_preroll});
    CHECK(end_of(g.regions[0]) - start_of(g.regions[0]) == Catch::Approx(1.5).margin(k_hop));
    CHECK(end_of(g.regions[1]) == Catch::Approx(5.5 - 0.015).margin(1e-9));
}

TEST_CASE("an_isolated_fricative_in_a_gap_is_audible_wherever_it_sits",
          "[segment_timeline_builder]") {
    // 200 ms of -50 dB (20 dB over the floor) in the middle of a 2 s pause.
    const Env e =
        env_with_level(5.0, -70.0, [](double t) { return (t >= 2.0 && t < 2.2) ? -50.0 : -70.0; });
    const auto t = build({make_note("a", 0.5, 1.0), make_note("b", 3.0, 4.0)}, 5.0, e).value();
    const Gap& g = t.gaps()[1];  // [1.0, 3.0]
    using K = GapRegionKind;
    REQUIRE(kinds_of(g) ==
            std::vector<K>{K::silence, K::audible, K::silence, K::protected_preroll});
    CHECK(start_of(g.regions[1]) == Catch::Approx(2.0 - 0.01).margin(k_hop));  // padded
    CHECK(end_of(g.regions[1]) == Catch::Approx(2.2 + 0.01).margin(k_hop));
}

TEST_CASE("a_blip_shorter_than_min_audible_is_ignored_and_the_threshold_is_strict",
          "[segment_timeline_builder]") {
    using K = GapRegionKind;
    const Env blip = env_with_level(5.0, -70.0, [](double t) {
        return (t >= 2.0 && t < 2.010) ? -50.0 : -70.0;  // 10 ms
    });
    const auto a = build({make_note("a", 0.5, 1.0), make_note("b", 3.0, 4.0)}, 5.0, blip).value();
    CHECK(kinds_of(a.gaps()[1]) == std::vector<K>{K::silence, K::protected_preroll});
    const Env at_margin = env_with_level(5.0, -70.0, [](double t) {
        return (t >= 2.0 && t < 2.2) ? -64.0 : -70.0;  // exactly floor + tail_margin: not above
    });
    const auto b =
        build({make_note("a", 0.5, 1.0), make_note("b", 3.0, 4.0)}, 5.0, at_margin).value();
    CHECK(kinds_of(b.gaps()[1]) == std::vector<K>{K::silence, K::protected_preroll});
}

TEST_CASE("the_preroll_guard_is_clipped_to_a_short_gap_and_absent_before_no_note",
          "[segment_timeline_builder]") {
    const Env e = flat_env(3.0);
    using K = GapRegionKind;
    const auto t = build({make_note("a", 0.5, 1.0), make_note("b", 1.01, 2.0)}, 3.0, e).value();
    const Gap& tiny = t.gaps()[1];  // 10 ms, shorter than the 15 ms guard: all preroll
    CHECK(kinds_of(tiny) == std::vector<K>{K::protected_preroll});
    CHECK(tiny.regions[0].start == tiny.start);
    CHECK(kinds_of(t.gaps().back()) == std::vector<K>{K::silence});
}

TEST_CASE("a_leading_gap_has_no_tail_but_can_hold_an_audible_stretch",
          "[segment_timeline_builder]") {
    const Env e = env_with_level(3.0, -70.0, [](double t) { return t < 0.3 ? -45.0 : -70.0; });
    const auto t = build({make_note("a", 1.0, 2.0)}, 3.0, e).value();
    using K = GapRegionKind;
    CHECK(kinds_of(t.gaps()[0]) == std::vector<K>{K::audible, K::silence, K::protected_preroll});
    CHECK(t.gaps()[0].regions[0].start == t.gaps()[0].start);
}

TEST_CASE("a_larger_tail_margin_shortens_the_tail", "[segment_timeline_builder]") {
    const Env e = env_with_level(4.0, -70.0, [](double t) {
        return t < 2.0 ? -40.0 : std::max(-70.0, -40.0 - 400.0 * (t - 2.0));
    });
    const std::vector<Note> notes{make_note("a", 1.0, 2.0), make_note("b", 3.0, 3.5)};
    GapParams strict;
    strict.tail_margin = Decibels{20.0};  // above -50 dB only: until 2.025 s
    const auto narrow = build(notes, 4.0, e, strict).value();
    GapParams loose;
    loose.tail_margin = Decibels{3.0};  // above -67 dB: until 2.0675 s
    const auto wide = build(notes, 4.0, e, loose).value();
    const auto length = [](const SegmentTimeline& t) {
        const GapRegion& r = t.gaps()[1].regions[0];
        return end_of(r) - start_of(r);
    };
    CHECK(length(wide) > length(narrow));
}

TEST_CASE("audible_and_tail_params_are_validated", "[segment_timeline_builder]") {
    const Env e = flat_env(3.0);
    GapParams p;
    p.min_audible = Seconds{-0.01};
    CHECK(build({}, 3.0, e, p).error().code == ErrorCode::invalid_argument);
    p = {};
    p.audible_pad = Seconds{std::nan("")};
    CHECK(build({}, 3.0, e, p).error().code == ErrorCode::invalid_argument);
}

// ---- classified runs (consonant evidence, slice 6b) ----

namespace {

using K = GapRegionKind;

std::vector<Note> two_notes() {
    return {make_note("a", 0.5, 1.0), make_note("b", 3.0, 4.0)};
}

}  // namespace

TEST_CASE("a_quiet_sibilant_the_level_rule_misses_gets_its_own_region",
          "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);  // the level never leaves the floor
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.2, FrameClass::sibilant, 4.0);  // 4 dB over the floor: under tail_margin
    const auto t = build_with(two_notes(), 5.0, e, ev).value();
    const Gap& g = t.gaps()[1];
    REQUIRE(kinds_of(g) ==
            std::vector<K>{K::silence, K::sibilant, K::silence, K::protected_preroll});
    CHECK(start_of(g.regions[1]) == Catch::Approx(1.99).margin(k_hop));
    CHECK(end_of(g.regions[1]) == Catch::Approx(2.21).margin(k_hop));
}

TEST_CASE("each_class_becomes_its_own_kind", "[segment_timeline_builder]") {
    const Env e = flat_env(6.0);
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 1.5, 1.7, FrameClass::breath, 5.0);
    mark(ev, 2.2, 2.4, FrameClass::sibilant, 5.0);
    mark(ev, 3.0, 3.005, FrameClass::plosive, 8.0);  // one frame
    const auto t =
        build_with({make_note("a", 0.5, 1.0), make_note("b", 4.0, 5.0)}, 6.0, e, ev).value();
    CHECK(kinds_of(t.gaps()[1]) == std::vector<K>{K::silence, K::breath, K::silence, K::sibilant,
                                                  K::silence, K::plosive, K::silence,
                                                  K::protected_preroll});
}

TEST_CASE("the_evidence_threshold_is_strict", "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    for (const auto& [excess, expect_region] :
         {std::pair{2.9, false}, std::pair{3.0, false}, std::pair{3.1, true}}) {
        GapEvidence ev = neutral_gap_evidence(e.activity.grid);
        mark(ev, 2.0, 2.2, FrameClass::sibilant, excess);
        const auto t = build_with(two_notes(), 5.0, e, ev).value();
        CHECK((kinds_of(t.gaps()[1]).size() > 2) == expect_region);
    }
}

TEST_CASE("short_sibilants_are_ignored_but_a_plosive_of_any_length_is_kept",
          "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.01, FrameClass::sibilant, 10.0);  // 10 ms < min_audible
    CHECK(kinds_of(build_with(two_notes(), 5.0, e, ev).value().gaps()[1]) ==
          std::vector<K>{K::silence, K::protected_preroll});
    GapEvidence burst = neutral_gap_evidence(e.activity.grid);
    mark(burst, 2.0, 2.005, FrameClass::plosive, 10.0);
    const auto kinds = kinds_of(build_with(two_notes(), 5.0, e, burst).value().gaps()[1]);
    CHECK(std::ranges::count(kinds, K::plosive) == 1);
}

TEST_CASE("a_plosive_followed_by_a_sibilant_is_two_regions", "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.015, FrameClass::plosive, 10.0);
    mark(ev, 2.015, 2.2, FrameClass::sibilant, 10.0);  // an affricate
    const auto kinds = kinds_of(build_with(two_notes(), 5.0, e, ev).value().gaps()[1]);
    CHECK(kinds ==
          std::vector<K>{K::silence, K::plosive, K::sibilant, K::silence, K::protected_preroll});
}

TEST_CASE("a_level_run_partly_covered_keeps_its_uncovered_part_as_audible",
          "[segment_timeline_builder]") {
    const Env e = env_with_level(5.0, -70.0, [](double t) {
        return (t >= 2.0 && t < 2.4) ? -50.0 : -70.0;  // 20 dB over the floor
    });
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.2, FrameClass::sibilant, 20.0);
    const auto g = build_with(two_notes(), 5.0, e, ev).value().gaps()[1];
    CHECK(kinds_of(g) ==
          std::vector<K>{K::silence, K::sibilant, K::audible, K::silence, K::protected_preroll});
    CHECK(end_of(g.regions[1]) == start_of(g.regions[2]));  // adjacent, no overlap
}

TEST_CASE("an_audible_sliver_beside_a_classified_region_joins_it", "[segment_timeline_builder]") {
    const Env e = env_with_level(5.0, -70.0,
                                 [](double t) { return (t >= 2.0 && t < 2.205) ? -50.0 : -70.0; });
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.2, FrameClass::sibilant, 20.0);
    const auto g = build_with(two_notes(), 5.0, e, ev).value().gaps()[1];
    CHECK(kinds_of(g) == std::vector<K>{K::silence, K::sibilant, K::silence, K::protected_preroll});
    CHECK(end_of(g.regions[1]) == Catch::Approx(2.215).margin(k_hop));
}

TEST_CASE("classified_runs_ignore_the_guard_after_a_note_and_the_preroll",
          "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 1.0, 1.1, FrameClass::sibilant, 10.0);  // starts at the note's end
    const auto g = build_with(two_notes(), 5.0, e, ev).value().gaps()[1];
    REQUIRE(kinds_of(g) ==
            std::vector<K>{K::silence, K::sibilant, K::silence, K::protected_preroll});
    // The first counted frame is 1.015 (the guard skips 1.0-1.015); padding reaches 10 ms back.
    CHECK(start_of(g.regions[1]) == Catch::Approx(1.005).margin(1e-6));
    // A burst right before the next note (a note onset looks like one) is inside the preroll.
    GapEvidence onset = neutral_gap_evidence(e.activity.grid);
    mark(onset, 2.99, 3.0, FrameClass::plosive, 15.0);
    CHECK(kinds_of(build_with(two_notes(), 5.0, e, onset).value().gaps()[1]) ==
          std::vector<K>{K::silence, K::protected_preroll});
    // A leading gap has no note before it: no guard.
    GapEvidence lead = neutral_gap_evidence(e.activity.grid);
    mark(lead, 0.0, 0.2, FrameClass::breath, 10.0);
    CHECK(start_of(build_with(two_notes(), 5.0, e, lead).value().gaps()[0].regions[0]) == 0.0);
}

TEST_CASE("evidence_that_does_not_match_is_a_named_error", "[segment_timeline_builder]") {
    const Env e = flat_env(5.0);
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    ev.excess_db.pop_back();
    CHECK(build_with(two_notes(), 5.0, e, ev).error().code == ErrorCode::invalid_argument);
    GapEvidence other = neutral_gap_evidence(e.activity.grid);
    other.grid.hop = Seconds{0.01};
    CHECK(build_with(two_notes(), 5.0, e, other).error().code == ErrorCode::invalid_argument);
    GapParams p;
    p.event_margin = Decibels{std::nan("")};
    CHECK(build(two_notes(), 5.0, e, p).error().code == ErrorCode::invalid_argument);
    p = {};
    p.evidence_guard = Seconds{-1.0};
    CHECK(build(two_notes(), 5.0, e, p).error().code == ErrorCode::invalid_argument);
}

TEST_CASE("regions_always_tile_the_gap_with_evidence", "[segment_timeline_builder]") {
    const Env e =
        env_with_level(6.0, -70.0, [](double t) { return (t > 1.4 && t < 1.9) ? -45.0 : -70.0; });
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 1.5, 1.6, FrameClass::sibilant, 25.0);
    mark(ev, 1.6, 1.62, FrameClass::plosive, 25.0);
    mark(ev, 2.5, 2.8, FrameClass::breath, 6.0);
    const auto t =
        build_with({make_note("a", 0.5, 1.0), make_note("b", 4.0, 5.0)}, 6.0, e, ev).value();
    for (const Gap& g : t.gaps()) {
        CHECK(g.regions.front().start == g.start);
        CHECK(g.regions.back().end == g.end);
        for (std::size_t i = 1; i < g.regions.size(); ++i) {
            CHECK(g.regions[i].start == g.regions[i - 1].end);
        }
    }
}

TEST_CASE("a_sibilant_inside_a_tail_cuts_it_but_a_breath_inside_a_tail_is_the_tail",
          "[segment_timeline_builder]") {
    // The level stays 20 dB over the floor from the note's end at 1.0 to 1.5 (a decay, and the
    // high-pass ringing through a consonant).
    const Env e =
        env_with_level(5.0, -70.0, [](double t) { return (t >= 1.0 && t < 1.5) ? -50.0 : -70.0; });
    GapEvidence hiss = neutral_gap_evidence(e.activity.grid);
    mark(hiss, 1.2, 1.4, FrameClass::sibilant, 20.0);
    const auto cut = build_with(two_notes(), 5.0, e, hiss).value().gaps()[1];
    // The level stays up to 1.5 s: after the hiss (to 1.41) 90 ms remain audible.
    CHECK(kinds_of(cut) ==
          std::vector<K>{K::tail, K::sibilant, K::audible, K::silence, K::protected_preroll});
    CHECK(end_of(cut.regions[0]) == Catch::Approx(1.19).margin(k_hop));  // padded start of the hiss
    CHECK(start_of(cut.regions[1]) == end_of(cut.regions[0]));

    GapEvidence decay = neutral_gap_evidence(e.activity.grid);
    mark(decay, 1.1, 1.4, FrameClass::breath, 20.0);  // low-frequency: the decay itself
    CHECK(kinds_of(build_with(two_notes(), 5.0, e, decay).value().gaps()[1]) ==
          std::vector<K>{K::tail, K::silence, K::protected_preroll});
}

TEST_CASE("a_breath_after_the_tail_is_still_a_breath", "[segment_timeline_builder]") {
    const Env e = env_with_level(5.0, -70.0, [](double t) {
        return (t >= 1.0 && t < 1.1) ? -50.0 : -70.0;  // a short decay
    });
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.3, FrameClass::breath, 6.0);
    CHECK(kinds_of(build_with(two_notes(), 5.0, e, ev).value().gaps()[1]) ==
          std::vector<K>{K::tail, K::silence, K::breath, K::silence, K::protected_preroll});
}

TEST_CASE("an_audible_sliver_between_two_classified_regions_joins_only_one",
          "[segment_timeline_builder]") {
    // Padding leaves a 5 ms audible piece between a sibilant and a breath (the level stays up
    // through both): it joins one of them, never both (they would overlap and break the tiling).
    const Env e = env_with_level(5.0, -70.0,
                                 [](double t) { return (t >= 2.0 && t < 2.225) ? -50.0 : -70.0; });
    GapEvidence ev = neutral_gap_evidence(e.activity.grid);
    mark(ev, 2.0, 2.1, FrameClass::sibilant, 20.0);
    mark(ev, 2.125, 2.225, FrameClass::breath, 20.0);
    const auto t = build_with(two_notes(), 5.0, e, ev);
    REQUIRE(t.has_value());  // SegmentTimeline::make checks that the regions tile
    const Gap& g = t->gaps()[1];
    CHECK(kinds_of(g) ==
          std::vector<K>{K::silence, K::sibilant, K::breath, K::silence, K::protected_preroll});
    for (std::size_t i = 1; i < g.regions.size(); ++i) {
        CHECK(g.regions[i].start == g.regions[i - 1].end);
    }
}

TEST_CASE("random_levels_and_evidence_always_give_a_valid_tiling", "[segment_timeline_builder]") {
    // Real recordings found a tiling bug the hand-made cases did not (a sliver between two
    // classified regions), so the builder is also run on random scenes: it must always return a
    // timeline whose regions tile every gap (SegmentTimeline::make checks it).
    std::uint64_t state = 12345;
    const auto next = [&state](double lo, double hi) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return lo + (hi - lo) * static_cast<double>(state >> 40U) / 16777216.0;
    };
    const std::array<FrameClass, 4> classes{FrameClass::sibilant, FrameClass::breath,
                                            FrameClass::plosive, FrameClass::silence};
    for (int scene = 0; scene < 300; ++scene) {
        const double seconds = 6.0;
        std::vector<std::pair<double, double>> loud;  // level bursts, 20 dB over the floor
        for (int i = 0; i < 6; ++i) {
            const double a = next(0.0, seconds - 0.5);
            loud.emplace_back(a, a + next(0.005, 0.4));
        }
        const Env e = env_with_level(seconds, -70.0, [&](double t) {
            return std::ranges::any_of(loud,
                                       [t](const auto& b) { return t >= b.first && t < b.second; })
                       ? -50.0
                       : -70.0;
        });
        GapEvidence ev = neutral_gap_evidence(e.activity.grid);
        for (int i = 0; i < 8; ++i) {
            const double a = next(0.0, seconds - 0.5);
            mark(ev, a, a + next(0.005, 0.3), classes[static_cast<std::size_t>(next(0.0, 3.999))],
                 next(-2.0, 25.0));
        }
        std::vector<Note> notes;
        double at = next(0.0, 0.6);
        for (int i = 0; i < 4 && at < seconds - 0.8; ++i) {
            const double len = next(0.1, 0.8);
            notes.push_back(make_note("n" + std::to_string(i), at, at + len));
            at += len + (next(0.0, 1.0) < 0.2 ? 0.0 : next(0.005, 1.2));
        }
        GapParams p;
        p.audible_pad = Seconds{next(0.0, 0.03)};
        p.min_audible = Seconds{next(0.0, 0.05)};
        INFO("scene " << scene);
        const auto t = build_with(notes, seconds, e, ev, p);
        REQUIRE(t.has_value());
        for (const Gap& g : t->gaps()) {
            REQUIRE(g.regions.front().start == g.start);
            REQUIRE(g.regions.back().end == g.end);
        }
    }
}
