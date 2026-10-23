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

Env flat_env(double seconds, double db = -70.0) {
    return env_for(seconds, [db](double) { return db; });
}

Result<SegmentTimeline> build(const std::vector<Note>& notes, double end, const Env& e,
                              const GapParams& p = {}) {
    return build_segment_timeline(notes, src(end), e.activity, e.floor, p);
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
    for (const Gap& g : t.gaps()) {  // slice 5: one silence region over the whole gap
        REQUIRE(g.regions.size() == 1);
        CHECK(g.regions[0].kind == GapRegionKind::silence);
        CHECK(g.regions[0].start == g.start);
        CHECK(g.regions[0].end == g.end);
    }
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
