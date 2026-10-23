#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/render_fixture.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>

// render_region with the layout of timing edits (ADR-0025): gaps absorb first, the recording keeps
// its length, notes keep their samples, the joins are smoothed.

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
constexpr double k_rate = 48000.0;
constexpr std::int64_t k_frames = 144000;  // 3 s
// The shift grid at 48 kHz, 120 bpm is 188 samples; a move of 25 steps is on it.
constexpr std::int64_t k_move = std::int64_t{25} * 188;
constexpr double k_move_s = static_cast<double>(k_move) / k_rate;

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

EditSpec shift_of(const char* id, double seconds) {
    return make_spec(id, EditCategory::time, EditParam::onset_shift, EditOp::set, Seconds{seconds});
}

// a 0.5..1.0, b 1.5..2.0, c 2.2..2.6; gaps g0 [0, .5], gA [1, 1.5], gB [2, 2.2], gC [2.6, 3].
Fixture layout_fixture() {
    Fixture f =
        make_fixture({make_note("a", 0.5, 1.0), make_note("b", 1.5, 2.0), make_note("c", 2.2, 2.6)},
                     3.0, 1, k_rate);
    f.analysis->timeline = timeline_of(f.analysis->notes, 3.0);
    return f;
}

AudioBuffer source_audio(const Fixture& f) {
    return f.source.read(Samples{0}, f.source.frame_count()).value();
}

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// out[i + shift] equals src[i] bit for bit for i in [from, to), both channels.
bool shifted_copy(const AudioBuffer& out, const AudioBuffer& src, std::int64_t from,
                  std::int64_t to, std::int64_t shift) {
    for (int c = 0; c < src.channel_count(); ++c) {
        for (std::int64_t i = from; i < to; ++i) {
            if (!same(out.channel(c)[static_cast<std::size_t>(i + shift)],
                      src.channel(c)[static_cast<std::size_t>(i)])) {
                return false;
            }
        }
    }
    return true;
}

std::shared_ptr<const RenderResult> render_all(const Fixture& f, RenderParams params = {}) {
    PassthroughEngine engine;
    auto r = render_region(f.inputs(), whole(f.source), engine, params);
    INFO((r ? "" : r.error().message));
    REQUIRE(r.has_value());
    return *r;
}

}  // namespace

TEST_CASE("a_moved_note_keeps_its_samples_and_the_recording_keeps_its_length", "[render_layout]") {
    const Fixture f = layout_fixture().with_edits(edits_of({shift_of("a", k_move_s)}));
    const auto r = render_all(f);
    const AudioBuffer src = source_audio(f);
    const AudioBuffer& out = audio_of(r);
    REQUIRE(out.frame_count().value() == k_frames);
    CHECK(r->layout_used);
    CHECK(r->repaired_notes == 0);
    CHECK(shifted_copy(out, src, 24000, 48000, k_move));  // a, shifted
    CHECK(shifted_copy(out, src, 72000, k_frames, 0));    // b and everything after: in place
}

TEST_CASE("the_insert_in_the_leading_gap_is_silent_and_faded_in_and_out", "[render_layout]") {
    const Fixture f = layout_fixture().with_edits(edits_of({shift_of("a", k_move_s)}));
    const auto r = render_all(f);
    const AudioBuffer& out = audio_of(r);
    // g0 = [0, 24000): the insert (k_move) starts at the centre, 12000
    for (int c = 0; c < 1; ++c) {
        for (std::int64_t i = 12000; i < 12000 + k_move; ++i) {
            REQUIRE(out.channel(c)[static_cast<std::size_t>(i)] == 0.0F);
        }
    }
    const AudioBuffer src = source_audio(f);
    CHECK(shifted_copy(out, src, 0, 12000 - 240, 0));  // before the fade-out: the source
    CHECK(shifted_copy(out, src, 12000, 24000, k_move) == false);  // the silence region differs
    CHECK(shifted_copy(out, src, 12000 + 240, 24000, k_move));  // after the fade-in: shifted source
}

TEST_CASE("the_cut_in_the_gap_after_the_note_joins_without_changing_the_notes", "[render_layout]") {
    const Fixture f = layout_fixture().with_edits(edits_of({shift_of("a", k_move_s)}));
    const auto r = render_all(f);
    const AudioBuffer src = source_audio(f);
    const AudioBuffer& out = audio_of(r);
    // gA = [48000, 72000): k_move samples are cut at its centre, 60000 - k_move / 2
    CHECK(shifted_copy(out, src, 48000, 60000 - k_move / 2, k_move));  // the left remnant, shifted
    CHECK(shifted_copy(out, src, 60000 - k_move / 2 + k_move + 240, 72000,
                       0));  // the right remnant, past the fade
}

