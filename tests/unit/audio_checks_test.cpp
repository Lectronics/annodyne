#include "support/audio_checks.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace anno;
using namespace anno::test;

TEST_CASE("bit_identical_distinguishes_one_ulp", "[audio_checks]") {
    const AudioBuffer a = make_signal(2, 0.1);
    AudioBuffer b = a;
    CHECK(bit_identical(a, b));
    CHECK_FALSE(first_difference(a, b).has_value());
    b.channel(1)[17] = std::nextafter(b.channel(1)[17], 2.0F);
    CHECK_FALSE(bit_identical(a, b));
    CHECK(first_difference(a, b) == 17);
}

TEST_CASE("the_click_detector_is_silent_on_a_clean_signal_and_finds_a_step", "[audio_checks]") {
    AudioBuffer clean = make_signal(1, 0.2);
    CHECK(find_clicks(clean).empty());
    AudioBuffer stepped = clean;
    for (std::size_t i = 4000; i < stepped.channel(0).size(); ++i) {
        stepped.channel(0)[i] *= 0.2F;  // an abrupt level change in the middle of a tone
    }
    const auto clicks = find_clicks(stepped);
    REQUIRE_FALSE(clicks.empty());
    CHECK(clicks.front() >= 3999);
    CHECK(clicks.back() <= 4001);
}

TEST_CASE("the_click_detector_ignores_digital_silence_and_a_lone_tiny_blip", "[audio_checks]") {
    AudioBuffer silent = AudioBuffer::create(1, Samples{2000}, Hz{48000.0}).value();
    silent.channel(0)[1000] = 1e-4F;  // below k_click_floor
    CHECK(find_clicks(silent).empty());
}

TEST_CASE("sanity_counts_nan_and_inf_and_measures_dc_and_peak", "[audio_checks]") {
    AudioBuffer audio = AudioBuffer::create(1, Samples{1000}, Hz{48000.0}).value();
    for (float& v : audio.channel(0)) {
        v = 0.1F;
    }
    audio.channel(0)[3] = std::numeric_limits<float>::quiet_NaN();
    audio.channel(0)[4] = std::numeric_limits<float>::infinity();
    audio.channel(0)[5] = -0.5F;
    const SanityReport report = sanity(audio);
    CHECK(report.nan_inf_samples == 2);
    CHECK(report.peak == 0.5F);
    CHECK(report.worst_dc_dbfs.value() > -25.0);  // a DC of ~0.1 is about -20 dBFS
    CHECK(sanity(make_signal(2, 0.5)).worst_dc_dbfs < tolerance::k_dc_offset_max_dbfs);
}

TEST_CASE("hash_audio_is_stable_and_sensitive", "[audio_checks]") {
    AudioBuffer a = make_signal(2, 0.1);
    const AudioBuffer b = a;
    CHECK(hash_audio(a) == hash_audio(b));
    a.channel(0)[0] += 1e-7F;
    CHECK(hash_audio(a) != hash_audio(b));
}

TEST_CASE("length_matches_allows_one_sample", "[audio_checks]") {
    const AudioBuffer a = AudioBuffer::create(1, Samples{100}, Hz{48000.0}).value();
    CHECK(length_matches(a, Samples{100}));
    CHECK(length_matches(a, Samples{101}));
    CHECK_FALSE(length_matches(a, Samples{102}));
}
