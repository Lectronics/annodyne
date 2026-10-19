#include "core/dsp/crossfade.h"
#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/render_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};
constexpr double k_rate = 48000.0;
constexpr std::int64_t k_margin = 480;  // 10 ms at 48 kHz

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

std::vector<Note> two_notes() {
    return {make_note("a", 0.5, 1.0), make_note("b", 1.5, 2.0)};
}

std::shared_ptr<const RenderResult> render(const Fixture& f, const SynthesisEngine& engine,
                                           RenderParams params = {}) {
    return render_region(f.inputs(), whole(f.source), engine, params).value();
}

AudioBuffer source_audio(const Fixture& f) {
    return f.source.read(Samples{0}, f.source.frame_count()).value();
}

float gain_of(double db) {
    return static_cast<float>(to_linear_gain(Decibels{db}).value());
}

}  // namespace

TEST_CASE("an_empty_edit_list_renders_the_source_bit_exactly", "[render]") {
    const Fixture f = make_fixture(two_notes());
    const auto r = render(f, GainTestEngine{});
    CHECK(bit_identical(audio_of(r), source_audio(f)));
    CHECK(r->passthrough);
    CHECK(r->notes_rendered.empty());
}

TEST_CASE("a_gain_edit_changes_only_the_notes_blend_zone", "[render]") {
    const Fixture f = make_fixture(two_notes()).with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto r = render(f, GainTestEngine{});
    const AudioBuffer original = source_audio(f);
    const std::int64_t a0 = 24000;
    const std::int64_t a1 = 48000;
    REQUIRE(r->notes_rendered.size() == 1);
    CHECK(r->notes_rendered[0].value == "a");
    CHECK_FALSE(r->passthrough);
    const float g = gain_of(6.0);
    for (int c = 0; c < 2; ++c) {
        for (std::int64_t i = 0; i < original.frame_count().value(); ++i) {
            const auto k = static_cast<std::size_t>(i);
            const float o = original.channel(c)[k];
            const float v = audio_of(r).channel(c)[k];
            if (i < a0 - k_margin || i >= a1 + k_margin) {
                REQUIRE(v == o);  // untouched, bit for bit
            } else if (i >= a0 && i < a1) {
                REQUIRE(v == o * g);  // weight-1 interior is the engine output
            }
        }
    }
}

TEST_CASE("the_ramp_into_a_note_is_the_crossfade_kernel_applied_to_original_and_engine",
          "[render]") {
    const Fixture f = make_fixture(two_notes()).with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto r = render(f, GainTestEngine{});
    const AudioBuffer original = source_audio(f);
    const auto lo = static_cast<std::size_t>(24000 - k_margin);
    std::vector<float> engine(static_cast<std::size_t>(k_margin));
    for (std::size_t i = 0; i < engine.size(); ++i) {
        engine[i] = original.channel(0)[lo + i] * gain_of(6.0);
    }
    std::vector<float> expected(engine.size());
    crossfade(original.channel(0).subspan(lo, engine.size()), engine, expected,
              FadeShape::equal_power, 0, k_margin);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(audio_of(r).channel(0)[lo + i] == expected[i]);
    }
}

TEST_CASE("abutting_edited_notes_crossfade_into_each_other_not_through_the_original", "[render]") {
    const std::vector<Note> notes{make_note("a", 0.5, 1.0), make_note("b", 1.0, 1.5)};
    const Fixture f = make_fixture(notes)
                          .with_edits(edits_of({gain_edit("a", 6.0)}))
                          .with_edits(edits_of({gain_edit("a", 6.0), gain_edit("b", -6.0)}));
    const auto r = render(f, GainTestEngine{});
    const AudioBuffer original = source_audio(f);
    const auto lo = static_cast<std::size_t>(48000 - k_margin);
    const auto n = static_cast<std::size_t>(2 * k_margin);
    std::vector<float> ea(n);
    std::vector<float> eb(n);
    for (std::size_t i = 0; i < n; ++i) {
        ea[i] = original.channel(0)[lo + i] * gain_of(6.0);
        eb[i] = original.channel(0)[lo + i] * gain_of(-6.0);
    }
    std::vector<float> expected(n);
    crossfade(ea, eb, expected, FadeShape::equal_power, 0, 2 * k_margin);
    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(audio_of(r).channel(0)[lo + i] == expected[i]);
    }
}

TEST_CASE("edits_that_cancel_out_render_as_the_source", "[render]") {
    Fixture f = make_fixture(two_notes());
    f = f.with_edits(*f.edits->add(std::vector{gain_edit("a", 6.0)}, k_t0));
    f = f.with_edits(*f.edits->add(std::vector{gain_edit("a", -6.0)}, k_t0));
    const auto r = render(f, GainTestEngine{});
    CHECK(bit_identical(audio_of(r), source_audio(f)));
    CHECK(r->passthrough);
}

