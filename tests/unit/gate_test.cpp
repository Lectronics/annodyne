#include "core/render/gate.h"
#include "support/test_data.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

// Rate 1000 Hz: one sample is one millisecond, so every expected value is exact arithmetic.
constexpr double k_rate = 1000.0;
using K = GapRegionKind;

OutputRange range(double a, double b) {
    return OutputRange::make(out(a), out(b)).value();
}

GapRegion region(K kind, double a, double b) {
    return {.kind = kind, .start = src(a), .end = src(b)};
}

/// A gap [start, end] with the given regions and neighbours ("a"/"b" notes; null = recording edge).
Gap gap_of(double start, double end, std::vector<GapRegion> regions, bool has_before = true,
           bool has_after = true) {
    Gap g;
    g.before = has_before ? std::optional<NoteId>{NoteId{"a"}} : std::nullopt;
    g.after = has_after ? std::optional<NoteId>{NoteId{"b"}} : std::nullopt;
    g.id = gap_segment_id(g.before, g.after);
    g.start = src(start);
    g.end = src(end);
    g.regions = std::move(regions);
    return g;
}

EffectiveGap edit_of(const Gap& g, double db) {
    return {.gap = g.id, .gain = Decibels{db}, .is_edited = db != 0.0};
}

GateParams fade_ms(double ms) {
    return {.gate_fade = Seconds{ms / 1000.0}};
}

std::vector<float> curve(const Gap& g, double db, const TimeMap& map, double a, double b,
                         double fade = 5.0) {
    return gate_curve(g, edit_of(g, db), map, Hz{k_rate}, range(a, b), fade_ms(fade)).value();
}

// gap [1,3]: tail [1,1.1], silence [1.1,2.9], preroll [2.9,3]
Gap standard_gap() {
    return gap_of(1.0, 3.0,
                  {region(K::tail, 1.0, 1.1), region(K::silence, 1.1, 2.9),
                   region(K::protected_preroll, 2.9, 3.0)});
}

}  // namespace

TEST_CASE("a_mute_ramps_in_over_the_fade_and_is_zero_inside", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{4.0});
    const auto c = curve(standard_gap(), k_gap_mute_db.value(), map, 0.0, 4.0);
    REQUIRE(c.size() == 4000);
    CHECK(c[1099] == 1.0F);  // the tail's last sample
    CHECK(c[1100] == 1.0F);  // the boundary sample
    CHECK(c[1101] == Catch::Approx(0.8).margin(1e-6));
    CHECK(c[1102] == Catch::Approx(0.6).margin(1e-6));
    CHECK(c[1104] == Catch::Approx(0.2).margin(1e-6));
    CHECK(c[1105] == 0.0F);  // exactly zero from fade onwards
    CHECK(c[2000] == 0.0F);
    CHECK(c[2894] == 0.0F);
    CHECK(c[2895] == Catch::Approx(0.2).margin(1e-6));
    CHECK(c[2899] == 1.0F);  // the region's last sample
    CHECK(c[2900] == 1.0F);  // the preroll: never gated
    CHECK(c[0] == 1.0F);
    CHECK(c[3999] == 1.0F);
}

TEST_CASE("a_cut_gain_holds_a_plateau_between_the_ramps", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{4.0});
    const auto c = curve(standard_gap(), -6.0, map, 0.0, 4.0);
    const double g = std::pow(10.0, -6.0 / 20.0);
    CHECK(c[2000] == Catch::Approx(g).margin(1e-6));
    CHECK(c[1105] == Catch::Approx(g).margin(1e-6));
    CHECK(c[1101] == Catch::Approx(1.0 + (g - 1.0) / 5.0).margin(1e-6));
    CHECK(c[1100] == 1.0F);
}

TEST_CASE("a_gain_of_zero_db_or_an_unedited_gap_is_all_ones", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{4.0});
    const Gap g = standard_gap();
    const EffectiveGap neutral{.gap = g.id, .gain = Decibels{0.0}, .is_edited = false};
    const auto c = gate_curve(g, neutral, map, Hz{k_rate}, range(0.0, 4.0), fade_ms(5.0)).value();
    CHECK(std::ranges::all_of(c, [](float v) { return v == 1.0F; }));
}

TEST_CASE("only_silence_regions_are_gated_whatever_the_other_kinds", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{6.0});
    const Gap g = gap_of(1.0, 5.0,
                         {region(K::tail, 1.0, 1.2), region(K::silence, 1.2, 2.0),
                          region(K::sibilant, 2.0, 2.3), region(K::silence, 2.3, 3.0),
                          region(K::breath, 3.0, 3.2), region(K::plosive, 3.2, 3.3),
                          region(K::audible, 3.3, 3.6), region(K::silence, 3.6, 4.9),
                          region(K::protected_preroll, 4.9, 5.0)});
    const auto c = curve(g, k_gap_mute_db.value(), map, 0.0, 6.0);
    const std::vector<std::pair<int, int>> ungated{
        {1000, 1200}, {2000, 2300}, {3000, 3600}, {4900, 5000}};
    for (const auto& [a, b] : ungated) {
        for (int i = a; i < b; ++i) {
            INFO("sample " << i);
            CHECK(c[static_cast<std::size_t>(i)] == 1.0F);
        }
    }
    CHECK(c[1600] == 0.0F);
    CHECK(c[2700] == 0.0F);
    CHECK(c[4200] == 0.0F);
    CHECK(c[2300] == 1.0F);  // the boundary sample of a silence region after the sibilant
}

