#pragma once

#include "core/analysis/pitch/detail/pyin_observe.h"

#include <vector>

// The pitch HMM of pYIN: Viterbi over {pitch bins, unvoiced}. Internal to core/analysis/pitch.
// Design: docs/design/phase3-pitch-onsets.md.

namespace anno {

/// -log transition costs.
struct HmmCosts {
    std::vector<double>
        smooth;         // staying within `reach` bins, by bin distance 0..reach (triangular)
    double jump = 0.0;  // any larger voiced -> voiced move
    double voiced_to_unvoiced = 0.0;
    double unvoiced_to_unvoiced = 0.0;
    double unvoiced_to_voiced = 0.0;  // into one specific bin
    int reach = 1;
};

struct HmmParams {
    double bin_cents = 10.0;
    double max_jump_cents = 80.0;
    double jump_prob = 1e-4;
    double voiced_to_unvoiced_prob = 0.01;
    double unvoiced_to_voiced_prob = 0.01;
};

/// Pre: bin_count >= 1; probabilities in (0, 1).
[[nodiscard]] HmmCosts make_hmm_costs(const HmmParams& params, int bin_count);

/// Viterbi decoding; returns per frame the chosen bin, or -1 for unvoiced. `excluded[t]` lists bins
/// whose +-`exclusion_bins` neighbourhood receives no voiced evidence in frame t (so an alternative
/// path keeps away from an earlier one). Pre: excluded.size() == obs.size(), obs not empty.
/// Memory: obs.size() x (bins + 1) ints for the back pointers. Pure; thread-safe.
[[nodiscard]] std::vector<int> viterbi(const std::vector<FrameObservation>& obs, const Bins& bins,
                                       const HmmCosts& costs,
                                       const std::vector<std::vector<int>>& excluded,
                                       int exclusion_bins);

}  // namespace anno
