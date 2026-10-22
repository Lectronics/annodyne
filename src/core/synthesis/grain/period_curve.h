#pragma once

#include <cstddef>
#include <span>
#include <vector>

// Pitch period in samples along a series of (position, absolute pitch) points; the period limits
// and the cents interpolation PSOLA used, moved here unchanged (ADR-0023).

namespace anno::grain {

constexpr double k_min_period_s = 1.0 / 2000.0;  // pitch periods are kept inside 40..2000 Hz
constexpr double k_max_period_s = 1.0 / 40.0;

struct CentsPoint {
    double position = 0.0;  // global sample position of the point
    double cents = 0.0;     // absolute pitch (A4 = 6900)
};

/// Period in samples at position `pos`: cents interpolated linearly between the points, held
/// beyond the first and last, converted to a period at `rate` and kept inside the limits.
/// Pre: at least one point; positions increasing. Pure; thread-safe.
class PeriodCurve {
public:
    PeriodCurve(std::vector<CentsPoint> points, double rate)
        : points_{std::move(points)}, rate_{rate} {}

    [[nodiscard]] double at(double pos) const;

private:
    std::vector<CentsPoint> points_;
    double rate_;
};

}  // namespace anno::grain
