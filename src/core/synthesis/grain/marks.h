#pragma once

#include "core/synthesis/grain/period_curve.h"
#include "core/synthesis/grain/signal.h"

#include <vector>

namespace anno::grain {

constexpr double k_snap_halfwidth = 0.2;  // mark search window, fraction of a period

/// Analysis marks over [from, to] (fractional global samples): the first at the strongest sample
/// of one period, each next one placed where the waveform best matches the previous mark's, within
/// +-k_snap_halfwidth periods of one period later (the period at the previous mark), refined to a
/// fraction of a sample. A peak pick jumps between features of a real voice and misaligns the
/// grains it places (the hiccups, Phase 4 listening); matching the previous period keeps every
/// mark on the same feature. Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<double> analysis_marks(const Signal& x, const PeriodCurve& period,
                                                 double from, double to);

}  // namespace anno::grain
