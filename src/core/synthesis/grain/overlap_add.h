#pragma once

#include "core/synthesis/grain/signal.h"

#include <cstdint>
#include <vector>

namespace anno::grain {

/// Adds one two-period Hann grain around analysis mark `a` (periods before and after it:
/// before/after, samples) at synthesis position `s` into acc and window_sum.
struct Grain {
    double analysis_mark;  // fractional global sample
    double synthesis_position;
    double before;
    double after;
    double output_period;  // the shifted period at this mark, samples
};

struct Accumulator {
    std::int64_t first;  // global sample of index 0
    std::vector<double> sum;
    std::vector<double> window_sum;
    std::vector<double> window_square_sum;  // sum of the squared windows (noise power gain)
    std::vector<double> output_period;  // of the latest grain covering the sample (0 where none)
};

/// Adds one two-period Hann grain of `x` around `g.analysis_mark` at `g.synthesis_position` into
/// `acc` (sum, window sum, squared window sum, latest output period). Samples outside the
/// accumulator are skipped. Pure apart from `acc`; thread-safe on separate accumulators.
void add_grain(const Signal& x, const Grain& g, Accumulator& acc);

}  // namespace anno::grain
