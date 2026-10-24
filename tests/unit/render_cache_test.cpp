#include "core/render/render_cache.h"
#include "core/render/render_key.h"
#include "core/types/note.h"
#include "support/audio_checks.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <vector>

using namespace anno;
using namespace anno::test;

namespace {

std::shared_ptr<const RenderResult> result_of(double seconds) {
    AudioBuffer audio = make_signal(1, seconds, 1000.0);  // 1 kHz: seconds * 4000 bytes
    return std::make_shared<const RenderResult>(
        RenderResult{.buffer = OutputAudio{.audio = std::move(audio), .start = OutputTime{}},
                     .cache_id = "x",
                     .notes_rendered = {NoteId{"n"}},
                     .gaps_rendered = {},
                     .passthrough = false});
}

}  // namespace

TEST_CASE("a_miss_is_null_and_a_hit_returns_the_stored_pointer", "[render_cache]") {
    RenderCache cache{1'000'000};
    CHECK(cache.get("a") == nullptr);
    const auto value = result_of(0.1);
    cache.put("a", value);
    CHECK(cache.get("a") == value);
    CHECK(cache.entry_count() == 1);
}

TEST_CASE("the_least_recently_used_entry_is_evicted_first", "[render_cache]") {
    const auto one = result_of(0.1);
    RenderCache probe{1'000'000};
    probe.put("k1", one);
    const std::size_t each = probe.size_bytes();
    RenderCache cache{each * 2 + each / 2};  // room for two
    cache.put("k1", result_of(0.1));
    cache.put("k2", result_of(0.1));
    REQUIRE(cache.get("k1") != nullptr);  // k1 becomes most recent
    cache.put("k3", result_of(0.1));      // evicts k2
    CHECK(cache.get("k2") == nullptr);
    CHECK(cache.get("k1") != nullptr);
    CHECK(cache.get("k3") != nullptr);
    CHECK(cache.size_bytes() <= each * 2 + each / 2);
}

TEST_CASE("an_entry_larger_than_the_budget_is_dropped", "[render_cache]") {
    RenderCache cache{100};
    cache.put("big", result_of(1.0));
    CHECK(cache.entry_count() == 0);
    CHECK(cache.size_bytes() == 0);
}

TEST_CASE("replacing_a_key_does_not_leak_bytes", "[render_cache]") {
    RenderCache cache{1'000'000};
    cache.put("a", result_of(0.1));
    const std::size_t once = cache.size_bytes();
    cache.put("a", result_of(0.1));
    CHECK(cache.size_bytes() == once);
    cache.clear();
    CHECK(cache.size_bytes() == 0);
    CHECK(cache.entry_count() == 0);
}

TEST_CASE("concurrent_use_is_safe", "[render_cache]") {
    RenderCache cache{200'000};
    const auto value = result_of(0.1);
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&cache, &value, t] {
            for (int i = 0; i < 200; ++i) {
                const std::string key = "k" + std::to_string((i + t) % 7);
                cache.put(key, value);
                (void)cache.get(key);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    CHECK(cache.size_bytes() <= 200'000);
}

TEST_CASE("the_note_fingerprint_covers_the_maximum_voiced_frequency", "[render_cache]") {
    Note a;
    Note b = a;
    CHECK(note_fingerprint(a) == note_fingerprint(b));
    b.noise.max_voiced_hz = {Hz{1500.0}};
    CHECK(note_fingerprint(a) != note_fingerprint(b));  // empty (not measured) differs from a value
    Note c = a;
    c.noise.max_voiced_hz = {Hz{1600.0}};
    CHECK(note_fingerprint(b) != note_fingerprint(c));
}
