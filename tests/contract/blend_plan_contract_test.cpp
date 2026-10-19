#include "core/render/blend_plan.h"
#include "support/random_scenarios.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

TEST_CASE("blend_plans_are_ordered_consistent_and_inside_both_zones", "[blend_plan_contract]") {
    for (int seed = 0; seed < k_property_cases; ++seed) {
        INFO("seed " << seed);
        Rng rng{static_cast<std::uint64_t>(seed)};
        const std::int64_t margin = uniform_int(rng, 0, 4) == 0 ? 0 : uniform_int(rng, 1, 1000);
        std::vector<SampleSpan> spans;
        std::int64_t cursor = uniform_int(rng, 0, 2000);
        for (int i = uniform_int(rng, 1, 6); i > 0; --i) {
            const std::int64_t length = uniform_int(rng, 0, 3) == 0 ? 0 : uniform_int(rng, 1, 3000);
            spans.push_back({cursor, cursor + length});
            const int kind = uniform_int(rng, 0, 3);
            cursor += length + (kind == 0 ? 0 : (kind == 1 ? margin : uniform_int(rng, 0, 4000)));
        }
        const auto ramps = plan_blend(spans, margin);
        REQUIRE_FALSE(ramps.empty());
        CHECK(ramps.front().from == k_pristine);
        CHECK(ramps.back().to == k_pristine);
        const auto in_zone = [&](int note, const Ramp& r) {
            if (note == k_pristine) {
                return true;
            }
            const SampleSpan& s = spans[static_cast<std::size_t>(note)];
            return r.begin >= s.begin - margin && r.end <= s.end + margin;
        };
        for (std::size_t j = 0; j < ramps.size(); ++j) {
            CHECK(ramps[j].begin <= ramps[j].end);
            CHECK(ramps[j].end - ramps[j].begin <= 2 * margin);
            CHECK(in_zone(ramps[j].from, ramps[j]));
            CHECK(in_zone(ramps[j].to, ramps[j]));
            if (j + 1 < ramps.size()) {
                CHECK(ramps[j].end <= ramps[j + 1].begin);
                CHECK(ramps[j].to == ramps[j + 1].from);
            }
        }
    }
}

TEST_CASE("plans_are_deterministic", "[blend_plan_contract]") {
    const std::vector<SampleSpan> spans{{100, 300}, {300, 350}, {500, 900}};
    CHECK(plan_blend(spans, 120) == plan_blend(spans, 120));
}
