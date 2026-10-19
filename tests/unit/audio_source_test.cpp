#include "core/types/audio_source.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

using namespace melo;

namespace {

AudioBuffer ramp(int channels, std::int64_t frames) {
    auto buf = AudioBuffer::create(channels, Samples{frames}, Hz{48000.0}).value();
    for (int ch = 0; ch < channels; ++ch) {
        auto samples = buf.channel(ch);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<float>(ch * 1000 + static_cast<int>(i)) / 4096.0F;
        }
    }
    return buf;
}

}  // namespace

// The source is never modified (CLAUDE.md rule 1): no default constructor, no mutable access.
static_assert(!std::is_default_constructible_v<AudioSource>);
static_assert(
    std::is_same_v<decltype(std::declval<const AudioSource&>().read(Samples{}, Samples{})),
                   Result<AudioBuffer>>);

TEST_CASE("audio_source_exposes_the_buffer_metadata", "[audio_source]") {
    const AudioSource src = AudioSource::from_buffer(ramp(2, 100));
    CHECK(src.channel_count() == 2);
    CHECK(src.frame_count() == Samples{100});
    CHECK(src.sample_rate() == Hz{48000.0});
    CHECK_FALSE(src.id().value.empty());
}

TEST_CASE("read_copies_the_requested_frames", "[audio_source]") {
    const AudioSource src = AudioSource::from_buffer(ramp(2, 100));
    const auto part = src.read(Samples{10}, Samples{5});
    REQUIRE(part.has_value());
    CHECK(part->channel_count() == 2);
    CHECK(part->frame_count() == Samples{5});
    CHECK(part->sample_rate() == Hz{48000.0});
    for (int ch = 0; ch < 2; ++ch) {
        for (std::size_t i = 0; i < 5; ++i) {
            const float expected =
                static_cast<float>(ch * 1000 + 10 + static_cast<int>(i)) / 4096.0F;
            CHECK(part->channel(ch)[i] == expected);  // a copy, so bit-exact
        }
    }
}

TEST_CASE("read_accepts_the_full_range_and_empty_reads", "[audio_source]") {
    const AudioSource src = AudioSource::from_buffer(ramp(1, 100));
    CHECK(src.read(Samples{0}, Samples{100}).has_value());
    CHECK(src.read(Samples{100}, Samples{0}).has_value());
}

TEST_CASE("read_rejects_out_of_range_requests", "[audio_source]") {
    const AudioSource src = AudioSource::from_buffer(ramp(1, 100));
    CHECK(src.read(Samples{-1}, Samples{1}).error().code == ErrorCode::out_of_range);
    CHECK(src.read(Samples{0}, Samples{-1}).error().code == ErrorCode::out_of_range);
    CHECK(src.read(Samples{95}, Samples{6}).error().code == ErrorCode::out_of_range);
    CHECK(src.read(Samples{101}, Samples{0}).error().code == ErrorCode::out_of_range);
    const Samples huge{std::numeric_limits<std::int64_t>::max()};
    CHECK(src.read(Samples{1}, huge).error().code == ErrorCode::out_of_range);
}

TEST_CASE("modifying_a_read_does_not_change_the_source", "[audio_source]") {
    const AudioSource src = AudioSource::from_buffer(ramp(1, 10));
    auto first = src.read(Samples{0}, Samples{10}).value();
    first.channel(0)[3] = 99.0F;
    CHECK(src.read(Samples{3}, Samples{1}).value().channel(0)[0] == 3.0F / 4096.0F);
}

TEST_CASE("ids_follow_content", "[audio_source]") {
    const AudioSource a = AudioSource::from_buffer(ramp(2, 100));
    const AudioSource b = AudioSource::from_buffer(ramp(2, 100));
    const AudioSource c = AudioSource::from_buffer(ramp(2, 101));
    auto changed = ramp(2, 100);
    changed.channel(1)[50] += 1.0F;
    const AudioSource d = AudioSource::from_buffer(std::move(changed));
    auto other_rate = AudioBuffer::create(2, Samples{100}, Hz{44100.0}).value();
    const AudioSource e = AudioSource::from_buffer(std::move(other_rate));
    auto silent = AudioBuffer::create(2, Samples{100}, Hz{48000.0}).value();
    const AudioSource f = AudioSource::from_buffer(std::move(silent));

    CHECK(a.id() == b.id());
    CHECK(a.id() != c.id());
    CHECK(a.id() != d.id());
    CHECK(e.id() != f.id());  // same zero samples, different rate
}

TEST_CASE("copies_share_identity_and_moved_from_sources_stay_valid", "[audio_source]") {
    AudioSource original = AudioSource::from_buffer(ramp(1, 10));
    const AudioSource copy = original;  // NOLINT(performance-unnecessary-copy-initialization)
    CHECK(copy.id() == original.id());

    // NOLINTNEXTLINE(performance-move-const-arg): checking what a move does is the point
    const AudioSource moved_to = std::move(original);
    CHECK(moved_to.id() == copy.id());
    // AudioSource has no empty state: a "moved-from" source is still a full copy.
    CHECK(original.frame_count() == Samples{10});               // NOLINT(bugprone-use-after-move)
    CHECK(original.read(Samples{0}, Samples{10}).has_value());  // NOLINT(bugprone-use-after-move)
}
