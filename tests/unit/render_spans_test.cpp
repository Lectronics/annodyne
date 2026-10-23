// render_region over a TimeMap with cut and insert spans (ADR-0008, Phase 2 addendum). Cuts have no
// output, inserts are digital silence, and every other sample is the source sample the map names.

#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/render_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace anno;
using namespace anno::test;

namespace {

/// These tests pin the map's own contract (read_through_map): no splice fade at cuts and inserts.
RenderParams hard_joins() {
    RenderParams params;
    params.layout.splice_fade = Seconds{0.0};
    return params;
}

constexpr WallTime k_t0{};
constexpr double k_rate = 48000.0;
constexpr std::int64_t k_margin = 480;  // 10 ms

TimeSpan span(SpanKind kind, double s0, double s1, double t0, double t1) {
    return TimeSpan{.kind = kind,
                    .source_start = src(s0),
                    .source_end = src(s1),
                    .output_start = out(t0),
                    .output_end = out(t1),
                    .segment = std::nullopt};
}

// 3 s source. Output: [0,1.25) source [0,1.25); [1.25,1.35) silence (the insert); [1.35,2.85)
// source [1.5,3.0). The source [1.25,1.5) is cut. Both are inside the gap between the notes.
std::shared_ptr<const TimeMap> cut_and_insert_map() {
    return std::make_shared<const TimeMap>(
        TimeMap::make({span(SpanKind::warp, 0.0, 1.25, 0.0, 1.25),
                       span(SpanKind::cut, 1.25, 1.5, 1.25, 1.25),
                       span(SpanKind::insert, 1.5, 1.5, 1.25, 1.35),
                       span(SpanKind::warp, 1.5, 3.0, 1.35, 2.85)})
            .value());
}

constexpr std::int64_t k_out_frames = 136800;  // 2.85 s
constexpr std::int64_t k_insert_begin = 60000;
constexpr std::int64_t k_insert_end = 64800;
constexpr std::int64_t k_shift = 7200;  // output i >= k_insert_end is source i + k_shift

/// The expected "original" of the output by hand: kept head, silence, shifted tail.
AudioBuffer expected_output(const Fixture& f) {
    AudioBuffer want =
        AudioBuffer::create(f.source.channel_count(), Samples{k_out_frames}, f.source.sample_rate())
            .value();
    const AudioBuffer all = f.source.read(Samples{0}, f.source.frame_count()).value();
    for (int c = 0; c < want.channel_count(); ++c) {
        for (std::int64_t i = 0; i < k_out_frames; ++i) {
            const auto o = static_cast<std::size_t>(i);
            if (i < k_insert_begin) {
                want.channel(c)[o] = all.channel(c)[o];
            } else if (i >= k_insert_end) {
                want.channel(c)[o] = all.channel(c)[static_cast<std::size_t>(i + k_shift)];
            }
        }
    }
    return want;
}

Fixture fixture_with_map(const std::vector<Note>& notes) {
    return make_fixture(notes, 3.0);
}

RenderInputs inputs_with(const Fixture& f, std::shared_ptr<const TimeMap> map) {
    RenderInputs in = f.inputs();
    in.time_map = std::move(map);
    return in;
}

OutputRange all_output() {
    return OutputRange::from_start(OutputTime{}, Seconds{2.85});
}

std::vector<Note> notes_a_b() {
    return {make_note("a", 0.5, 1.0), make_note("b", 1.6, 2.2)};
}

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

}  // namespace

TEST_CASE("a_map_with_a_cut_and_an_insert_renders_the_right_length_silence_and_source_samples",
          "[render_spans]") {
    const Fixture f = fixture_with_map(notes_a_b());
    const auto r = render_region(inputs_with(f, cut_and_insert_map()), all_output(),
                                 GainTestEngine{}, hard_joins())
                       .value();
    CHECK(audio_of(r).frame_count().value() == k_out_frames);
    CHECK(r->passthrough);
    CHECK(bit_identical(audio_of(r), expected_output(f)));
    for (int c = 0; c < audio_of(r).channel_count(); ++c) {  // the insert is digital silence
        for (std::int64_t i = k_insert_begin; i < k_insert_end; ++i) {
            REQUIRE(audio_of(r).channel(c)[static_cast<std::size_t>(i)] == 0.0F);
        }
    }
}

