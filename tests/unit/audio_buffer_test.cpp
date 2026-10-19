#include "core/types/audio_buffer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace melo;
using namespace melo::unit_literals;

TEST_CASE("create_makes_a_zero_filled_planar_buffer", "[audio_buffer]") {
    auto created = AudioBuffer::create(2, Samples{100}, Hz{48000.0});
    REQUIRE(created.has_value());
    const AudioBuffer& buf = *created;
    CHECK(buf.channel_count() == 2);
    CHECK(buf.frame_count() == Samples{100});
    CHECK(buf.sample_rate() == Hz{48000.0});
    for (int ch = 0; ch < 2; ++ch) {
        const auto samples = buf.channel(ch);
        CHECK(samples.size() == 100);
        CHECK(std::ranges::all_of(samples, [](float s) { return s == 0.0F; }));
    }
}

TEST_CASE("channels_are_independent", "[audio_buffer]") {
    auto buf = AudioBuffer::create(2, Samples{4}, Hz{8000.0}).value();
    buf.channel(1)[2] = 0.5F;
    CHECK(buf.channel(0)[2] == 0.0F);
    CHECK(buf.channel(1)[2] == 0.5F);
}

TEST_CASE("duration_follows_frames_and_rate", "[audio_buffer]") {
    const auto buf = AudioBuffer::create(1, Samples{24000}, Hz{48000.0}).value();
    CHECK(buf.duration() == 0.5_s);
}

TEST_CASE("empty_buffers_are_allowed", "[audio_buffer]") {
    const auto buf = AudioBuffer::create(1, Samples{0}, Hz{48000.0});
    REQUIRE(buf.has_value());
    CHECK(buf->channel(0).empty());
}

TEST_CASE("create_rejects_invalid_arguments", "[audio_buffer]") {
    CHECK(AudioBuffer::create(0, Samples{1}, Hz{48000.0}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(AudioBuffer::create(1, Samples{-1}, Hz{48000.0}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(AudioBuffer::create(1, Samples{1}, Hz{0.0}).error().code == ErrorCode::invalid_argument);
    CHECK(AudioBuffer::create(1, Samples{1}, Hz{-44100.0}).error().code ==
          ErrorCode::invalid_argument);
    CHECK(AudioBuffer::create(1, Samples{1}, Hz{std::numeric_limits<double>::quiet_NaN()})
              .error()
              .code == ErrorCode::invalid_argument);
    CHECK(AudioBuffer::create(1, Samples{1}, Hz{std::numeric_limits<double>::infinity()})
              .error()
              .code == ErrorCode::invalid_argument);
}

TEST_CASE("create_rejects_sizes_that_overflow", "[audio_buffer]") {
    const Samples huge{std::numeric_limits<std::int64_t>::max()};
    CHECK(AudioBuffer::create(2, huge, Hz{48000.0}).error().code == ErrorCode::out_of_range);
    CHECK(AudioBuffer::create(1, huge, Hz{48000.0}).error().code == ErrorCode::out_of_range);
}

TEST_CASE("positioned_audio_reports_its_range", "[audio_buffer]") {
    OutputAudio out{.audio = AudioBuffer::create(1, Samples{48000}, Hz{48000.0}).value(),
                    .start = OutputTime{} + 2.0_s};
    const OutputRange range = output_range(out);
    CHECK(range.start() == OutputTime{} + 2.0_s);
    CHECK(range.end() == OutputTime{} + 3.0_s);

    SourceAudio src{.audio = AudioBuffer::create(1, Samples{24000}, Hz{48000.0}).value(),
                    .start = SourceTime{}};
    CHECK(source_range(src).duration() == 0.5_s);
}
