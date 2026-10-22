#include "core/synthesis/grain/period_curve.h"

#include "core/types/units.h"

#include <algorithm>

namespace anno::grain {

double PeriodCurve::at(double pos) const {
    const CentsPoint* lo = &points_.front();
    const CentsPoint* hi = &points_.back();
    double cents = 0.0;
    if (pos <= lo->position) {
        cents = lo->cents;
    } else if (pos >= hi->position) {
        cents = hi->cents;
    } else {
        const auto upper =
            std::ranges::upper_bound(points_, pos, std::ranges::less{}, &CentsPoint::position);
        const CentsPoint& b = *upper;
        const CentsPoint& a = *(upper - 1);
        const double w = (pos - a.position) / (b.position - a.position);
        cents = a.cents + w * (b.cents - a.cents);
    }
    const double period = rate_ / to_hz(Cents{cents}, k_a4_standard_hz).value();
    return std::clamp(period, k_min_period_s * rate_, k_max_period_s * rate_);
}

}  // namespace anno::grain
