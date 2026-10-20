#include "core/analysis/pitch_target/pitch_target.h"
#include "tolerances.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <numbers>
#include <string>

using namespace anno;

namespace {

constexpr double k_hop = 0.005;
constexpr double k_a4 = 6900.0;
constexpr double k_note_start = 1.0;  // the note starts 1 s into the source

/// A note whose curve is `cents(t)` (t seconds since the note start), with a matching pitch track.
struct Scene {
    Note note;
    PitchTrack pitch;
    OnsetList onsets;
    std::vector<double> disagreement;

    [[nodiscard]] NoteEvidence evidence() const {
        return {.pitch = &pitch, .onsets = &onsets, .disagreement = disagreement};
    }
};

Scene make_scene(const std::function<double(double)>& cents, double seconds, double voicing = 1.0,
                 const std::function<double(double)>& amp_db = {}) {
    Scene sc;
    const auto frames = static_cast<std::size_t>(std::lround(seconds / k_hop));
    std::vector<double> curve(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        curve[i] = cents(static_cast<double>(i) * k_hop);
    }
    std::vector<double> sorted = curve;
    std::ranges::sort(sorted);
    const double centre = sorted[frames / 2];
    Note& n = sc.note;
    n.id = NoteId{"n1"};
    n.track_id = TrackId{"t"};
    n.source_start = SourceTime{} + Seconds{k_note_start};
    n.attack_end = n.source_start;
    n.release_start = n.source_start + Seconds{seconds};
    n.source_end = n.release_start;
    n.pitch_center = Cents{centre};
    n.grid = FrameGrid{.hop = Seconds{k_hop},
                       .start = n.source_start,
                       .n_frames = static_cast<std::int64_t>(frames)};
    for (std::size_t i = 0; i < frames; ++i) {
        n.pitch_drift.emplace_back(0.0);
        n.pitch_mod.emplace_back(curve[i] - centre);
        n.amp.emplace_back(amp_db ? amp_db(static_cast<double>(i) * k_hop) : -20.0);
    }
    const auto total =
        static_cast<std::size_t>(std::lround((k_note_start + seconds + 0.5) / k_hop));
    sc.pitch.grid = FrameGrid{
        .hop = Seconds{k_hop}, .start = SourceTime{}, .n_frames = static_cast<std::int64_t>(total)};
    sc.pitch.f0.assign(total, Cents{std::nan("")});
    sc.pitch.voicing_prob.assign(total, 0.0);
    sc.pitch.confidence_prob.assign(total, 0.0);
    const auto offset = static_cast<std::size_t>(std::lround(k_note_start / k_hop));
    for (std::size_t i = 0; i < frames; ++i) {
        sc.pitch.f0[offset + i] = Cents{curve[i]};
        sc.pitch.voicing_prob[offset + i] = voicing;
        sc.pitch.confidence_prob[offset + i] = 1.0;
    }
    return sc;
}

PitchTarget estimate(const Scene& sc, const NoteContext& ctx = {},
                     const PitchTargetParams& p = {}) {
    return estimate_pitch_target(sc.note, sc.evidence(), ctx, p).value();
}

std::string describe(const PitchTarget& r) {
    std::string out = "kind " + std::to_string(static_cast<int>(r.kind)) + " target " +
                      std::to_string(r.target.value()) + " core " +
                      std::to_string((r.core.start() - SourceTime{}).value()) + ".." +
                      std::to_string((r.core.end() - SourceTime{}).value()) + " confidence " +
                      std::to_string(r.confidence_prob) + " segments:";
    for (const TonalSegment& s : r.segments) {
        out += " [" + std::to_string((s.range.start() - SourceTime{}).value()) + "-" +
               std::to_string((s.range.end() - SourceTime{}).value()) +
               (s.shape == TonalShape::moving ? " mv " : " lv ") +
               std::to_string(s.start_pitch.value()) + ">" + std::to_string(s.end_pitch.value()) +
               "]";
    }
    return out;
}

double steady(double /*t*/) {
    return k_a4;
}

NoteContext touching(std::optional<Cents> previous, std::optional<Cents> next) {
    return {.previous_pitch = previous,
            .next_pitch = next,
            .gap_before = Seconds{0.0},
            .gap_after = Seconds{0.0}};
}

}  // namespace