TEST_CASE("the_passthrough_engine_leaves_the_source_exact_even_for_edited_notes", "[render]") {
    const Fixture f = make_fixture(two_notes()).with_edits(edits_of({pitch_add("a", 200.0)}));
    const auto r = render(f, PassthroughEngine{});
    CHECK(bit_identical(audio_of(r), source_audio(f)));
    CHECK(r->notes_rendered.size() == 1);  // asked for, though unchanged
    CHECK_FALSE(r->passthrough);
}

TEST_CASE("a_sub_region_equals_the_matching_slice_of_the_whole_render", "[render]") {
    const Fixture f = make_fixture(two_notes()).with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto whole_r = render(f, GainTestEngine{});
    // 0.98 s .. 1.02 s cuts through a's falling ramp.
    const OutputRange cut =
        OutputRange::from_start(OutputTime::unchecked_from_origin(Seconds{0.98}), Seconds{0.04});
    const auto part = render_region(f.inputs(), cut, GainTestEngine{}, {}).value();
    const auto first = static_cast<std::size_t>(std::llround(0.98 * k_rate));
    REQUIRE(part->buffer.audio.frame_count().value() == 1920);
    for (std::size_t i = 0; i < 1920; ++i) {
        REQUIRE(audio_of(part).channel(1)[i] == audio_of(whole_r).channel(1)[first + i]);
    }
}

TEST_CASE("the_result_range_is_the_request_snapped_to_whole_samples", "[render]") {
    const Fixture f = make_fixture(two_notes());
    const OutputRange odd = OutputRange::from_start(
        OutputTime::unchecked_from_origin(Seconds{0.5000071}), Seconds{0.1});
    const auto r = render_region(f.inputs(), odd, GainTestEngine{}, {}).value();
    const double start = r->buffer.start.unchecked_since_origin().value();
    CHECK(std::abs(start * k_rate - std::round(start * k_rate)) < 1e-6);
    CHECK(std::abs(start - 0.5000071) <= 0.5 / k_rate + 1e-12);
    CHECK(output_range(r->buffer).duration().value() > 0.0999);
}

TEST_CASE("an_empty_region_and_an_empty_source_give_empty_audio", "[render]") {
    const Fixture f = make_fixture(two_notes());
    const OutputRange none = OutputRange::from_start(OutputTime{}, Seconds{0.0});
    CHECK(
        render_region(f.inputs(), none, GainTestEngine{}, {}).value()->buffer.audio.frame_count() ==
        Samples{0});
    AudioSource empty =
        AudioSource::from_buffer(AudioBuffer::create(1, Samples{0}, Hz{k_rate}).value());
    auto analysis = std::make_shared<AnalysisResult>();
    analysis->source_id = empty.id();
    const RenderInputs inputs{.source = empty,
                              .analysis = analysis,
                              .edits = std::make_shared<const EditList>(TrackId{"t1"})};
    const auto r = render_region(inputs, none, GainTestEngine{}, {});
    REQUIRE(r.has_value());
    CHECK((*r)->passthrough);
}

TEST_CASE("bad_inputs_are_reported_with_the_documented_codes", "[render]") {
    const Fixture f = make_fixture(two_notes());
    const GainTestEngine engine;
    const auto code = [&](const RenderInputs& in, OutputRange region, RenderParams p = {}) {
        return render_region(in, region, engine, p).error().code;
    };
    RenderInputs no_analysis = f.inputs();
    no_analysis.analysis = nullptr;
    CHECK(code(no_analysis, whole(f.source)) == ErrorCode::invalid_argument);
    RenderInputs no_edits = f.inputs();
    no_edits.edits = nullptr;
    CHECK(code(no_edits, whole(f.source)) == ErrorCode::invalid_argument);

    Fixture other = f;
    other.analysis = std::make_shared<AnalysisResult>(*f.analysis);
    other.analysis->source_id = SourceId{"another"};
    CHECK(code(other.inputs(), whole(f.source)) == ErrorCode::invalid_argument);

    CHECK(code(f.inputs(), whole(f.source), {.crossfade_length = Seconds{-0.001}}) ==
          ErrorCode::invalid_argument);
    CHECK(code(f.inputs(), whole(f.source), {.crossfade_length = Seconds{0.0201}}) ==
          ErrorCode::invalid_argument);
    CHECK(render_region(f.inputs(), whole(f.source), engine, {.crossfade_length = k_max_crossfade})
              .has_value());

    const OutputRange beyond = OutputRange::from_start(OutputTime{}, Seconds{3.5});
    CHECK(code(f.inputs(), beyond) == ErrorCode::out_of_range);
    const OutputRange before =
        OutputRange::from_start(OutputTime::unchecked_from_origin(Seconds{-0.1}), Seconds{0.5});
    CHECK(code(f.inputs(), before) == ErrorCode::out_of_range);
}

TEST_CASE("timing_edits_are_not_implemented_even_for_a_far_region", "[render]") {
    const Fixture f =
        make_fixture(two_notes())
            .with_edits(edits_of({make_spec("b", EditCategory::time, EditParam::onset_shift,
                                            EditOp::add, Seconds{0.1})}));
    const OutputRange far = OutputRange::from_start(OutputTime{}, Seconds{0.2});
    CHECK(render_region(f.inputs(), far, PassthroughEngine{}, {}).error().code ==
          ErrorCode::not_implemented);
}

