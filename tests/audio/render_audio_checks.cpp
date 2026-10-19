#include "core/synthesis/passthrough/passthrough_engine.h"
#include "support/gain_engine.h"
#include "support/render_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>

using namespace anno;
using namespace anno::test;

namespace {

constexpr WallTime k_t0{};

EditList edits_of(std::vector<EditSpec> group) {
    return EditList{TrackId{"t1"}}.add(group, k_t0).value();
}

Fixture scenario() {
    // Boundaries off the test signal's zero crossings, so a hard splice makes a real jump.
    return make_fixture({make_note("a", 0.5013, 1.0007), make_note("b", 1.0007, 1.5011),
                         make_note("c", 2.0003, 2.5)});
}

AudioBuffer source_audio(const Fixture& f) {
    return f.source.read(Samples{0}, f.source.frame_count()).value();
}

}  // namespace

TEST_CASE("null_test_empty_edit_list_returns_the_source", "[audio_checks]") {
    const Fixture f = scenario();
    const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    CHECK(bit_identical(audio_of(r), source_audio(f)));
}

TEST_CASE("undo_null_test_edits_then_full_undo_return_the_source", "[audio_checks]") {
    Fixture f = scenario();
    EditList list{TrackId{"t1"}};
    list = list.add(std::vector{gain_edit("a", 6.0)}, k_t0).value();
    list = list.add(std::vector{gain_edit("b", -3.0), gain_edit("c", 2.0)}, k_t0).value();
    list = list.undo().value().undo().value();
    f = f.with_edits(list);
    const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    CHECK(bit_identical(audio_of(r), source_audio(f)));
}

TEST_CASE("untouched_regions_test_note_c_is_unchanged_by_edits_to_a_and_b", "[audio_checks]") {
    const Fixture f = scenario().with_edits(edits_of({gain_edit("a", 6.0), gain_edit("b", -6.0)}));
    const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    const AudioBuffer original = source_audio(f);
    // Everything after b's end (1.5011 s = sample 72053) plus the 10 ms crossfade is untouched.
    const auto from = static_cast<std::size_t>(72053 + 480 + 1);
    for (int c = 0; c < 2; ++c) {
        for (std::size_t i = from; std::cmp_less(i, original.frame_count().value()); ++i) {
            REQUIRE(audio_of(r).channel(c)[i] == original.channel(c)[i]);
        }
    }
    CHECK_FALSE(bit_identical(audio_of(r), original));  // the check does see the edit
}

TEST_CASE("click_detector_a_10ms_crossfade_is_clean_and_a_hard_splice_is_detected",
          "[audio_checks]") {
    const Fixture f =
        scenario().with_edits(edits_of({gain_edit("a", 12.0), gain_edit("b", -12.0)}));
    const auto smooth = render_region(f.inputs(), whole(f.source), GainTestEngine{},
                                      {.crossfade_length = Seconds{0.010}})
                            .value();
    CHECK(find_clicks(audio_of(smooth)).size() ==
          static_cast<std::size_t>(tolerance::k_click_detections));
    // Calibration pair: the same edit with no crossfade must be flagged, or the threshold is blind.
    const auto splice = render_region(f.inputs(), whole(f.source), GainTestEngine{},
                                      {.crossfade_length = Seconds{0.0}})
                            .value();
    CHECK_FALSE(find_clicks(audio_of(splice)).empty());
}

TEST_CASE("sanity_no_nan_inf_low_dc_peaks_reported", "[audio_checks]") {
    const Fixture f = scenario().with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    const SanityReport report = sanity(audio_of(r));
    CHECK(report.nan_inf_samples == tolerance::k_nan_inf_samples);
    CHECK(report.worst_dc_dbfs < tolerance::k_dc_offset_max_dbfs);
    CHECK(report.peak > 0.0F);
}

TEST_CASE("determinism_two_renders_hash_equal", "[audio_checks]") {
    const Fixture f = scenario().with_edits(edits_of({gain_edit("a", 6.0), gain_edit("c", -2.0)}));
    const auto one = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    const auto two = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    CHECK(hash_audio(audio_of(one)) == hash_audio(audio_of(two)));
}

TEST_CASE("length_check_output_has_the_source_length", "[audio_checks]") {
    const Fixture f = scenario().with_edits(edits_of({gain_edit("a", 6.0)}));
    const auto r = render_region(f.inputs(), whole(f.source), GainTestEngine{}, {}).value();
    CHECK(length_matches(audio_of(r), f.source.frame_count()));
}

TEST_CASE("passthrough_engine_with_edits_is_bit_exact_and_click_free", "[audio_checks]") {
    const Fixture f = scenario().with_edits(edits_of({pitch_add("a", 200.0)}));
    const auto r = render_region(f.inputs(), whole(f.source), PassthroughEngine{}, {}).value();
    CHECK(bit_identical(audio_of(r), source_audio(f)));
}
