#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

// Fractional-position reader over a run of source samples (moved out of psola_engine.cpp,
// ADR-0023). Header-only, so the translation unit that includes it decides its floating-point
// contraction: grain, psola and stretch are all built with -ffp-contract=off.

namespace anno::grain {

/// The source samples around the zone with a way to read them at fractional positions.
class Signal {
public:
    Signal(std::span<const float> samples, std::int64_t first) : samples_{samples}, first_{first} {}

    [[nodiscard]] std::int64_t first() const { return first_; }
    [[nodiscard]] std::int64_t end() const {
        return first_ + static_cast<std::int64_t>(samples_.size());
    }

    [[nodiscard]] double at(std::int64_t global) const {
        const std::int64_t i = global - first_;
        return i < 0 || std::cmp_greater_equal(i, samples_.size())
                   ? 0.0
                   : static_cast<double>(samples_[static_cast<std::size_t>(i)]);
    }

    /// Catmull-Rom interpolation at a fractional global sample position.
    [[nodiscard]] double cubic(double pos) const {
        const double floor_pos = std::floor(pos);
        const auto i = static_cast<std::int64_t>(floor_pos);
        const double f = pos - floor_pos;
        const double p0 = at(i - 1);
        const double p1 = at(i);
        const double p2 = at(i + 1);
        const double p3 = at(i + 2);
        return p1 +
               0.5 * f *
                   (p2 - p0 +
                    f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
    }

private:
    std::span<const float> samples_;
    std::int64_t first_;
};

}  // namespace anno::grain