TEST_CASE("an_edited_note_after_the_splice_changes_only_its_blend_zone", "[render_spans]") {
    // Note b [1.6, 2.2] source sits at output [1.45, 2.05] = samples [69600, 98400).
    const Fixture f = fixture_with_map(notes_a_b()).with_edits(edits_of({gain_edit("b", 6.0)}));
    const auto r = render_region(inputs_with(f, cut_and_insert_map()), all_output(),
                                 GainTestEngine{}, hard_joins())
                       .value();
    REQUIRE(r->notes_rendered.size() == 1);
    const AudioBuffer want = expected_output(f);
    const float g = static_cast<float>(to_linear_gain(Decibels{6.0}).value());
    constexpr std::int64_t k_b_begin = 69600;
    constexpr std::int64_t k_b_end = 98400;
    for (int c = 0; c < want.channel_count(); ++c) {
        for (std::int64_t i = 0; i < k_out_frames; ++i) {
            const auto o = static_cast<std::size_t>(i);
            const float original = want.channel(c)[o];
            const float got = audio_of(r).channel(c)[o];
            if (i < k_b_begin - k_margin || i >= k_b_end + k_margin) {
                REQUIRE(got == original);  // outside the blend zone, bit for bit
            } else if (i >= k_b_begin && i < k_b_end) {
                REQUIRE(got == original * g);  // the plateau is the engine's output
            }
        }
    }
}

TEST_CASE("an_edited_note_before_the_splice_changes_only_its_blend_zone", "[render_spans]") {
    const Fixture f = fixture_with_map(notes_a_b()).with_edits(edits_of({gain_edit("a", -6.0)}));
    const auto r = render_region(inputs_with(f, cut_and_insert_map()), all_output(),
                                 GainTestEngine{}, hard_joins())
                       .value();
    const AudioBuffer want = expected_output(f);
    for (int c = 0; c < want.channel_count(); ++c) {
        for (std::int64_t i = 0; i < k_out_frames; ++i) {
            if (i < 24000 - k_margin || i >= 48000 + k_margin) {
                REQUIRE(audio_of(r).channel(c)[static_cast<std::size_t>(i)] ==
                        want.channel(c)[static_cast<std::size_t>(i)]);
            }
        }
    }
}

TEST_CASE("a_region_of_the_output_equals_the_same_slice_of_the_whole", "[render_spans]") {
    const Fixture f = fixture_with_map(notes_a_b());
    const auto map = cut_and_insert_map();
    const auto part =
        render_region(inputs_with(f, map), OutputRange::from_start(out(1.2), Seconds{0.3}),
                      PassthroughEngine{}, hard_joins())
            .value();
    const AudioBuffer want = expected_output(f);
    REQUIRE(audio_of(part).frame_count().value() == 14400);
    for (std::int64_t i = 0; i < 14400; ++i) {
        REQUIRE(audio_of(part).channel(0)[static_cast<std::size_t>(i)] ==
                want.channel(0)[static_cast<std::size_t>(i + 57600)]);
    }
}

TEST_CASE("a_supplied_map_wins_over_timing_edits", "[render_spans]") {
    const Fixture f =
        fixture_with_map(notes_a_b())
            .with_edits(edits_of({make_spec("b", EditCategory::time, EditParam::onset_shift,
                                            EditOp::set, Seconds{0.4})}));
    const auto r = render_region(inputs_with(f, cut_and_insert_map()), all_output(),
                                 PassthroughEngine{}, hard_joins());
    REQUIRE(
        r.has_value());  // without the map the stereo source cannot be stretched (not_implemented)
    CHECK(bit_identical(audio_of(*r), expected_output(f)));
    CHECK(render_region(f.inputs(), whole(f.source), PassthroughEngine{}, hard_joins())
              .error()
              .code == ErrorCode::not_implemented);
}

