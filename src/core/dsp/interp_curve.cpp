#include "core/dsp/interp_curve.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace anno {

namespace {

double at_clamped(std::span<const double> v, std::int64_t i) {
    const auto last = static_cast<std::int64_t>(v.size()) - 1;
    return v[static_cast<std::size_t>(std::clamp<std::int64_t>(i, 0, last))];
}

double cubic(std::span<const double> v, std::int64_t i0, double frac) {
    // Lagrange through frames i0-1, i0, i0+1, i0+2; shifted inward when the stencil leaves the
    // curve.
    const auto n = static_cast<std::int64_t>(v.size());
    std::int64_t first = i0 - 1;
    if (n >= 4) {
        first = std::clamp<std::int64_t>(first, 0, n - 4);
    }
    const double x = static_cast<double>(i0 - first) + frac;  // position within the stencil 0..3
    double acc = 0.0;
    for (std::int64_t j = 0; j < 4; ++j) {
        double basis = 1.0;
        for (std::int64_t m = 0; m < 4; ++m) {
            if (m != j) {
                basis *= (x - static_cast<double>(m)) / static_cast<double>(j - m);
            }
        }
        acc += basis * at_clamped(v, first + j);
    }
    return acc;
}

}  // namespace

std::vector<double> interp_curve(std::span<const double> values, const FrameGrid& grid,
                                 std::span<const SourceTime> times, InterpKind kind) {
    std::vector<double> out(times.size(), std::numeric_limits<double>::quiet_NaN());
    if (values.empty()) {
        return out;
    }
    const auto last = static_cast<std::int64_t>(values.size()) - 1;
    for (std::size_t k = 0; k < times.size(); ++k) {
        const double pos = (times[k] - grid.start).value() / grid.hop.value();
        if (pos <= 0.0) {
            out[k] = values.front();
            continue;
        }
        if (pos >= static_cast<double>(last)) {
            out[k] = values.back();
            continue;
        }
        const auto i0 = static_cast<std::int64_t>(std::floor(pos));
        const double frac = pos - static_cast<double>(i0);
        const double a = at_clamped(values, i0);
        switch (kind) {
        case InterpKind::hold: out[k] = a; break;
        case InterpKind::linear:
            out[k] = frac == 0.0 ? a : a + frac * (at_clamped(values, i0 + 1) - a);
            break;
        case InterpKind::cubic: out[k] = frac == 0.0 ? a : cubic(values, i0, frac); break;
        }
    }
    return out;
}

}  // namespace anno