TEST_CASE("a_short_region_ramps_over_half_its_length_each_side", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{4.0});
    const Gap g = gap_of(1.0, 3.0,
                         {region(K::tail, 1.0, 1.1), region(K::silence, 1.1, 1.106),
                          region(K::audible, 1.106, 3.0)});
    const auto c = curve(g, k_gap_mute_db.value(), map, 1.0, 1.2);
    CHECK(c[100] == 1.0F);
    CHECK(c[101] == Catch::Approx(2.0 / 3.0).margin(1e-6));
    CHECK(c[102] == Catch::Approx(1.0 / 3.0).margin(1e-6));
    CHECK(c[103] == Catch::Approx(1.0 / 3.0).margin(1e-6));
    CHECK(c[104] == Catch::Approx(2.0 / 3.0).margin(1e-6));
    CHECK(c[105] == 1.0F);
    CHECK(c[106] == 1.0F);
}

TEST_CASE("the_recordings_start_and_end_are_not_ramped", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{4.0});
    const Gap lead =
        gap_of(0.0, 1.0, {region(K::silence, 0.0, 0.985), region(K::protected_preroll, 0.985, 1.0)},
               false, true);
    const auto a = curve(lead, k_gap_mute_db.value(), map, 0.0, 2.0);
    CHECK(a[0] == 0.0F);    // no ramp at the start of the recording
    CHECK(a[984] == 1.0F);  // ramp toward the preroll: the boundary sample
    const Gap tail_gap =
        gap_of(3.0, 4.0, {region(K::tail, 3.0, 3.1), region(K::silence, 3.1, 4.0)}, true, false);
    const auto b = curve(tail_gap, k_gap_mute_db.value(), map, 3.0, 4.0);
    CHECK(b.back() == 0.0F);  // no ramp at the end of the recording
    CHECK(b[100] == 1.0F);
}

TEST_CASE("the_curve_follows_a_shifted_map", "[gate]") {
    // 100 ms of silence inserted at the start: everything plays 100 ms later.
    const TimeMap map =
        TimeMap::from_anchors({{.source = src(0.0), .output = out(0.1)}}, Seconds{4.0}).value();
    const auto c = curve(standard_gap(), k_gap_mute_db.value(), map, 0.0, 4.1);
    CHECK(c[1100] == 1.0F);  // 1.1 + 0.1 = 1.2: still the tail
    CHECK(c[1200] == 1.0F);  // the shifted region starts at sample 1200
    CHECK(c[1205] == 0.0F);
    CHECK(c[2500] == 0.0F);
    CHECK(c[2994] == 0.0F);
    CHECK(c[2999] == 1.0F);  // the region's last sample (2.9 + 0.1 = 3.0 is its end)
    CHECK(c[3000] == 1.0F);  // the preroll
}

TEST_CASE("the_curve_follows_a_stretched_map_and_its_blocks_are_consistent", "[gate]") {
    // Source [0,2] plays over [0,4] (slope 2), then at slope 1. Region [1.1,2.9] -> output
    // [2.2,4.9].
    const TimeMap map =
        TimeMap::from_anchors({{.source = src(2.0), .output = out(4.0)}}, Seconds{4.0}).value();
    const Gap g = standard_gap();
    const auto full = curve(g, k_gap_mute_db.value(), map, 0.0, 6.0);
    CHECK(full[2199] == 1.0F);
    CHECK(full[2200] == 1.0F);
    CHECK(full[2205] == 0.0F);
    CHECK(full[3500] == 0.0F);
    CHECK(full[4899] == 1.0F);
    // A block equals the same samples of the whole range.
    const auto block = curve(g, k_gap_mute_db.value(), map, 2.19, 2.25);
    for (std::size_t i = 0; i < block.size(); ++i) {
        CHECK(block[i] == full[2190 + i]);
    }
}

TEST_CASE("errors_are_named", "[gate]") {
    const TimeMap map = TimeMap::identity(Seconds{4.0});
    const Gap g = standard_gap();
    const EffectiveGap eff = edit_of(g, -6.0);
    const auto code = [&](const EffectiveGap& e, const TimeMap& m, double rate, OutputRange r,
                          GateParams p) {
        return gate_curve(g, e, m, Hz{rate}, r, p).error().code;
    };
    CHECK(code(eff, map, k_rate, range(0.0, 4.0), fade_ms(-1.0)) == ErrorCode::invalid_argument);
    CHECK(code(eff, map, k_rate, range(0.0, 4.0), {.gate_fade = Seconds{std::nan("")}}) ==
          ErrorCode::invalid_argument);
    CHECK(code(eff, map, 0.0, range(0.0, 4.0), fade_ms(5.0)) == ErrorCode::invalid_argument);
    EffectiveGap other = eff;
    other.gap = gap_segment_id(std::nullopt, std::nullopt);
    CHECK(code(other, map, k_rate, range(0.0, 4.0), fade_ms(5.0)) == ErrorCode::invalid_argument);
    CHECK(code(eff, map, k_rate, range(0.0, 5.0), fade_ms(5.0)) == ErrorCode::out_of_range);

    // A cut where the silence region starts: its start maps into a cut.
    const TimeMap cut = TimeMap::make({{.kind = SpanKind::warp,
                                        .source_start = src(0.0),
                                        .source_end = src(1.1),
                                        .output_start = out(0.0),
                                        .output_end = out(1.1),
                                        .segment = std::nullopt},
                                       {.kind = SpanKind::cut,
                                        .source_start = src(1.1),
                                        .source_end = src(1.5),
                                        .output_start = out(1.1),
                                        .output_end = out(1.1),
                                        .segment = std::nullopt},
                                       {.kind = SpanKind::warp,
                                        .source_start = src(1.5),
                                        .source_end = src(4.0),
                                        .output_start = out(1.1),
                                        .output_end = out(3.6),
                                        .segment = std::nullopt}})
                            .value();
    CHECK(code(eff, cut, k_rate, range(0.0, 3.6), fade_ms(5.0)) == ErrorCode::invalid_argument);
}
