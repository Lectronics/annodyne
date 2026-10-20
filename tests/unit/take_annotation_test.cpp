#include "core/analysis/pitch_target/take_annotation.h"
#include "support/test_data.h"

#include <catch2/catch_test_macros.hpp>

using namespace anno;
using namespace anno::test;

namespace {

/// A whole-kind note from start to end seconds at `cents`.
Note whole_note(double start_s, double end_s, double cents) {
    Note n =
        make_note("n" + std::to_string(static_cast<int>(start_s * 1000.0)), start_s, end_s, cents);
    n.pitch_target = make_whole_target(Cents{cents});
    return n;
}

/// Four phrases of four 0.5 s notes (60 ms gaps inside, 0.6 s between phrases) on the given
/// semitones (MIDI numbers), each sung `sharp` cents sharp.
std::vector<Note> take_of(const std::vector<std::vector<int>>& phrases, double sharp = 0.0) {
    std::vector<Note> out;
    double t = 0.0;
    for (const auto& phrase : phrases) {
        for (const int midi : phrase) {
            out.push_back(whole_note(t, t + 0.5, midi * 100.0 + sharp));
            t += 0.56;
        }
        t += 0.6;
    }
    return out;
}

}  // namespace

TEST_CASE("an_empty_take_has_no_flags", "[take]") {
    CHECK(annotate_take({}, {}).value().empty());
}

TEST_CASE("three_short_touching_notes_are_a_run", "[take]") {
    std::vector<Note> notes;
    notes.reserve(5);
    for (int i = 0; i < 4; ++i) {
        notes.push_back(whole_note(i * 0.2, i * 0.2 + 0.18, 6000.0 + 100.0 * i));
    }
    notes.push_back(whole_note(1.5, 2.5, 6000.0));  // a long note after a gap
    const auto flags = annotate_take(notes, {}).value();
    for (int i = 0; i < 4; ++i) {
        CHECK(flags[static_cast<std::size_t>(i)].has(PitchFlag::run));
    }
    CHECK(!flags[4].has(PitchFlag::run));
    CHECK(!annotate_take(std::span<const Note>{notes}.first(2), {}).value()[0].has(PitchFlag::run));
}

TEST_CASE("a_pitch_class_sung_the_same_way_off_the_grid_is_a_blue_note", "[take]") {
    // The pitch class of D (MIDI 62, 74) is sung 25 cents sharp 4 times; everything else is on the
    // grid.
    std::vector<Note> notes = take_of({{60, 62, 64, 65}, {62, 67, 69, 71}, {62, 64, 62, 60}});
    int sharp_ds = 0;
    for (Note& n : notes) {
        if (std::lround(n.pitch_target.target.value() / 100.0) == 62 && sharp_ds < 4) {
            n.pitch_target.target = Cents{n.pitch_target.target.value() + 25.0};
            ++sharp_ds;
        }
    }
    const auto flags = annotate_take(notes, {}).value();
    int flagged = 0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const bool is_d = std::lround(notes[i].pitch_target.target.value() / 100.0) == 62;
        if (flags[i].has(PitchFlag::consistent_off_grid)) {
            ++flagged;
            CHECK(is_d);
        }
    }
    CHECK(flagged == 4);
}

TEST_CASE("two_off_grid_notes_are_not_enough", "[take]") {
    std::vector<Note> notes = take_of({{60, 62, 64, 65}, {67, 69, 71, 72}});
    notes[1].pitch_target.target = Cents{6225.0};
    notes[5].pitch_target.target = Cents{6225.0 + 1200.0};
    for (const PitchFlags f : annotate_take(notes, {}).value()) {
        CHECK(!f.has(PitchFlag::consistent_off_grid));
    }
}

TEST_CASE("a_singer_sharp_throughout_has_no_blue_notes", "[take]") {
    const auto notes = take_of({{60, 62, 64, 62}, {62, 64, 60, 62}, {60, 64, 62, 60}}, 20.0);
    for (const PitchFlags f : annotate_take(notes, {}).value()) {
        CHECK(!f.has(PitchFlag::consistent_off_grid));
        CHECK(!f.has(PitchFlag::expressive_peak));
    }
}

TEST_CASE("the_highest_note_of_a_phrase_sharp_by_15_cents_is_an_expressive_peak", "[take]") {
    auto notes = take_of({{60, 62, 64, 67}, {60, 64, 65, 69}, {62, 64, 62, 60}});
    notes[3].pitch_target.target = Cents{6700.0 + 15.0};  // phrase 1 peak, 15 sharp
    notes[7].pitch_target.target = Cents{6900.0 + 40.0};  // phrase 2 peak, too sharp
    const auto flags = annotate_take(notes, {}).value();
    CHECK(flags[3].has(PitchFlag::expressive_peak));
    CHECK(!flags[7].has(PitchFlag::expressive_peak));
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (i != 3) {
            CHECK(!flags[i].has(PitchFlag::expressive_peak));
        }
    }
}

TEST_CASE("take_annotation_never_changes_a_target", "[take]") {
    auto notes = take_of({{60, 62, 64, 67}, {60, 64, 65, 69}});
    const auto before = notes;
    (void)annotate_take(notes, {}).value();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        CHECK(notes[i].pitch_target.target == before[i].pitch_target.target);
    }
}

TEST_CASE("take_annotation_rejects_bad_parameters_and_unsorted_notes", "[take]") {
    auto notes = take_of({{60, 62, 64, 67}});
    PitchTargetParams bad;
    bad.take.run_min_notes = 0;
    CHECK(!annotate_take(notes, bad).has_value());
    bad = {};
    bad.take.off_grid_spread = Cents{std::nan("")};
    CHECK(!annotate_take(notes, bad).has_value());
    std::swap(notes[0], notes[2]);
    CHECK(!annotate_take(notes, {}).has_value());
}
