#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/audio_checks.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

TimeSpan span(SpanKind kind, double s0, double s1, double t0, double t1) {
    return TimeSpan{.kind = kind,
                    .source_start = src(s0),
                    .source_end = src(s1),
                    .output_start = out(t0),
                    .output_end = out(t1),
                    .segment = std::nullopt};
}

}  // namespace

TEST_CASE("passthrough_returns_the_source_samples_unchanged", "[passthrough_engine]") {
    const AudioSource source = AudioSource::from_buffer(make_signal(2, 1.0));
    const PassthroughEngine engine;
    const Note note = make_note("a", 0.2, 0.4);
    const OutputRange range =
        OutputRange::from_start(OutputTime::unchecked_from_origin(Seconds{0.1}), Seconds{0.5});
    const auto audio =
        engine.render_note(source, note, EffectiveNote{}, TimeMap::identity(Seconds{1.0}), range);
    REQUIRE(audio.has_value());
    const AudioBuffer expected = source.read(Samples{4800}, Samples{24000}).value();
    CHECK(bit_identical(audio->audio, expected));
    CHECK(output_range(*audio) == range);
    CHECK(engine.name() == "passthrough");
    CHECK(engine.settings_key().empty());
}

TEST_CASE("passthrough_plays_the_source_through_a_map_with_a_cut_and_an_insert",
          "[passthrough_engine]") {
    // 1 s source at 48 kHz: [0,0.25) kept, [0.25,0.5) cut, 0.1 s of silence, [0.5,1) shifted.
    const AudioSource source = AudioSource::from_buffer(make_signal(1, 1.0));
    const auto map = TimeMap::make({span(SpanKind::warp, 0.0, 0.25, 0.0, 0.25),
                                    span(SpanKind::cut, 0.25, 0.5, 0.25, 0.25),
                                    span(SpanKind::insert, 0.5, 0.5, 0.25, 0.35),
                                    span(SpanKind::warp, 0.5, 1.0, 0.35, 0.85)})
                         .value();
    const PassthroughEngine engine;
    const Note note = make_note("a", 0.6, 0.8);
    const OutputRange range = OutputRange::from_start(OutputTime{}, Seconds{0.85});
    const auto audio = engine.render_note(source, note, EffectiveNote{}, map, range);
    REQUIRE(audio.has_value());
    CHECK(audio->audio.frame_count().value() == 40800);
    const auto got = audio->audio.channel(0);
    const auto want = source.read(Samples{0}, Samples{48000}).value();
    for (std::int64_t i = 0; i < 12000; ++i) {
        REQUIRE(got[static_cast<std::size_t>(i)] == want.channel(0)[static_cast<std::size_t>(i)]);
    }
    for (std::int64_t i = 12000; i < 16800; ++i) {
        REQUIRE(got[static_cast<std::size_t>(i)] == 0.0F);
    }
    for (std::int64_t i = 16800; i < 40800; ++i) {  // output i is source i - 16800 + 24000
        REQUIRE(got[static_cast<std::size_t>(i)] ==
                want.channel(0)[static_cast<std::size_t>(i + 7200)]);
    }
}

TEST_CASE("passthrough_refuses_a_stretch_a_fractional_shift_and_a_range_outside_the_output",
          "[passthrough_engine]") {
    const AudioSource source = AudioSource::from_buffer(make_signal(1, 1.0));
    const PassthroughEngine engine;
    const Note note = make_note("a", 0.2, 0.4);
    const OutputRange range = OutputRange::from_start(OutputTime{}, Seconds{0.5});
    const auto stretched =
        TimeMap::from_anchors({{.source = src(0.5), .output = out(0.6)}}, Seconds{1.0}).value();
    CHECK(engine.render_note(source, note, EffectiveNote{}, stretched, range).error().code ==
          ErrorCode::not_implemented);
    const auto fractional = TimeMap::make({span(SpanKind::insert, 0.0, 0.0, 0.0, 1.5 / 48000.0),
                                           span(SpanKind::warp, 0.0, 1.0, 1.5 / 48000.0, 1.0 + 1.5 / 48000.0)})
                                .value();
    CHECK(engine.render_note(source, note, EffectiveNote{}, fractional, range).error().code ==
          ErrorCode::not_implemented);
    const OutputRange beyond = OutputRange::from_start(OutputTime{}, Seconds{2.0});
    CHECK(engine.render_note(source, note, EffectiveNote{}, TimeMap::identity(Seconds{1.0}), beyond)
              .error()
              .code == ErrorCode::out_of_range);
    CHECK(engine.render_note(source, note, EffectiveNote{}, TimeMap::identity(Seconds{3.0}), range)
              .error()
              .code == ErrorCode::invalid_argument);  // the map does not fit the source
}
