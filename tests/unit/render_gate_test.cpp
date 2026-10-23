#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/render_fixture.h"
#include "support/timeline_data.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>

// The gap gate in render_region (ADR-0024): a gap_gain edit scales the silence regions of its gap
// after the base and the blend; everything else is the source, bit for bit.

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
constexpr double k_rate = 48000.0;
constexpr std::int64_t k_fade = 240;       // 5 ms at 48 kHz
constexpr std::int64_t k_frames = 144000;  // 3 s at 48 kHz

SegmentId gap_a_b() {
    return gap_segment_id(NoteId{"a"}, NoteId{"b"});
}
SegmentId leading_gap() {
    return gap_segment_id(std::nullopt, NoteId{"a"});
}
SegmentId trailing_gap() {
    return gap_segment_id(NoteId{"b"}, std::nullopt);
}

EditSpec gap_gain(const SegmentId& gap, double db, EditOp op = EditOp::set) {
    EditSpec spec = make_spec("unused", EditCategory::gap, EditParam::gap_gain, op, Decibels{db});
    spec.target.segment_ids = {gap};
    return spec;
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

/// Stereo, 48 kHz, 3 s; a = 0.5..1.0, b = 1.5..2.0; the timeline holds the three gaps.
Fixture gap_fixture() {
    Fixture f = make_fixture({make_note("a", 0.5, 1.0), make_note("b", 1.5, 2.0)}, 3.0, 2, k_rate);
    f.analysis->timeline = timeline_of(f.analysis->notes, 3.0);
    return f;
}

AudioBuffer source_audio(const Fixture& f) {
    return f.source.read(Samples{0}, f.source.frame_count()).value();
}

std::shared_ptr<const RenderResult> render_all(const Fixture& f, const SynthesisEngine& engine,
                                               RenderParams params = {}) {
    return render_region(f.inputs(), whole(f.source), engine, params).value();
}

bool same_bits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// True if every sample of both channels in [from, to) is the source, bit for bit.
bool identical(const AudioBuffer& a, const AudioBuffer& b, std::int64_t from, std::int64_t to) {
    for (int c = 0; c < a.channel_count(); ++c) {
        for (std::int64_t i = from; i < to; ++i) {
            if (!same_bits(a.channel(c)[static_cast<std::size_t>(i)],
                           b.channel(c)[static_cast<std::size_t>(i)])) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("without_gap_edits_the_render_is_the_source", "[render_gate]") {
    const Fixture f = gap_fixture();
    PassthroughEngine engine;
    const auto r = render_all(f, engine);
    CHECK(identical(audio_of(r), source_audio(f), 0, k_frames));
    CHECK(r->gaps_rendered.empty());
    CHECK(r->passthrough);
}

TEST_CASE("a_muted_gap_is_silent_inside_and_the_source_outside", "[render_gate]") {
    const Fixture f = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -120.0)}));
    PassthroughEngine engine;
    const auto r = render_all(f, engine);
    const AudioBuffer src = source_audio(f);
    const AudioBuffer& out = audio_of(r);
    constexpr std::int64_t k_lo = 48000;  // the gap [1.0, 1.5]
    constexpr std::int64_t k_hi = 72000;
    CHECK(identical(out, src, 0, k_lo + 1));  // notes and the boundary sample
    CHECK(identical(out, src, k_hi, k_frames));
    for (int c = 0; c < 2; ++c) {
        for (std::int64_t i = k_lo + k_fade; i < k_hi - k_fade; ++i) {
            REQUIRE(out.channel(c)[static_cast<std::size_t>(i)] == 0.0F);
        }
        for (std::int64_t k = 1; k < k_fade; ++k) {  // the ramps are linear in gain
            const double g = 1.0 - static_cast<double>(k) / static_cast<double>(k_fade);
            const auto up = static_cast<std::size_t>(k_lo + k);
            const auto down = static_cast<std::size_t>(k_hi - 1 - k);
            REQUIRE(out.channel(c)[up] == Catch::Approx(src.channel(c)[up] * g).margin(1e-6));
            REQUIRE(out.channel(c)[down] == Catch::Approx(src.channel(c)[down] * g).margin(1e-6));
        }
    }
    REQUIRE(r->gaps_rendered.size() == 1);
    CHECK(r->gaps_rendered[0] == gap_a_b());
    CHECK_FALSE(r->passthrough);
}