TEST_CASE("timing_edits_on_inactive_notes_are_ignored", "[render]") {
    Fixture f = make_fixture(two_notes())
                    .with_edits(edits_of({make_spec("b", EditCategory::time, EditParam::onset_shift,
                                                    EditOp::add, Seconds{0.1})}));
    f.analysis->notes[1].is_active = false;
    const auto r = render(f, PassthroughEngine{});
    CHECK(bit_identical(audio_of(r), source_audio(f)));
}

TEST_CASE("an_engine_that_returns_the_wrong_shape_is_an_internal_error", "[render]") {
    struct MonoEngine final : SynthesisEngine {
        [[nodiscard]] std::string_view name() const noexcept override { return "mono"; }
        [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
        [[nodiscard]] std::string settings_key() const override { return {}; }
        [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& /*note*/,
                                                      const EffectiveNote& /*eff*/,
                                                      const TimeMap& /*map*/,
                                                      OutputRange r) const override {
            const Samples n = to_samples(r.duration(), src.sample_rate(), Rounding::nearest);
            return OutputAudio{.audio = AudioBuffer::create(1, n, src.sample_rate()).value(),
                               .start = r.start()};
        }
    };
    const Fixture f = make_fixture(two_notes()).with_edits(edits_of({gain_edit("a", 6.0)}));
    CHECK(render_region(f.inputs(), whole(f.source), MonoEngine{}, {}).error().code ==
          ErrorCode::internal);
}

TEST_CASE("a_hard_splice_with_zero_crossfade_is_allowed", "[render]") {
    const Fixture f = make_fixture(two_notes()).with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto r = render(f, GainTestEngine{}, {.crossfade_length = Seconds{0.0}});
    const AudioBuffer original = source_audio(f);
    CHECK(audio_of(r).channel(0)[23999] == original.channel(0)[23999]);
    CHECK(audio_of(r).channel(0)[24000] == original.channel(0)[24000] * gain_of(6.0));
}

TEST_CASE("the_cache_returns_the_same_result_and_undo_returns_to_the_old_entry", "[render]") {
    RenderCache cache{64'000'000};
    Fixture f = make_fixture(two_notes());
    const auto base = render(f, GainTestEngine{}, {.cache = &cache});
    CHECK(render(f, GainTestEngine{}, {.cache = &cache}) == base);  // hit: same object
    Fixture edited = f.with_edits(*f.edits->add(std::vector{gain_edit("a", 6.0)}, k_t0));
    const auto changed = render(edited, GainTestEngine{}, {.cache = &cache});
    CHECK(changed != base);
    CHECK(cache.entry_count() == 2);
    const Fixture undone = f.with_edits(*edited.edits->undo());
    CHECK(render(undone, GainTestEngine{}, {.cache = &cache}) == base);
}

TEST_CASE("the_cache_key_sees_analysis_content_engine_and_a_neighbours_ramp", "[render]") {
    RenderCache cache{64'000'000};
    const GainTestEngine engine;
    Fixture f = make_fixture(two_notes()).with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto first = render(f, engine, {.cache = &cache});

    // Same id, span and version, different content: must not hit.
    Fixture changed = f;
    changed.analysis = std::make_shared<AnalysisResult>(*f.analysis);
    changed.analysis->notes[0].pitch_center = Cents{7000.0};
    CHECK(render(changed, engine, {.cache = &cache}) != first);

    // A different engine version or settings must not hit.
    CHECK(render(f, PassthroughEngine{}, {.cache = &cache}) != first);

    // Crossfade length is part of the key.
    CHECK(render(f, engine, {.crossfade_length = Seconds{0.005}, .cache = &cache}) != first);
}

TEST_CASE("moving_a_neighbour_outside_the_region_that_changes_a_ramp_changes_the_key", "[render]") {
    // a ends at 1.0 s; b starts 1.5 * m after it (gap 720 samples), so a's falling ramp is
    // shortened to 360 samples. The region ends 100 samples after a, before b's zone starts.
    RenderCache cache{64'000'000};
    const GainTestEngine engine;
    const std::vector<Note> near{make_note("a", 0.5, 1.0), make_note("b", 1.015, 1.5)};
    const std::vector<Note> far{make_note("a", 0.5, 1.0), make_note("b", 1.2, 1.5)};
    const OutputRange region =
        OutputRange::from_start(OutputTime::unchecked_from_origin(Seconds{0.99}), Seconds{0.0121});
    const auto specs = std::vector{gain_edit("a", 6.0), gain_edit("b", -6.0)};
    Fixture f1 = make_fixture(near);
    f1 = f1.with_edits(edits_of(specs));
    Fixture f2 = make_fixture(far);
    f2.analysis->version = f1.analysis->version;
    f2 = f2.with_edits(edits_of(specs));
    const auto r1 = render_region(f1.inputs(), region, engine, {.cache = &cache}).value();
    const auto r2 = render_region(f2.inputs(), region, engine, {.cache = &cache}).value();
    CHECK(r1 != r2);
    CHECK(r1->cache_id != r2->cache_id);
}
