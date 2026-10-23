#include "core/render/splice.h"
#include "core/synthesis/engine/source_through_map.h"
#include "support/synth_signals.h"
#include "support/test_data.h"
#include "support/timeline_data.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr double k_rate = 44100.0;

/// A slow 100 Hz tone with a little noise: smooth enough that a hard join is a visible step.
std::vector<float> slow_signal(double seconds) {
    std::vector<float> x = make_sine(100.0, seconds, k_rate, 0.02);
    const std::vector<float> n = make_noise(seconds, k_rate, 0.0005, 3);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += n[i];
    }
    return x;
}

TimeSpan warp(double s0, double s1, double o0, double o1, std::optional<SegmentId> id = {}) {
    return {.kind = SpanKind::warp,
            .source_start = src(s0),
            .source_end = src(s1),
            .output_start = out(o0),
            .output_end = out(o1),
            .segment = std::move(id)};
}

double max_step(std::span<const float> x, std::size_t lo, std::size_t hi) {
    double worst = 0.0;
    for (std::size_t i = lo + 1; i < hi; ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(x[i] - x[i - 1])));
    }
    return worst;
}

std::int64_t n_of(double seconds) {
    return std::llround(seconds * k_rate);
}

struct Cut {
    AudioSource source;
    TimeMap map;
    AudioBuffer plain;
};

// 1 s source, 0.0123 s (a phase jump for 100 Hz) cut at 0.4.
Cut make_cut() {
    const AudioSource source = AudioSource::from_buffer(to_buffer(slow_signal(1.0), k_rate));
    const double cut_s = static_cast<double>(n_of(0.0123)) / k_rate;
    const double at = static_cast<double>(n_of(0.4)) / k_rate;
    const TimeMap map = TimeMap::make({warp(0.0, at, 0.0, at),
                                       {.kind = SpanKind::cut,
                                        .source_start = src(at),
                                        .source_end = src(at + cut_s),
                                        .output_start = out(at),
                                        .output_end = out(at),
                                        .segment = std::nullopt},
                                       warp(at + cut_s, 1.0, at, 1.0 - cut_s)})
                            .value();
    AudioBuffer plain =
        read_through_map(
            source, map, Samples{0},
            Samples{to_samples(map.output_length(), Hz{k_rate}, Rounding::nearest).value()})
            .value();
    return {.source = source, .map = map, .plain = std::move(plain)};
}

}  // namespace

TEST_CASE("a_cut_join_is_continuous_where_the_hard_join_steps", "[splice]") {
    Cut c = make_cut();
    const std::int64_t join = n_of(0.4);
    const auto lo = static_cast<std::size_t>(join - 30);
    const auto hi = static_cast<std::size_t>(join + 400);
    const double hard = max_step(c.plain.channel(0), lo, hi);
    AudioBuffer out = c.plain;
    REQUIRE(apply_splices(c.source, c.map, nullptr, 0, out, {}).has_value());
    const double soft = max_step(out.channel(0), lo, hi);
    CHECK(hard > 4.0 * soft);
    CHECK(soft < 0.01);
}

TEST_CASE("only_the_splice_fade_after_a_cut_changes", "[splice]") {
    Cut c = make_cut();
    const std::int64_t join = n_of(0.4);
    const std::int64_t fade = n_of(0.005);
    AudioBuffer out = c.plain;
    REQUIRE(apply_splices(c.source, c.map, nullptr, 0, out, {}).has_value());
    bool changed = false;
    for (std::int64_t i = 0; i < out.frame_count().value(); ++i) {
        const auto k = static_cast<std::size_t>(i);
        const bool same = out.channel(0)[k] == c.plain.channel(0)[k];
        if (i < join || i >= join + fade) {
            CHECK(same);
        }
        changed = changed || !same;
    }
    CHECK(changed);
}

TEST_CASE("any_block_split_gives_the_same_samples", "[splice]") {
    Cut c = make_cut();
    AudioBuffer whole = c.plain;
    REQUIRE(apply_splices(c.source, c.map, nullptr, 0, whole, {}).has_value());
    const std::int64_t join = n_of(0.4);
    for (const std::int64_t split :
         {join - 5, join, join + 1, join + 100, join + 220, join + 221}) {
        const std::int64_t total = c.plain.frame_count().value();
        AudioBuffer first = AudioBuffer::create(1, Samples{split}, Hz{k_rate}).value();
        AudioBuffer second = AudioBuffer::create(1, Samples{total - split}, Hz{k_rate}).value();
        std::ranges::copy(c.plain.channel(0).first(static_cast<std::size_t>(split)),
                          first.channel(0).begin());
        std::ranges::copy(c.plain.channel(0).subspan(static_cast<std::size_t>(split)),
                          second.channel(0).begin());
        REQUIRE(apply_splices(c.source, c.map, nullptr, 0, first, {}).has_value());
        REQUIRE(apply_splices(c.source, c.map, nullptr, split, second, {}).has_value());
        CHECK(std::ranges::equal(first.channel(0),
                                 whole.channel(0).first(static_cast<std::size_t>(split))));
        CHECK(std::ranges::equal(second.channel(0),
                                 whole.channel(0).subspan(static_cast<std::size_t>(split))));
    }
}

