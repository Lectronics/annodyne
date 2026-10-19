#pragma once

#include "core/types/tracks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Objective analysis metrics (mir_eval definitions; docs/design/phase3-orchestrator-cli.md §3).
// References are per-frame vectors of absolute cents on the SAME grid as the estimate, NaN =
// unvoiced.

namespace anno::test {

struct PitchScores {
    int voiced_reference = 0;  // reference frames that are voiced (within the compared range)
    int correct = 0;           // estimate voiced and within the tolerance
    int octave_errors = 0;     // estimate voiced and within 50 cents of +-1200 from the reference
    int false_alarms = 0;      // estimate voiced where the reference is unvoiced
    int unvoiced_reference = 0;
    [[nodiscard]] double rpa() const {
        return voiced_reference == 0 ? 1.0 : static_cast<double>(correct) / voiced_reference;
    }
    [[nodiscard]] double false_alarm_rate() const {
        return unvoiced_reference == 0 ? 0.0
                                       : static_cast<double>(false_alarms) / unvoiced_reference;
    }
};

/// Compares frames [first, last) of the estimate against the reference.
inline PitchScores score_pitch(const PitchTrack& est, const std::vector<double>& reference_cents,
                               double tolerance_cents, std::size_t first, std::size_t last) {
    PitchScores s;
    for (std::size_t i = first; i < last && i < reference_cents.size() && i < est.f0.size(); ++i) {
        const double ref = reference_cents[i];
        const double e = est.f0[i].value();
        if (std::isnan(ref)) {
            ++s.unvoiced_reference;
            s.false_alarms += std::isnan(e) ? 0 : 1;
            continue;
        }
        ++s.voiced_reference;
        if (std::isnan(e)) {
            continue;
        }
        const double err = e - ref;
        if (std::abs(err) <= tolerance_cents) {
            ++s.correct;
        } else if (std::abs(std::abs(err) - 1200.0) <= 50.0) {
            ++s.octave_errors;
        }
    }
    return s;
}

/// Largest absolute error in cents over the voiced frames of [first, last) where both are voiced.
inline double max_abs_error_cents(const PitchTrack& est, const std::vector<double>& reference_cents,
                                  std::size_t first, std::size_t last) {
    double worst = 0.0;
    for (std::size_t i = first; i < last && i < reference_cents.size() && i < est.f0.size(); ++i) {
        if (!std::isnan(reference_cents[i]) && !std::isnan(est.f0[i].value())) {
            worst = std::max(worst, std::abs(est.f0[i].value() - reference_cents[i]));
        }
    }
    return worst;
}

struct OnsetScores {
    int matched = 0;
    int estimated = 0;
    int reference = 0;
    [[nodiscard]] double precision() const {
        return estimated == 0 ? 1.0 : static_cast<double>(matched) / estimated;
    }
    [[nodiscard]] double recall() const {
        return reference == 0 ? 1.0 : static_cast<double>(matched) / reference;
    }
    [[nodiscard]] double f_measure() const {
        const double p = precision();
        const double r = recall();
        return p + r == 0.0 ? 0.0 : 2.0 * p * r / (p + r);
    }
};

/// One-to-one matching of estimated to reference times (seconds): each reference onset takes the
/// nearest unmatched estimate within +-window_s (mir_eval's onset F-measure, greedy by distance).
inline OnsetScores score_onsets(const std::vector<double>& estimated_s,
                                const std::vector<double>& reference_s, double window_s) {
    OnsetScores s;
    s.estimated = static_cast<int>(estimated_s.size());
    s.reference = static_cast<int>(reference_s.size());
    struct Pair {
        double distance;
        std::size_t e;
        std::size_t r;
    };
    std::vector<Pair> pairs;
    for (std::size_t e = 0; e < estimated_s.size(); ++e) {
        for (std::size_t r = 0; r < reference_s.size(); ++r) {
            const double d = std::abs(estimated_s[e] - reference_s[r]);
            if (d <= window_s) {
                pairs.push_back({.distance = d, .e = e, .r = r});
            }
        }
    }
    std::ranges::sort(pairs, {}, &Pair::distance);
    std::vector<bool> e_used(estimated_s.size(), false);
    std::vector<bool> r_used(reference_s.size(), false);
    for (const Pair& p : pairs) {
        if (!e_used[p.e] && !r_used[p.r]) {
            e_used[p.e] = true;
            r_used[p.r] = true;
            ++s.matched;
        }
    }
    return s;
}

}  // namespace anno::test