TEST_CASE("a_steady_note_is_whole_at_its_pitch", "[pitch_target]") {
    const auto sc = make_scene(steady, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::whole);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
    CHECK(t.approach == ApproachKind::none);
    CHECK(t.release == ReleaseKind::none);
    CHECK(t.confidence_prob > 0.8);
    CHECK(t.target_whole.has_value());
    Note described = sc.note;
    described.pitch_target = t;
    CHECK(validate(described).has_value());
}

TEST_CASE("a_vibrato_note_is_whole_at_the_centre", "[pitch_target]") {
    const auto sc = make_scene(
        [](double t) { return k_a4 + 50.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t); }, 1.2);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::whole);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_wide_vibrato_is_not_an_ornament", "[pitch_target]") {
    const auto sc = make_scene(
        [](double t) { return k_a4 + 100.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t); }, 1.2);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::whole);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_note_that_hunts_around_its_pitch_is_whole_and_averaged", "[pitch_target]") {
    const auto hunt = [](double t) {
        const double phase = std::fmod(t, 0.5) / 0.5;
        return k_a4 + 45.0 * (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
    };
    const auto sc = make_scene(hunt, 1.5);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::whole);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_hunt_cents);
}

TEST_CASE("a_scoop_into_the_note_is_not_part_of_the_pitch", "[pitch_target]") {
    const auto scoop = [](double t) { return k_a4 - (t < 0.08 ? 200.0 * (1.0 - t / 0.08) : 0.0); };
    const auto sc = make_scene(scoop, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
    CHECK(t.approach == ApproachKind::scoop);
    CHECK(t.core.start() > sc.note.source_start);
}

TEST_CASE("an_approach_from_the_previous_note_is_legato", "[pitch_target]") {
    const double previous = k_a4 - 200.0;
    const auto glide = [&](double t) { return t < 0.1 ? previous + 200.0 * t / 0.1 : k_a4; };
    const auto sc = make_scene(glide, 1.0);
    const auto t = estimate(sc, touching(Cents{previous}, std::nullopt));
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.approach == ApproachKind::legato);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("an_overshoot_that_settles_is_labelled_and_excluded", "[pitch_target]") {
    const auto overshoot = [](double t) {
        if (t < 0.06) {
            return k_a4 - 150.0 + 210.0 * t / 0.06;  // rises past the target by 60
        }
        if (t < 0.12) {
            return k_a4 + 60.0 * (1.0 - (t - 0.06) / 0.06);
        }
        return k_a4;
    };
    const auto sc = make_scene(overshoot, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.approach == ApproachKind::overshoot);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_fall_at_the_end_is_a_release", "[pitch_target]") {
    const auto fall = [](double t) { return k_a4 - (t > 0.85 ? 300.0 * (t - 0.85) / 0.15 : 0.0); };
    const auto sc = make_scene(fall, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.release == ReleaseKind::fall);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_rise_at_the_end_is_a_release", "[pitch_target]") {
    const auto rise = [](double t) { return k_a4 + (t > 0.88 ? 80.0 * (t - 0.88) / 0.12 : 0.0); };
    const auto sc = make_scene(rise, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.release == ReleaseKind::rise);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_dip_before_a_higher_note_is_a_preparation", "[pitch_target]") {
    const auto dip = [](double t) { return k_a4 - (t > 0.9 ? 30.0 : 0.0); };
    const auto sc = make_scene(dip, 1.0);
    const auto t = estimate(sc, touching(std::nullopt, Cents{k_a4 + 200.0}));
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.release == ReleaseKind::preparation);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("scoop_in_steady_and_fall_out_use_the_middle", "[pitch_target]") {
    const auto mlm = [](double t) {
        if (t < 0.1) {
            return k_a4 - 200.0 * (1.0 - t / 0.1);
        }
        if (t > 0.9) {
            return k_a4 - 250.0 * (t - 0.9) / 0.1;
        }
        return k_a4;
    };
    const auto sc = make_scene(mlm, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.approach == ApproachKind::scoop);
    CHECK(t.release == ReleaseKind::fall);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_short_hold_before_a_long_fall_has_low_confidence", "[pitch_target]") {
    const auto hold_then_fall = [](double t) {
        return t < 0.06 ? k_a4 : k_a4 - 600.0 * (t - 0.06) / 0.4;
    };
    const auto sc = make_scene(hold_then_fall, 0.46);
    const auto t = estimate(sc);
    INFO(describe(t));
    CHECK(t.kind == PitchTargetKind::settled);
    CHECK(t.confidence_prob < 0.5);
}

TEST_CASE("a_small_end_drift_stays_whole_and_near_the_steady_part", "[pitch_target]") {
    const auto drift = [](double t) { return k_a4 + (t > 0.8 ? 25.0 * (t - 0.8) / 0.2 : 0.0); };
    const auto sc = make_scene(drift, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::whole);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_loose_cents);
}

