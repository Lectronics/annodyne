#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/render_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>

// Timing edits that stretch render through the TimeMap (ADR-0023): the unedited base is the source
// through the map, slope-1 parts byte for byte and slope != 1 parts by stretch_span.

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
constexpr double k_rate = 48000.0;

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

EditSpec length_edit(const char* id, double ratio) {
    return make_spec(id, EditCategory::time, EditParam::length_scale, EditOp::set, Ratio{ratio});
}

/// Mono, 48 kHz, 3 s; a = 0.5..1.0 (attack to 0.55), b = 1.5..2.0.
Fixture mono_fixture(const std::vector<Note>& notes) {
    return make_fixture(notes, 3.0, 1, k_rate);
}

Fixture mono_fixture() {
    return mono_fixture({make_note("a", 0.5, 1.0), make_note("b", 1.5, 2.0)});
}

AudioBuffer source_audio(const Fixture& f) {
    return f.source.read(Samples{0}, f.source.frame_count()).value();
}

bool same_bits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// Output frames of a result (the whole recording's worth of the map).
std::shared_ptr<const RenderResult> render_all(const Fixture& f, const SynthesisEngine& engine,
                                               RenderParams params = {}) {
    // the output is longer than the source after a lengthening: ask for what the map says
    const auto probe = render_region(
        f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{0.01}), engine, params);
    REQUIRE(probe.has_value());
    return render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{3.0 + 0.225}),
                         engine, params)
        .value();
}

}  // namespace

TEST_CASE("a_length_edit_on_a_mono_source_renders_and_shifts_the_tail", "[render_stretch]") {
    const Fixture f = mono_fixture().with_edits(edits_of({length_edit("a", 1.5)}));
    const auto r = render_all(f, PassthroughEngine{});
    const AudioBuffer src = source_audio(f);
    const AudioBuffer& out = r->buffer.audio;
    // a's body (0.55..1.0, 0.45 s) becomes 0.675 s: the output is 0.225 s longer
    CHECK(out.frame_count().value() == 3 * 48000 + 10800);
    CHECK(r->stretched);
    CHECK(r->repaired_notes == 0);
    CHECK(r->notes_rendered.empty());  // a timing-only note is part of the base
    // before the stretch (source 0 .. 0.55): bit-identical
    for (const std::int64_t n : {0, 1000, 24000, 26000}) {
        CHECK(same_bits(out.channel(0)[static_cast<std::size_t>(n)],
                        src.channel(0)[static_cast<std::size_t>(n)]));
    }
    // after it: the source shifted by a whole 10800 samples, byte for byte
    for (const std::int64_t n : {48000 + 10800 + 5, 72000 + 10800, 140000}) {
        CHECK(same_bits(out.channel(0)[static_cast<std::size_t>(n)],
                        src.channel(0)[static_cast<std::size_t>(n - 10800)]));
    }
}

TEST_CASE("the_stretched_body_has_the_level_of_the_source", "[render_stretch]") {
    const Fixture f = mono_fixture().with_edits(edits_of({length_edit("a", 1.5)}));
    const auto r = render_all(f, PassthroughEngine{});
    const AudioBuffer src = source_audio(f);
    const auto rms = [](std::span<const float> x) {
        double sum = 0.0;
        for (const float v : x) {
            sum += static_cast<double>(v) * v;
        }
        return std::sqrt(sum / static_cast<double>(x.size()));
    };
    const auto out_body = r->buffer.audio.channel(0).subspan(30000, 40000);  // 0.625 .. 1.46 s
    const auto src_body = src.channel(0).subspan(27000, 20000);              // 0.5625 .. 0.98 s
    CHECK(std::abs(20.0 * std::log10(rms(out_body) / rms(src_body))) < 1.0);
}