TEST_CASE("a_cut_shorter_than_the_fade_fades_over_its_own_length", "[splice]") {
    const AudioSource source = AudioSource::from_buffer(to_buffer(slow_signal(1.0), k_rate));
    const double at = static_cast<double>(n_of(0.4)) / k_rate;
    const double len = 3.0 / k_rate;  // three samples
    const TimeMap map = TimeMap::make({warp(0.0, at, 0.0, at),
                                       {.kind = SpanKind::cut,
                                        .source_start = src(at),
                                        .source_end = src(at + len),
                                        .output_start = out(at),
                                        .output_end = out(at),
                                        .segment = std::nullopt},
                                       warp(at + len, 1.0, at, 1.0 - len)})
                            .value();
    AudioBuffer plain =
        read_through_map(
            source, map, Samples{0},
            Samples{to_samples(map.output_length(), Hz{k_rate}, Rounding::nearest).value()})
            .value();
    AudioBuffer spliced = plain;
    REQUIRE(apply_splices(source, map, nullptr, 0, spliced, {}).has_value());
    const std::int64_t join = n_of(0.4);
    for (std::int64_t i = 0; i < spliced.frame_count().value(); ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (i < join || i >= join + 3) {
            CHECK(spliced.channel(0)[k] == plain.channel(0)[k]);
        }
    }
}

namespace {

struct Insert {
    AudioSource source;
    SegmentTimeline timeline;
    TimeMap map;
    AudioBuffer plain;
    std::int64_t from;
    std::int64_t to;
};

// Notes a [0.1,0.2] and b [0.8,0.9] in 1 s; 0.05 s of silence inserted in gap gA at `at`.
Insert make_insert(double at_wish) {
    const std::vector<Note> notes{make_note("a", 0.1, 0.2), make_note("b", 0.8, 0.9)};
    SegmentTimeline timeline = timeline_of(notes, 1.0);
    const SegmentId id = timeline.gaps()[1].id;
    const AudioSource source = AudioSource::from_buffer(to_buffer(slow_signal(1.0), k_rate));
    const double at = static_cast<double>(n_of(at_wish)) / k_rate;
    const double len = static_cast<double>(n_of(0.05)) / k_rate;
    TimeMap map = TimeMap::make({warp(0.0, at, 0.0, at),
                                 {.kind = SpanKind::insert,
                                  .source_start = src(at),
                                  .source_end = src(at),
                                  .output_start = out(at),
                                  .output_end = out(at + len),
                                  .segment = id},
                                 warp(at, 1.0, at + len, 1.0 + len)})
                      .value();
    AudioBuffer plain =
        read_through_map(
            source, map, Samples{0},
            Samples{to_samples(map.output_length(), Hz{k_rate}, Rounding::nearest).value()})
            .value();
    return {.source = source,
            .timeline = std::move(timeline),
            .map = std::move(map),
            .plain = std::move(plain),
            .from = n_of(at_wish),
            .to = n_of(at_wish) + n_of(0.05)};
}

}  // namespace

TEST_CASE("an_insert_fades_the_audio_to_and_from_the_zeros", "[splice]") {
    Insert ins = make_insert(0.5);
    AudioBuffer out = ins.plain;
    REQUIRE(apply_splices(ins.source, ins.map, &ins.timeline, 0, out, {}).has_value());
    const std::int64_t fade = n_of(0.005);
    float peak = 0.0F;
    for (const float v : ins.plain.channel(0)) {
        peak = std::max(peak, std::abs(v));
    }
    for (std::int64_t i = 0; i < out.frame_count().value(); ++i) {
        const auto k = static_cast<std::size_t>(i);
        const bool in_fade = (i >= ins.from - fade && i < ins.to + fade);
        if (!in_fade) {
            CHECK(out.channel(0)[k] == ins.plain.channel(0)[k]);
        }
        if (i >= ins.from && i < ins.to) {
            CHECK(out.channel(0)[k] == 0.0F);
        }
    }
    const auto before = static_cast<std::size_t>(ins.from - 1);
    const auto after = static_cast<std::size_t>(ins.to);
    CHECK(std::abs(out.channel(0)[before]) < 0.05F * peak);
    CHECK(std::abs(out.channel(0)[after]) < 0.05F * peak);
    CHECK(max_step(out.channel(0), before - 400, after + 400) <=
          1.5 * max_step(ins.plain.channel(0), 0, 400) + 0.01);
}

TEST_CASE("an_insert_never_fades_beyond_its_silence_region", "[splice]") {
    // 2 ms after the region start (the gap starts at 0.2): the fade is limited to those samples
    Insert ins = make_insert(0.202);
    AudioBuffer out = ins.plain;
    REQUIRE(apply_splices(ins.source, ins.map, &ins.timeline, 0, out, {}).has_value());
    const std::int64_t region_start = n_of(0.2);
    for (std::int64_t i = 0; i < region_start; ++i) {
        const auto k = static_cast<std::size_t>(i);
        CHECK(out.channel(0)[k] == ins.plain.channel(0)[k]);
    }
}

TEST_CASE("an_insert_without_a_timeline_keeps_its_hard_edges", "[splice]") {
    Insert ins = make_insert(0.5);
    AudioBuffer out = ins.plain;
    REQUIRE(apply_splices(ins.source, ins.map, nullptr, 0, out, {}).has_value());
    CHECK(std::ranges::equal(out.channel(0), ins.plain.channel(0)));
}

TEST_CASE("a_map_without_cuts_or_inserts_is_untouched", "[splice]") {
    const AudioSource source = AudioSource::from_buffer(to_buffer(slow_signal(0.5), k_rate));
    AudioBuffer block =
        read_through_map(source, TimeMap::identity(Seconds{0.5}), Samples{0}, Samples{n_of(0.5)})
            .value();
    const AudioBuffer copy = block;
    REQUIRE(
        apply_splices(source, TimeMap::identity(Seconds{0.5}), nullptr, 0, block, {}).has_value());
    CHECK(std::ranges::equal(block.channel(0), copy.channel(0)));
}