TEST_CASE("a_gain_cut_scales_the_plateau", "[render_gate]") {
    const Fixture f = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -6.0)}));
    PassthroughEngine engine;
    const auto r = render_all(f, engine);
    const AudioBuffer src = source_audio(f);
    const double g = std::pow(10.0, -6.0 / 20.0);
    for (int c = 0; c < 2; ++c) {
        for (const std::int64_t i : {50000L, 60000L, 70000L}) {
            const auto at = static_cast<std::size_t>(i);
            CHECK(audio_of(r).channel(c)[at] == Catch::Approx(src.channel(c)[at] * g).margin(1e-6));
        }
    }
}

TEST_CASE("the_recordings_ends_are_not_ramped", "[render_gate]") {
    const Fixture f = gap_fixture().with_edits(
        edits_of({gap_gain(leading_gap(), -120.0), gap_gain(trailing_gap(), -120.0)}));
    PassthroughEngine engine;
    const auto r = render_all(f, engine);
    const AudioBuffer src = source_audio(f);
    const AudioBuffer& out = audio_of(r);
    CHECK(out.channel(0)[0] == 0.0F);                                       // no ramp at the start
    CHECK(out.channel(0)[static_cast<std::size_t>(k_frames - 1)] == 0.0F);  // nor at the end
    CHECK(identical(out, src, 23999, 96000));  // the boundary sample, notes, the gap between
    REQUIRE(r->gaps_rendered.size() == 2);
}

TEST_CASE("only_the_gaps_meeting_the_region_are_reported", "[render_gate]") {
    const Fixture f = gap_fixture().with_edits(
        edits_of({gap_gain(leading_gap(), -120.0), gap_gain(trailing_gap(), -120.0)}));
    PassthroughEngine engine;
    const auto r =
        render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{0.9}), engine, {})
            .value();
    REQUIRE(r->gaps_rendered.size() == 1);
    CHECK(r->gaps_rendered[0] == leading_gap());
}

TEST_CASE("blocks_of_a_gated_render_agree_with_the_whole", "[render_gate]") {
    const Fixture f = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -9.0)}));
    PassthroughEngine engine;
    const auto whole_r = render_all(f, engine);
    const auto first =
        render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{1.2}), engine, {})
            .value();
    const auto second =
        render_region(f.inputs(),
                      OutputRange::from_start(OutputTime{} + Seconds{1.2}, Seconds{1.8}), engine,
                      {})
            .value();
    const auto split = static_cast<std::size_t>(1.2 * k_rate);
    for (int c = 0; c < 2; ++c) {
        for (std::size_t i = 0; i < split; ++i) {
            REQUIRE(same_bits(audio_of(first).channel(c)[i], audio_of(whole_r).channel(c)[i]));
        }
        for (std::size_t i = 0; i < audio_of(second).channel(c).size(); ++i) {
            REQUIRE(
                same_bits(audio_of(second).channel(c)[i], audio_of(whole_r).channel(c)[split + i]));
        }
    }
}

TEST_CASE("a_gap_edit_without_a_matching_gap_is_a_named_error", "[render_gate]") {
    PassthroughEngine engine;
    const SegmentId missing = gap_segment_id(NoteId{"x"}, NoteId{"y"});
    const Fixture unknown = gap_fixture().with_edits(edits_of({gap_gain(missing, -6.0)}));
    const auto a = render_region(unknown.inputs(), whole(unknown.source), engine, {});
    REQUIRE_FALSE(a.has_value());
    CHECK(a.error().code == ErrorCode::invalid_argument);
    CHECK(a.error().message.find(missing.value) != std::string::npos);

    Fixture none = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -6.0)}));
    none.analysis->timeline.reset();
    const auto b = render_region(none.inputs(), whole(none.source), engine, {});
    REQUIRE_FALSE(b.has_value());
    CHECK(b.error().code == ErrorCode::invalid_argument);
}

