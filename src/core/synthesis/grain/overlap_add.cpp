#include "core/synthesis/grain/overlap_add.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace anno::grain {

void add_grain(const Signal& x, const Grain& g, Accumulator& acc) {
    const auto lo = static_cast<std::int64_t>(std::ceil(g.synthesis_position - g.before));
    const auto hi = static_cast<std::int64_t>(std::floor(g.synthesis_position + g.after));
    for (std::int64_t n = std::max(lo, acc.first);
         n <= hi && std::cmp_less(n - acc.first, acc.sum.size()); ++n) {
        const double u = static_cast<double>(n) - g.synthesis_position;
        const double span = u < 0.0 ? g.before : g.after;
        const double window = 0.5 * (1.0 + std::cos(std::numbers::pi * u / span));
        const auto idx = static_cast<std::size_t>(n - acc.first);
        acc.sum[idx] += window * x.cubic(g.analysis_mark + u);
        acc.window_sum[idx] += window;
        acc.window_square_sum[idx] += window * window;
        acc.output_period[idx] = g.output_period;  // the latest grain covering the sample
    }
}

}  // namespace anno::grain