TEST_CASE("a_slow_sag_stays_whole", "[pitch_target]") {
    const auto sag = [](double t) { return k_a4 - 20.0 * t; };
    const auto sc = make_scene(sag, 1.0);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::whole);
}

TEST_CASE("a_bend_without_a_level_part_is_a_glide", "[pitch_target]") {
    const auto bend = [](double t) { return k_a4 + 100.0 * t / 0.4; };
    const auto sc = make_scene(bend, 0.4);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::glide);
    CHECK(std::abs(t.target.value() - (k_a4 + 100.0)) <= tolerance::k_target_glide_end_cents);
    REQUIRE(t.secondary.has_value());
    CHECK(std::abs(t.secondary->value() - k_a4) <= tolerance::k_target_glide_end_cents);
}

TEST_CASE("two_plateaus_far_apart_are_multi", "[pitch_target]") {
    const auto step = [](double t) {
        if (t < 0.5) {
            return k_a4;
        }
        return t < 0.56 ? k_a4 + 100.0 * (t - 0.5) / 0.06 : k_a4 + 100.0;
    };
    const auto sc = make_scene(step, 1.2);
    const auto t = estimate(sc);
    INFO(describe(t));
    CHECK(t.kind == PitchTargetKind::multi);
    REQUIRE(t.secondary.has_value());
    CHECK(std::abs(std::abs(t.secondary->value() - t.target.value()) - 100.0) <=
          tolerance::k_target_loose_cents);
}

TEST_CASE("a_90_ms_note_is_short_with_low_confidence", "[pitch_target]") {
    const auto sc = make_scene(steady, 0.09);
    const auto t = estimate(sc);
    CHECK(t.flags.has(PitchFlag::short_note));
    CHECK(t.confidence_prob < 0.6);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_consonant_dip_before_the_vowel_does_not_move_the_target", "[pitch_target]") {
    const auto dip = [](double t) {
        return k_a4 - (t < 0.06 ? 150.0 * std::sin(std::numbers::pi * t / 0.06) : 0.0);
    };
    auto sc = make_scene(dip, 1.0);
    sc.onsets.times.push_back(sc.note.source_start);
    sc.onsets.strength.push_back(3.0);
    sc.onsets.kind.push_back(OnsetKind::consonant);
    const auto t = estimate(sc);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("halved_f0_inside_a_note_is_creak_and_does_not_move_the_target", "[pitch_target]") {
    const auto creak = [](double t) { return (t > 0.4 && t < 0.5) ? k_a4 - 1200.0 : k_a4; };
    const auto sc = make_scene(creak, 1.0);
    const auto t = estimate(sc);
    CHECK(t.flags.has(PitchFlag::creak));
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_note_without_voicing_is_unpitched", "[pitch_target]") {
    const auto sc = make_scene(steady, 1.0, 0.1);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::unpitched);
    CHECK(t.target == sc.note.pitch_center);
    CHECK(t.confidence_prob == 0.0);
}

TEST_CASE("a_quiet_sharp_ending_does_not_pull_the_target", "[pitch_target]") {
    const auto sharp = [](double t) { return k_a4 + (t > 0.7 ? 40.0 : 0.0); };
    const auto quiet = [](double t) { return t > 0.7 ? -38.0 : -20.0; };
    const auto sc = make_scene(sharp, 1.0, 1.0, quiet);
    const auto t = estimate(sc);
    INFO(describe(t));
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_quiet_end_cents);
}

TEST_CASE("the_target_is_deterministic_and_finite", "[pitch_target]") {
    const auto sc = make_scene(
        [](double t) { return k_a4 + 30.0 * std::sin(2.0 * std::numbers::pi * 5.0 * t); }, 0.8);
    const auto a = estimate(sc);
    const auto b = estimate(sc);
    CHECK(a.target == b.target);
    CHECK(a.kind == b.kind);
    CHECK(std::isfinite(a.target.value()));
    CHECK(a.confidence_prob >= 0.0);
    CHECK(a.confidence_prob <= 1.0);
    for (const TonalSegment& s : a.segments) {
        CHECK(std::isfinite(s.start_pitch.value()));
        CHECK(std::isfinite(s.end_pitch.value()));
    }
}