TEST_CASE("an_orphaned_gap_edit_is_ignored", "[render_gate]") {
    EditSpec spec = gap_gain(gap_segment_id(NoteId{"x"}, NoteId{"y"}), -6.0);
    spec.target.orphaned_segment_ids = spec.target.segment_ids;
    spec.target.segment_ids.clear();
    const EditList list =
        EditList::from_parts(
            {.track = TrackId{"t1"},
             .edits = {Edit{
                 .seq = EditSeq{1}, .group = GroupId{1}, .created_at = k_t0, .spec = spec}},
             .cursor = 1,
             .discarded = {},
             .next_seq = EditSeq{2}})
            .value();
    const Fixture f = gap_fixture().with_edits(list);
    PassthroughEngine engine;
    const auto r = render_all(f, engine);
    CHECK(identical(audio_of(r), source_audio(f), 0, k_frames));
    CHECK(r->gaps_rendered.empty());
}

TEST_CASE("the_gate_changes_the_cache_key_and_a_repeat_hits_the_cache", "[render_gate]") {
    PassthroughEngine engine;
    RenderCache cache{std::size_t{64} << 20U};
    RenderParams params;
    params.cache = &cache;
    const Fixture quiet = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -6.0)}));
    const Fixture muted = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -12.0)}));
    const auto a1 = render_all(quiet, engine, params);
    const auto a2 = render_all(quiet, engine, params);
    const auto b = render_all(muted, engine, params);
    CHECK(a1.get() == a2.get());         // a hit
    CHECK(a1->cache_id != b->cache_id);  // the gain is part of the key
    RenderParams slow = params;
    slow.gate.gate_fade = Seconds{0.010};
    CHECK(render_all(quiet, engine, slow)->cache_id != a1->cache_id);  // so is the fade
    const Fixture plain = gap_fixture();
    CHECK(render_all(plain, engine, params)->cache_id != a1->cache_id);
}

TEST_CASE("the_gate_follows_a_time_map", "[render_gate]") {
    Fixture f = gap_fixture().with_edits(edits_of({gap_gain(gap_a_b(), -120.0)}));
    // 0.1 s of silence inserted at the start: the gap [1.0, 1.5] plays at [1.1, 1.6].
    const auto map = std::make_shared<const TimeMap>(
        TimeMap::from_anchors({{.source = src(0.0), .output = out(0.1)}}, Seconds{3.0}).value());
    RenderInputs inputs = f.inputs();
    inputs.time_map = map;
    PassthroughEngine engine;
    const auto r =
        render_region(inputs, OutputRange::from_start(OutputTime{}, Seconds{3.1}), engine, {})
            .value();
    const AudioBuffer& o = audio_of(r);
    CHECK(o.channel(0)[static_cast<std::size_t>(1.1 * k_rate) + 1000] == 0.0F);
    CHECK(o.channel(0)[static_cast<std::size_t>(1.1 * k_rate) - 1000] != 0.0F);
    CHECK(o.channel(0)[static_cast<std::size_t>(1.0 * k_rate) + 1000] != 0.0F);  // the old place
}

TEST_CASE("an_edited_neighbour_note_keeps_its_core_and_the_gate_wins_in_the_gap", "[render_gate]") {
    const Fixture f =
        gap_fixture().with_edits(edits_of({gain_edit("a", 6.0), gap_gain(gap_a_b(), -120.0)}));
    GainTestEngine engine;
    const auto r = render_all(f, engine);
    const AudioBuffer src = source_audio(f);
    const double g = std::pow(10.0, 6.0 / 20.0);
    const auto core = static_cast<std::size_t>(0.75 * k_rate);
    CHECK(audio_of(r).channel(0)[core] == Catch::Approx(src.channel(0)[core] * g).margin(1e-5));
    // 5 ms past the note's end the gate has reached zero although the note's blend zone (10 ms)
    // reaches 480 samples into the gap.
    CHECK(audio_of(r).channel(0)[48000 + static_cast<std::size_t>(k_fade) + 5] == 0.0F);
    CHECK(audio_of(r).channel(0)[60000] == 0.0F);
    REQUIRE(r->notes_rendered.size() == 1);
    REQUIRE(r->gaps_rendered.size() == 1);
}