TEST_CASE("the_cache_key_includes_the_map", "[render_spans]") {
    const Fixture f = fixture_with_map(notes_a_b());
    RenderCache cache{64'000'000};
    RenderParams params;
    params.cache = &cache;
    const auto identity = std::make_shared<const TimeMap>(TimeMap::identity(Seconds{3.0}));
    const auto a =
        render_region(inputs_with(f, identity), OutputRange::from_start(OutputTime{}, Seconds{1.0}),
                      PassthroughEngine{}, params)
            .value();
    const auto b = render_region(inputs_with(f, cut_and_insert_map()),
                                 OutputRange::from_start(OutputTime{}, Seconds{1.0}),
                                 PassthroughEngine{}, params)
                       .value();
    CHECK(a->cache_id != b->cache_id);
    // the same map again is a hit
    const auto c = render_region(inputs_with(f, cut_and_insert_map()),
                                 OutputRange::from_start(OutputTime{}, Seconds{1.0}),
                                 PassthroughEngine{}, params)
                       .value();
    CHECK(c.get() == b.get());
}

TEST_CASE("bad_maps_and_notes_are_refused", "[render_spans]") {
    const Fixture f = fixture_with_map(notes_a_b());
    const auto wrong_length = std::make_shared<const TimeMap>(TimeMap::identity(Seconds{2.0}));
    CHECK(
        render_region(inputs_with(f, wrong_length), all_output(), PassthroughEngine{}, hard_joins())
            .error()
            .code == ErrorCode::invalid_argument);
    const auto fractional = std::make_shared<const TimeMap>(
        TimeMap::make({span(SpanKind::insert, 0.0, 0.0, 0.0, 1.5 / k_rate),
                       span(SpanKind::warp, 0.0, 3.0, 1.5 / k_rate, 3.0 + 1.5 / k_rate)})
            .value());
    CHECK(render_region(inputs_with(f, fractional),
                        OutputRange::from_start(OutputTime{}, Seconds{1.0}), PassthroughEngine{},
                        {})
              .error()
              .code == ErrorCode::not_implemented);
    // an edited note that starts inside a cut
    const Fixture inside_cut =
        make_fixture({make_note("c", 1.3, 1.4)}, 3.0).with_edits(edits_of({gain_edit("c", 3.0)}));
    CHECK(render_region(inputs_with(inside_cut, cut_and_insert_map()), all_output(),
                        GainTestEngine{}, hard_joins())
              .error()
              .code == ErrorCode::invalid_argument);
    // a region past the end of the (shorter) output
    CHECK(render_region(inputs_with(f, cut_and_insert_map()),
                        OutputRange::from_start(OutputTime{}, Seconds{3.0}), PassthroughEngine{},
                        {})
              .error()
              .code == ErrorCode::out_of_range);
}

TEST_CASE("maps_that_differ_in_a_single_span_give_different_cache_keys", "[render_spans]") {
    const Fixture f = fixture_with_map(notes_a_b());
    const auto longer_insert =
        std::make_shared<const TimeMap>(TimeMap::make({span(SpanKind::warp, 0.0, 1.25, 0.0, 1.25),
                                                       span(SpanKind::cut, 1.25, 1.5, 1.25, 1.25),
                                                       span(SpanKind::insert, 1.5, 1.5, 1.25, 1.45),
                                                       span(SpanKind::warp, 1.5, 3.0, 1.45, 2.95)})
                                            .value());
    // A region that meets neither the insert nor anything after it: only the map differs.
    const OutputRange head = OutputRange::from_start(OutputTime{}, Seconds{1.0});
    const auto a =
        render_region(inputs_with(f, cut_and_insert_map()), head, PassthroughEngine{}, hard_joins())
            .value();
    const auto b =
        render_region(inputs_with(f, longer_insert), head, PassthroughEngine{}, hard_joins())
            .value();
    CHECK(a->cache_id != b->cache_id);
    CHECK(bit_identical(audio_of(a), audio_of(b)));  // same samples, different keys
}

TEST_CASE("an_edited_note_outside_the_recording_is_refused", "[render_spans]") {
    const Fixture late = make_fixture({make_note("late", 2.5, 3.5)}, 3.0)
                             .with_edits(edits_of({gain_edit("late", 3.0)}));
    CHECK(render_region(inputs_with(late, cut_and_insert_map()), all_output(), GainTestEngine{},
                        hard_joins())
              .error()
              .code == ErrorCode::invalid_argument);
}
