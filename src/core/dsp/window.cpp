#include "core/dsp/window.h"

#include <cmath>
#include <numbers>

namespace anno {

std::vector<double> make_window(WindowKind kind, int length) {
    std::vector<double> w(static_cast<std::size_t>(length > 0 ? length : 0), 1.0);
    if (length <= 1) {
        return w;
    }
    constexpr double k_two_pi = 2.0 * std::numbers::pi;
    for (int n = 0; n < length; ++n) {
        const double phase = k_two_pi * n / length;
        double value = 1.0;
        switch (kind) {
        case WindowKind::hann_periodic: value = 0.5 - 0.5 * std::cos(phase); break;
        case WindowKind::blackman_harris:
            value = 0.35875 - 0.48829 * std::cos(phase) + 0.14128 * std::cos(2.0 * phase) -
                    0.01168 * std::cos(3.0 * phase);
            break;
        }
        w[static_cast<std::size_t>(n)] = value;
    }
    return w;
}

}  // namespace anno
