#pragma once

#include "core/types/units.h"

#include <cstddef>
#include <vector>

// pYIN per-frame observations: f0 candidates with probability masses (Mauch & Dixon 2014).
// Internal to core/analysis/pitch. Design: docs/design/phase3-pitch-onsets.md.

namespace anno {

/// Index of a non-negative int (vectors indexed by lag, bin or frame).
constexpr std::size_t idx(int i) {
    return static_cast<std::size_t>(i);
}

/// One f0 hypothesis in a frame.
struct Candidate {
    int bin = 0;
    double cents = 0.0;  // refined absolute cents
    double tau = 0.0;    // refined lag in analysis-rate samples
    double mass = 0.0;   // probability mass from the threshold distribution
};

struct FrameObservation {
    std::vector<Candidate> candidates;  // at most one per occupied bin
    double unvoiced = 1.0;              // 1 - sum of the masses of the YIN candidates
};

/// The pitch grid of the HMM: bin b is centred at min_cents + step_cents * b.
struct Bins {
    double min_cents = 0.0;
    double step_cents = 10.0;
    int count = 0;
    [[nodiscard]] double centre(int b) const { return min_cents + step_cents * b; }
    [[nodiscard]] int of(double cents) const;
};

/// What observe() needs about the analysis: lag range, sample rate, pitch bins.
struct ObserveSetup {
    int tau_min = 2;
    int tau_max = 2;
    double rate = 0.0;
    Bins bins;
    std::vector<double> weights;  // Beta threshold weights, see threshold_weights()
};

/// Beta(2, b) weights (b from the mean) at the 100 thresholds 0.01 .. 1.00, normalised to sum to 1.
[[nodiscard]] std::vector<double> threshold_weights(double mean);

/// Candidates of one frame from its CMND and NCCF (index = lag, see YinFrame): for every threshold
/// the first lag below it, descended to its local minimum, gets that threshold's weight (the global
/// minimum gets a small share when none passes); lags are refined to fractions of a sample by the
/// NCCF peak. Plus weak octave alternatives (one octave above and below the strongest candidate).
/// Pure; thread-safe.
[[nodiscard]] FrameObservation observe(const std::vector<double>& cmnd,
                                       const std::vector<double>& nccf, const ObserveSetup& setup);

}  // namespace anno