TEST_CASE("mode_stretch_does_not_use_the_layout_and_gives_a_different_render", "[render_layout]") {
    const Fixture f = layout_fixture().with_edits(edits_of({shift_of("a", k_move_s)}));
    RenderParams stretch;
    stretch.layout.mode = LayoutMode::stretch;
    const auto layout = render_all(f);
    const auto flex = render_all(f, stretch);
    CHECK_FALSE(flex->layout_used);
    CHECK(flex->stretched);
    CHECK(layout->cache_id != flex->cache_id);
}

TEST_CASE("a_layout_render_is_deterministic", "[render_layout]") {
    const Fixture f =
        layout_fixture().with_edits(edits_of({shift_of("a", k_move_s), shift_of("c", -0.0489)}));
    const auto first = render_all(f);
    const auto second = render_all(f);
    CHECK(shifted_copy(audio_of(first), audio_of(second), 0, k_frames, 0));
}

TEST_CASE("a_region_of_a_layout_render_equals_the_same_slice_of_the_whole", "[render_layout]") {
    const Fixture f = layout_fixture().with_edits(edits_of({shift_of("a", k_move_s)}));
    PassthroughEngine engine;
    const auto whole_result = render_all(f);
    const AudioBuffer& whole_out = audio_of(whole_result);
    // regions that start and end inside the splice zones and the insert
    for (const auto& [lo, hi] : {std::pair{11900, 12200}, std::pair{16600, 17200},
                                 std::pair{57500, 57800}, std::pair{62300, 62600}}) {
        const auto region =
            OutputRange::from_start(to_time_point<OutputAxis>(Samples{lo}, Hz{k_rate}),
                                    to_seconds(Samples{hi - lo}, Hz{k_rate}));
        const auto part = render_region(f.inputs(), region, engine, {}).value();
        const AudioBuffer& block = audio_of(part);
        for (int c = 0; c < 1; ++c) {
            for (std::int64_t i = 0; i < hi - lo; ++i) {
                REQUIRE(same(block.channel(c)[static_cast<std::size_t>(i)],
                             whole_out.channel(c)[static_cast<std::size_t>(lo + i)]));
            }
        }
    }
}

TEST_CASE("a_timing_edit_elsewhere_leaves_a_distant_blocks_key_alone", "[render_layout]") {
    // Moving c later holds g0 at slope 1 and stretches a..b..c; the block inside g0 never reads
    // the changed spans.
    RenderCache cache{64'000'000};
    RenderParams params;
    params.cache = &cache;
    PassthroughEngine engine;
    const Fixture base =
        layout_fixture().with_edits(edits_of({shift_of("c", 10.0 * 188.0 / k_rate)}));
    const Fixture moved = layout_fixture().with_edits(edits_of({shift_of("c", k_move_s)}));
    const auto block = OutputRange::from_start(OutputTime{}, Seconds{0.3});
    const auto before = render_region(base.inputs(), block, engine, params).value();
    const auto after = render_region(moved.inputs(), block, engine, params).value();
    CHECK(before->cache_id == after->cache_id);
    const auto late = OutputRange::from_start(out(2.0), Seconds{0.5});
    CHECK(render_region(base.inputs(), late, engine, params).value()->cache_id !=
          render_region(moved.inputs(), late, engine, params).value()->cache_id);
}

TEST_CASE("an_edited_note_whose_zone_meets_a_cut_or_insert_is_not_implemented", "[render_layout]") {
    // gA is 15 ms: b moved later puts the whole move into an insert there, 7.5 ms from b, inside
    // the 10 ms blend zone of a gain edit on b.
    Fixture f =
        make_fixture({make_note("a", 0.5, 1.0), make_note("b", 1.015, 2.0)}, 3.0, 1, k_rate);
    f.analysis->timeline = timeline_of(f.analysis->notes, 3.0);
    const std::vector<EditSpec> group{
        make_spec("a", EditCategory::time, EditParam::length_scale, EditOp::set, Ratio{1.0001}),
        shift_of("b", k_move_s), gain_edit("b", 3.0)};
    const Fixture edited = f.with_edits(edits_of(group));
    PassthroughEngine engine;
    const auto r = render_region(edited.inputs(), whole(edited.source), engine, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::not_implemented);
    // without the gain edit the same move renders
    const std::vector<EditSpec> timing_only{group[0], group[1]};
    CHECK(render_region(f.with_edits(edits_of(timing_only)).inputs(), whole(f.source), engine, {})
              .has_value());
}