TEST_CASE("a_note_the_target_describes_validates", "[pitch_target]") {
    for (const auto& f : std::vector<std::function<double(double)>>{
             steady, [](double t) { return k_a4 - (t < 0.08 ? 200.0 * (1.0 - t / 0.08) : 0.0); },
             [](double t) { return k_a4 + 100.0 * t / 0.4; }}) {
        auto sc = make_scene(f, 0.6);
        sc.note.pitch_target = estimate(sc);
        CHECK(validate(sc.note).has_value());
    }
}

TEST_CASE("invalid_inputs_are_errors_and_thin_evidence_is_not", "[pitch_target]") {
    const auto sc = make_scene(steady, 0.5);
    PitchTargetParams bad;
    bad.segmenting.glissando_g = 0.0;
    CHECK(!estimate_pitch_target(sc.note, sc.evidence(), {}, bad).has_value());
    bad = {};
    bad.reliability.consonant_zone = Seconds{std::nan("")};
    CHECK(!estimate_pitch_target(sc.note, sc.evidence(), {}, bad).has_value());
    CHECK(!estimate_pitch_target(sc.note, NoteEvidence{}, {}, {}).has_value());  // no pitch track
    Scene broken = sc;
    broken.note.pitch_mod.pop_back();
    CHECK(!estimate_pitch_target(broken.note, broken.evidence(), {}, {}).has_value());
    const auto tiny = make_scene(steady, 0.01);  // two frames: valid, low confidence
    const auto r = estimate_pitch_target(tiny.note, tiny.evidence(), {}, {});
    REQUIRE(r.has_value());
    CHECK(r->confidence_prob < 0.5);
}

TEST_CASE("a_flat_topped_trill_is_an_ornament_with_both_pitches", "[pitch_target][ornament]") {
    // 6 Hz, A4 and +100 cents, ~83 ms plateaus.
    const auto trill = [](double t) {
        return k_a4 + (std::fmod(t, 0.1667) < 0.0833 ? 0.0 : 100.0);
    };
    const auto sc = make_scene(trill, 1.0);
    const auto t = estimate(sc);
    INFO(describe(t));
    CHECK(t.kind == PitchTargetKind::ornament);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
    REQUIRE(t.secondary.has_value());
    CHECK(std::abs(t.secondary->value() - (k_a4 + 100.0)) <= tolerance::k_target_tight_cents);
}

TEST_CASE("a_mordent_returns_to_the_main_note", "[pitch_target][ornament]") {
    const auto mordent = [](double t) { return k_a4 + ((t > 0.5 && t < 0.6) ? 100.0 : 0.0); };
    const auto sc = make_scene(mordent, 1.2);
    const auto t = estimate(sc);
    CHECK(t.kind == PitchTargetKind::ornament);
    CHECK(std::abs(t.target.value() - k_a4) <= tolerance::k_target_tight_cents);
}

TEST_CASE("neither_vibrato_nor_a_single_step_is_an_ornament", "[pitch_target][ornament]") {
    const auto vibrato = [](double t) {
        return k_a4 + 60.0 * std::sin(2.0 * std::numbers::pi * 6.0 * t);
    };
    CHECK(estimate(make_scene(vibrato, 1.0)).kind != PitchTargetKind::ornament);
    const auto step = [](double t) { return k_a4 + (t > 0.5 ? 100.0 : 0.0); };
    CHECK(estimate(make_scene(step, 1.0)).kind != PitchTargetKind::ornament);
    const auto hunt = [](double t) {
        return k_a4 + 45.0 * std::sin(2.0 * std::numbers::pi * 2.0 * t);
    };
    CHECK(estimate(make_scene(hunt, 1.5)).kind != PitchTargetKind::ornament);
}

