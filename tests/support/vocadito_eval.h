#pragma once

#include "core/types/analysis_result.h"
#include "support/analysis_metrics.h"
#include "support/vocadito.h"

#include <array>
#include <cmath>

// Scores of one analysis against a Vocadito track (shared by the golden ctest and the tuning
// report, so both compute the same numbers). Design: docs/design/phase3-activity.md §5.
//   - pitch: voiced reference frames only (the f0 CSV, 0 Hz = unvoiced), 50-cent tolerance;
//     voicing recall = share of reference-voiced frames the analysis calls voiced (is_voiced);
//   - onsets: note starts against each annotator separately (one-to-one, mir_eval), +-20 and
//     +-50 ms;
//   - note pitch: each annotator-1 note with an analysed note start within 50 ms; its perceived
//     pitch must be within 50 cents of the annotation (an octave error is counted separately).

namespace anno::test {

struct VocaditoScores {
    PitchScores pitch;
    int voiced_reference = 0;
    int voiced_both = 0;
    std::array<OnsetScores, 2> onset20;
    std::array<OnsetScores, 2> onset50;
    int notes = 0;
    int matched_notes = 0;
    int matched_pitch_ok = 0;
    int matched_octave = 0;

    [[nodiscard]] double voicing_recall() const {
        return voiced_reference == 0 ? 0.0 : static_cast<double>(voiced_both) / voiced_reference;
    }
    [[nodiscard]] double octave_rate() const {
        return pitch.voiced_reference == 0
                   ? 0.0
                   : static_cast<double>(pitch.octave_errors) / pitch.voiced_reference;
    }
    [[nodiscard]] double note_pitch_ok() const {
        return matched_notes == 0 ? 0.0 : static_cast<double>(matched_pitch_ok) / matched_notes;
    }
    /// The better of the two annotators (they agree on only ~75 % of onsets themselves).
    [[nodiscard]] double best_f20() const {
        return std::max(onset20[0].f_measure(), onset20[1].f_measure());
    }
    [[nodiscard]] double best_f50() const {
        return std::max(onset50[0].f_measure(), onset50[1].f_measure());
    }
};

inline void accumulate(OnsetScores& sum, const OnsetScores& s) {
    sum.matched += s.matched;
    sum.estimated += s.estimated;
    sum.reference += s.reference;
}

inline void accumulate(VocaditoScores& sum, const VocaditoScores& s) {
    sum.pitch.voiced_reference += s.pitch.voiced_reference;
    sum.pitch.correct += s.pitch.correct;
    sum.pitch.octave_errors += s.pitch.octave_errors;
    sum.pitch.false_alarms += s.pitch.false_alarms;
    sum.pitch.unvoiced_reference += s.pitch.unvoiced_reference;
    sum.voiced_reference += s.voiced_reference;
    sum.voiced_both += s.voiced_both;
    for (std::size_t a = 0; a < 2; ++a) {
        accumulate(sum.onset20[a], s.onset20[a]);
        accumulate(sum.onset50[a], s.onset50[a]);
    }
    sum.notes += s.notes;
    sum.matched_notes += s.matched_notes;
    sum.matched_pitch_ok += s.matched_pitch_ok;
    sum.matched_octave += s.matched_octave;
}

inline VocaditoScores evaluate(const VocaditoTrack& track, const AnalysisResult& result) {
    VocaditoScores s;
    const PitchTrack& pitch = *result.pitch;
    const auto reference = reference_cents(track, pitch.grid.hop.value(), pitch.f0.size());
    s.pitch = score_pitch(pitch, reference, 50.0, 0, pitch.f0.size());
    for (std::size_t i = 0; i < reference.size(); ++i) {
        if (!std::isnan(reference[i])) {
            ++s.voiced_reference;
            s.voiced_both += is_voiced(pitch, i) ? 1 : 0;
        }
    }
    std::vector<double> starts;
    starts.reserve(result.notes.size());
    for (const Note& n : result.notes) {
        starts.push_back((n.source_start - SourceTime{}).value());
    }
    s.notes = static_cast<int>(starts.size());
    const std::array<const std::vector<RefNote>*, 2> annotators{&track.notes_a1, &track.notes_a2};
    for (std::size_t a = 0; a < 2; ++a) {
        std::vector<double> ref_starts;
        ref_starts.reserve(annotators[a]->size());
        for (const RefNote& n : *annotators[a]) {
            ref_starts.push_back(n.onset_s);
        }
        s.onset20[a] = score_onsets(starts, ref_starts, 0.020);
        s.onset50[a] = score_onsets(starts, ref_starts, 0.050);
    }
    for (const RefNote& ref : track.notes_a1) {
        for (const Note& n : result.notes) {
            if (std::abs((n.source_start - SourceTime{}).value() - ref.onset_s) <= 0.050) {
                const double error = n.perceived_pitch.value() - note_cents(ref);
                ++s.matched_notes;
                s.matched_pitch_ok += std::abs(error) <= 50.0 ? 1 : 0;
                s.matched_octave += is_harmonic_error(error) ? 1 : 0;
                break;
            }
        }
    }
    return s;
}

}  // namespace anno::test
