#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/audio_checks.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

TEST_CASE("passthrough_returns_the_source_samples_unchanged", "[passthrough_engine]") {
    const AudioSource source = AudioSource::from_buffer(make_signal(2, 1.0));
    const PassthroughEngine engine;
    const Note note = make_note("a", 0.2, 0.4);
    const OutputRange range =
        OutputRange::from_start(OutputTime::unchecked_from_origin(Seconds{0.1}), Seconds{0.5});
    const auto audio =
        engine.render_note(source, note, EffectiveNote{}, TimeMap::identity(), range);
    REQUIRE(audio.has_value());
    const AudioBuffer expected = source.read(Samples{4800}, Samples{24000}).value();
    CHECK(bit_identical(audio->audio, expected));
    CHECK(output_range(*audio) == range);
    CHECK(engine.name() == "passthrough");
    CHECK(engine.settings_key().empty());
}

TEST_CASE("passthrough_refuses_a_stretched_time_map_and_a_range_outside_the_source",
          "[passthrough_engine]") {
    const AudioSource source = AudioSource::from_buffer(make_signal(1, 1.0));
    const PassthroughEngine engine;
    const Note note = make_note("a", 0.2, 0.4);
    const OutputRange range = OutputRange::from_start(OutputTime{}, Seconds{0.5});
    const auto shifted = TimeMap::from_anchors({{.source = src(0.0), .output = out(0.1)}}).value();
    CHECK(engine.render_note(source, note, EffectiveNote{}, shifted, range).error().code ==
          ErrorCode::not_implemented);
    const OutputRange beyond = OutputRange::from_start(OutputTime{}, Seconds{2.0});
    CHECK(engine.render_note(source, note, EffectiveNote{}, TimeMap::identity(), beyond)
              .error()
              .code == ErrorCode::out_of_range);
}
