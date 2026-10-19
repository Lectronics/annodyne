#include "core/analysis/perceived_pitch/perceived_pitch.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace anno {

std::vector<double> stability_weights(std::span<const Cents> f0, Seconds hop,
                                      std::span<const Decibels> amp,
                                      const StabilityWeightParams& params) {
    const std::size_t n = f0.size();
    assert(amp.empty() || amp.size() == n);
    std::vector<double> w(n, 1.0);
    if (n == 0) {
        return w;
    }
    const double amp_max =
        amp.empty() ? 0.0 : std::ranges::max_element(amp, {}, &Decibels::value)->value();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i > 0 ? i - 1 : i;
        const std::size_t hi = i + 1 < n ? i + 1 : i;
        const double span_s = static_cast<double>(hi - lo) * hop.value();
        const double rate = span_s > 0.0 ? (f0[hi].value() - f0[lo].value()) / span_s : 0.0;
        const double z = rate / params.rate_scale_cents_per_s;
        w[i] = std::exp(-params.gamma * z * z);
        if (!amp.empty()) {
            const double below = amp_max - amp[i].value();
            w[i] *= std::clamp(1.0 - below / params.amp_range.value(), 0.0, 1.0);
        }
    }
    return w;
}

}  // namespace anno