TEST_CASE("a_target_between_two_grid_lines_is_ambiguous", "[pitch_target][flags]") {
    const auto sc = make_scene([](double) { return k_a4 + 50.0; }, 0.8);
    CHECK(estimate(sc).flags.has(PitchFlag::ambiguous_intent));
    const auto near = make_scene([](double) { return k_a4 + 20.0; }, 0.8);
    CHECK(!estimate(near).flags.has(PitchFlag::ambiguous_intent));
    PitchTargetParams shifted;
    shifted.tuning_offset = Cents{50.0};  // a grid sitting 50 cents sharp: the pitch is on a line
    CHECK(!estimate(sc, {}, shifted).flags.has(PitchFlag::ambiguous_intent));
}

TEST_CASE("a_big_abrupt_step_inside_a_note_is_a_register_break", "[pitch_target][flags]") {
    const auto jump = [](double t) { return t < 0.5 ? k_a4 : k_a4 + 500.0; };
    const auto t = estimate(make_scene(jump, 1.0));
    CHECK(t.kind == PitchTargetKind::multi);
    CHECK(t.flags.has(PitchFlag::register_break));
    const auto small = [](double t2) { return t2 < 0.5 ? k_a4 : k_a4 + 100.0; };
    CHECK(!estimate(make_scene(small, 1.0)).flags.has(PitchFlag::register_break));
}

TEST_CASE("the_style_table_has_a_validated_pop_row_and_provisional_others",
          "[pitch_target][style]") {
    CHECK(!style_defaults(StylePreset::pop).provisional);
    for (const StylePreset s :
         {StylePreset::classical, StylePreset::jazz_blues, StylePreset::gamaka}) {
        CHECK(style_defaults(s).provisional);
    }
    CHECK(style_defaults(StylePreset::pop).glissando_g == 0.32);
    CHECK(style_defaults(StylePreset::classical).glissando_g == 0.16);
    CHECK(style_defaults(StylePreset::jazz_blues).skip_consistent_off_grid);
    CHECK(style_defaults(StylePreset::gamaka).ornaments == OrnamentHandling::leave_alone);
    CHECK(pitch_target_params_for(StylePreset::pop).segmenting.glissando_g == 0.32);
    CHECK(!pitch_target_params_for(StylePreset::pop).ornament.detect_gamaka);
    CHECK(pitch_target_params_for(StylePreset::gamaka).ornament.detect_gamaka);
}

TEST_CASE("a_gamaka_style_note_is_an_ornament_only_with_the_gamaka_preset",
          "[pitch_target][style]") {
    // Continuous slides of +-120 cents at 2.5 Hz: no level part anywhere.
    const auto slides = [](double t) {
        const double phase = std::fmod(t * 2.5, 1.0);
        return k_a4 + 120.0 * (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
    };
    const auto sc = make_scene(slides, 1.6);
    CHECK(estimate(sc).kind != PitchTargetKind::ornament);
    const auto gamaka = estimate(sc, {}, pitch_target_params_for(StylePreset::gamaka));
    CHECK(gamaka.kind == PitchTargetKind::ornament);
    REQUIRE(gamaka.secondary.has_value());
    CHECK(std::abs(gamaka.target.value() - k_a4) <= tolerance::k_target_gamaka_median_cents);
}

TEST_CASE("every_parameter_group_is_validated", "[pitch_target]") {
    const auto bad = [](const auto& edit) {
        PitchTargetParams p;
        edit(p);
        return !validate(p).has_value();
    };
    CHECK(validate(PitchTargetParams{}).has_value());
    CHECK(bad([](PitchTargetParams& p) { p.target.weights.rate_scale_cents_per_s = 0.0; }));
    CHECK(bad([](PitchTargetParams& p) { p.target.weights.amp_range = Decibels{0.0}; }));
    CHECK(bad([](PitchTargetParams& p) { p.target.weights.gamma = -1.0; }));
    CHECK(bad([](PitchTargetParams& p) { p.reliability.unpitched_voicing = 1.5; }));
    CHECK(bad([](PitchTargetParams& p) { p.segmenting.zone_max_share = 0.9; }));
    CHECK(bad([](PitchTargetParams& p) { p.target.preparation_min = Cents{80.0}; }));
    CHECK(bad([](PitchTargetParams& p) { p.ornament.gamaka_min_segments = 0; }));
    CHECK(bad([](PitchTargetParams& p) { p.take.peak_tolerance = Cents{1.0}; }));
    CHECK(bad([](PitchTargetParams& p) { p.tuning_offset = Cents{std::nan("")}; }));
}
