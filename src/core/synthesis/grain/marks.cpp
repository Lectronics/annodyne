#include "core/synthesis/grain/marks.h"

#include <algorithm>
#include <cmath>

namespace anno::grain {

std::vector<double> analysis_marks(const Signal& x, const PeriodCurve& period, double from,
                                   double to) {
    std::vector<double> marks;
    const auto lo = static_cast<std::int64_t>(std::floor(from));
    const auto first_hi = lo + static_cast<std::int64_t>(std::ceil(period.at(from)));
    std::int64_t first = lo;
    for (std::int64_t n = lo; n <= first_hi; ++n) {
        if (x.at(n) > x.at(first)) {
            first = n;
        }
    }
    marks.push_back(static_cast<double>(first));
    const auto similarity = [&](double prev, double candidate, std::int64_t reach) {
        double dot = 0.0;
        double e_prev = 0.0;
        double e_cand = 0.0;
        for (std::int64_t k = -reach; k <= reach; ++k) {
            const double a = x.cubic(prev + static_cast<double>(k));
            const double b = x.cubic(candidate + static_cast<double>(k));
            dot += a * b;
            e_prev += a * a;
            e_cand += b * b;
        }
        return dot / std::sqrt(e_prev * e_cand + 1e-30);
    };
    while (true) {
        const double prev = marks.back();
        const double t = period.at(prev);
        const double nominal = prev + t;
        if (nominal > to) {
            break;
        }
        const auto half = static_cast<std::int64_t>(std::ceil(k_snap_halfwidth * t));
        const auto reach = static_cast<std::int64_t>(std::ceil(0.5 * t));
        const auto centre = static_cast<std::int64_t>(std::llround(nominal));
        std::int64_t best = centre;
        double best_score = -2.0;
        for (std::int64_t n = centre - half; n <= centre + half; ++n) {
            const double score = similarity(prev, static_cast<double>(n), reach);
            if (score > best_score) {
                best_score = score;
                best = n;
            }
        }
        // Parabola through the score at best-1, best, best+1 for the fractional position.
        const double left = similarity(prev, static_cast<double>(best - 1), reach);
        const double right = similarity(prev, static_cast<double>(best + 1), reach);
        const double curve = left - 2.0 * best_score + right;
        const double offset =
            curve < -1e-12 ? std::clamp(0.5 * (left - right) / curve, -0.5, 0.5) : 0.0;
        marks.push_back(static_cast<double>(best) + offset);
    }
    return marks;
}

}  // namespace anno::grain
