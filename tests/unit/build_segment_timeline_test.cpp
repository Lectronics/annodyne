#include "core/analysis/gaps/build_segment_timeline.h"
#include "support/test_data.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

Result<SegmentTimeline> build(const std::vector<Note>& notes, double end, const Env& e,
                              const GapParams& p = {}) {
    return build_segment_timeline(notes, src(end), e.activity, e.floor, p);
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