TEST_CASE("an_edited_note_in_a_slope_1_part_still_renders_next_to_a_stretch", "[render_stretch]") {
    const Fixture f =
        mono_fixture().with_edits(edits_of({length_edit("a", 1.5), gain_edit("b", 6.0)}));
    const auto r = render_all(f, GainTestEngine{});
    const AudioBuffer src = source_audio(f);
    REQUIRE(r->notes_rendered.size() == 1);
    CHECK(r->notes_rendered[0].value == "b");
    const float gain = static_cast<float>(to_linear_gain(Decibels{6.0}).value());
    // b's body, shifted by 10800 samples and amplified
    const std::int64_t n = 90000;  // inside b (1.5 .. 2.0 s source)
    CHECK(same_bits(r->buffer.audio.channel(0)[static_cast<std::size_t>(n + 10800)],
                    src.channel(0)[static_cast<std::size_t>(n)] * gain));
}

TEST_CASE("an_edit_on_a_stretched_note_is_not_implemented_and_names_it", "[render_stretch]") {
    const Fixture f =
        mono_fixture().with_edits(edits_of({length_edit("a", 1.5), gain_edit("a", 6.0)}));
    const auto r = render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{1.0}),
                                 GainTestEngine{}, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::not_implemented);
    CHECK(r.error().message.find("'a'") != std::string::npos);
}

TEST_CASE("an_edited_note_whose_margin_meets_a_stretched_neighbour_is_not_implemented",
          "[render_stretch]") {
    // a ends where b starts; b's attack (1.0 .. 1.05) is stretched, a's 10 ms margin meets it
    const Fixture f =
        mono_fixture({make_note("a", 0.5, 1.0), make_note("b", 1.0, 2.0)})
            .with_edits(edits_of({make_spec("b", EditCategory::attack, EditParam::attack_time_scale,
                                            EditOp::set, Ratio{2.0}),
                                  gain_edit("a", 6.0)}));
    const auto r = render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{1.5}),
                                 GainTestEngine{}, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::not_implemented);
    CHECK(r.error().message.find("'a'") != std::string::npos);
}

TEST_CASE("a_stereo_source_with_a_stretch_is_not_implemented", "[render_stretch]") {
    const Fixture f = make_fixture({make_note("a", 0.5, 1.0)}, 3.0, 2, k_rate)
                          .with_edits(edits_of({length_edit("a", 1.5)}));
    const auto r = render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{1.0}),
                                 PassthroughEngine{}, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::not_implemented);
}

TEST_CASE("the_cache_key_follows_the_stretch_settings_and_a_second_render_hits",
          "[render_stretch]") {
    const Fixture f = mono_fixture().with_edits(edits_of({length_edit("a", 1.5)}));
    RenderCache cache{64'000'000};
    RenderParams params;
    params.cache = &cache;
    const OutputRange range = OutputRange::from_start(OutputTime{}, Seconds{1.0});
    const auto a = render_region(f.inputs(), range, PassthroughEngine{}, params).value();
    const auto again = render_region(f.inputs(), range, PassthroughEngine{}, params).value();
    CHECK(a.get() == again.get());
    params.stretch.noise_seed = 99;
    const auto other = render_region(f.inputs(), range, PassthroughEngine{}, params).value();
    CHECK(other.get() != a.get());
    CHECK(other->cache_id != a->cache_id);
    params.stretch.noise_seed = StretchParams{}.noise_seed;
    params.stretch.vibrato = VibratoMode::retime;
    const auto retimed = render_region(f.inputs(), range, PassthroughEngine{}, params).value();
    CHECK(retimed->cache_id != a->cache_id);
}

TEST_CASE("a_region_before_the_stretch_is_not_marked_stretched", "[render_stretch]") {
    const Fixture f = mono_fixture().with_edits(edits_of({length_edit("a", 1.5)}));
    const auto r = render_region(f.inputs(), OutputRange::from_start(OutputTime{}, Seconds{0.4}),
                                 PassthroughEngine{}, {})
                       .value();
    CHECK_FALSE(r->stretched);
    CHECK(r->passthrough);
}
