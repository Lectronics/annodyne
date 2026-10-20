#pragma once

#include <vector>

// The one biquad of core/dsp: coefficients with a0 normalised to 1, run in direct form I in double.
// Used by k_weight and the zero-phase filters. Internal to the target (not for other modules).

namespace anno::detail {

struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

/// Filters `x` in place with zero initial state. Pure.
inline void run_biquad(const Biquad& q, std::vector<double>& x) {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    for (double& v : x) {
        const double y = q.b0 * v + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
        x2 = x1;
        x1 = v;
        y2 = y1;
        y1 = y;
        v = y;
    }
}

}  // namespace anno::detail
