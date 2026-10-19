#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/random_scenarios.h"
#include "support/render_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
constexpr double k_rate = 8000.0;  // keeps 300 cases quick; the maths is rate-independent

struct Scenario {
    Fixture fixture;
    std::vector<Note> notes;
};

Scenario random_scenario(Rng& rng) {
    const auto notes = random_notes(rng, uniform_int(rng, 1, 5));
    const double end = notes.back().source_end.unchecked_since_origin().value() + 0.5;
    return {make_fixture(notes, end, 2, k_rate), notes};
}

/// Adds 1..4 random gain groups on random notes; returns every intermediate list (index 0 = empty).
std::vector<EditList> random_history(Rng& rng, const std::vector<Note>& notes) {
    std::vector<EditList> history{EditList{TrackId{"t1"}}};
    for (int i = uniform_int(rng, 1, 4); i > 0; --i) {
        const Note& n = notes[static_cast<std::size_t>(
            uniform_int(rng, 0, static_cast<int>(notes.size()) - 1))];
        history.push_back(
            history.back()
                .add(std::vector{gain_edit(n.id.value, uniform(rng, -12.0, 12.0))}, k_t0)
                .value());
    }
    return history;
}

AudioBuffer source_audio(const Fixture& f) {
    return f.source.read(Samples{0}, f.source.frame_count()).value();
}

}  // namespace

TEST_CASE("an_empty_edit_list_renders_every_region_of_the_source_exactly", "[render_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const auto sc = random_scenario(rng);
        const std::int64_t total = sc.fixture.source.frame_count().value();
        const std::int64_t a = uniform_int(rng, 0, static_cast<int>(total));
        const std::int64_t b = uniform_int(rng, static_cast<int>(a), static_cast<int>(total));
        const OutputRange region =
            OutputRange::from_start(to_time_point<OutputAxis>(Samples{a}, Hz{k_rate}),
                                    to_seconds(Samples{b - a}, Hz{k_rate}));
        const auto r = render_region(sc.fixture.inputs(), region, GainTestEngine{}, {}).value();
        const AudioBuffer expected = sc.fixture.source.read(Samples{a}, Samples{b - a}).value();
        CHECK(bit_identical(audio_of(r), expected));
        CHECK(r->passthrough);
    }
}

TEST_CASE("edits_followed_by_full_undo_render_the_source_exactly", "[render_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        auto sc = random_scenario(rng);
        EditList list = random_history(rng, sc.notes).back();
        while (list.can_undo()) {
            list = list.undo().value();
        }
        const Fixture f = sc.fixture.with_edits(list);
        const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
        CHECK(bit_identical(audio_of(r), source_audio(f)));
    }
}

TEST_CASE("samples_outside_the_dirty_regions_are_bit_identical", "[render_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        auto sc = random_scenario(rng);
        const EditList list = random_history(rng, sc.notes).back();
        const Fixture f = sc.fixture.with_edits(list);
        const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{},
                                     {.crossfade_length = k_max_crossfade})
                           .value();
        const auto dirty =
            dirty_regions(
                sc.notes, EditList{TrackId{"t1"}}, list,
                DirtyParams{.source_end = SourceTime::unchecked_from_origin(Seconds{1.0e9})})
                .value();
        const AudioBuffer original = source_audio(f);
        const auto frames = original.frame_count().value();
        std::vector<bool> allowed(static_cast<std::size_t>(frames), false);
        for (const SourceRange& range : dirty) {
            const auto lo = std::max<std::int64_t>(
                0, to_sample_index(range.start(), Hz{k_rate}, Rounding::down).value());
            const auto hi = std::min<std::int64_t>(
                frames, to_sample_index(range.end(), Hz{k_rate}, Rounding::up).value());
            for (std::int64_t i = lo; i < hi; ++i) {
                allowed[static_cast<std::size_t>(i)] = true;
            }
        }
        std::int64_t leaks = 0;
        for (std::int64_t i = 0; i < frames; ++i) {
            if (!allowed[static_cast<std::size_t>(i)]) {
                for (int c = 0; c < 2; ++c) {
                    const auto k = static_cast<std::size_t>(i);
                    leaks += audio_of(r).channel(c)[k] == original.channel(c)[k] ? 0 : 1;
                }
            }
        }
        CHECK(leaks == 0);
    }
}

TEST_CASE("the_passthrough_engine_renders_the_source_whatever_the_edits", "[render_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        auto sc = random_scenario(rng);
        const EditList list = random_history(rng, sc.notes).back();
        const Fixture f = sc.fixture.with_edits(list);
        const auto r = render_region(f.inputs(), whole(f.source), PassthroughEngine{}, {}).value();
        CHECK(bit_identical(audio_of(r), source_audio(f)));
    }
}

TEST_CASE("two_aligned_halves_equal_the_whole_render", "[render_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        auto sc = random_scenario(rng);
        const Fixture f = sc.fixture.with_edits(random_history(rng, sc.notes).back());
        const std::int64_t total = f.source.frame_count().value();
        const std::int64_t split = uniform_int(rng, 0, static_cast<int>(total));
        const auto piece = [&](std::int64_t a, std::int64_t b) {
            return render_region(
                       f.inputs(),
                       OutputRange::from_start(to_time_point<OutputAxis>(Samples{a}, Hz{k_rate}),
                                               to_seconds(Samples{b - a}, Hz{k_rate})),
                       GainTestEngine{}, {})
                .value();
        };
        const auto whole_r = piece(0, total);
        const auto left = piece(0, split);
        const auto right = piece(split, total);
        std::int64_t mismatches = 0;
        for (int c = 0; c < 2; ++c) {
            for (std::int64_t i = 0; i < total; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const float part =
                    i < split ? audio_of(left).channel(c)[k]
                              : audio_of(right).channel(c)[static_cast<std::size_t>(i - split)];
                mismatches += part == audio_of(whole_r).channel(c)[k] ? 0 : 1;
            }
        }
        CHECK(mismatches == 0);
    }
}

TEST_CASE("renders_are_deterministic_and_a_cache_hit_equals_a_miss", "[render_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        auto sc = random_scenario(rng);
        const Fixture f = sc.fixture.with_edits(random_history(rng, sc.notes).back());
        RenderCache cache{256'000'000};
        const auto plain = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
        const auto miss =
            render_region(f.inputs(), whole(f.source), GainTestEngine{}, {.cache = &cache}).value();
        const auto hit =
            render_region(f.inputs(), whole(f.source), GainTestEngine{}, {.cache = &cache}).value();
        CHECK(hit == miss);
        CHECK(hash_audio(audio_of(plain)) == hash_audio(audio_of(miss)));
        CHECK(plain->notes_rendered.size() == miss->notes_rendered.size());
    }
}
