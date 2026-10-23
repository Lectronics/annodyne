#pragma once

#include "core/synthesis/grain/period_curve.h"
#include "core/synthesis/grain/signal.h"
#include "core/synthesis/stretch/detail/geometry.h"
#include "core/synthesis/stretch/detail/vibrato.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace anno::stretch_detail {

/// TD-PSOLA time scaling of the output samples [begin, end) (absolute indices inside the span):
/// analysis marks over the source interval they play, synthesis marks one source period apart whose
/// first and last sit on analysis marks at their offsets from the run's ends (the first mark keeps
/// its distance from the run start, the last its distance from the run end, so the output joins an
/// adjoining slope-1 copy in phase; the spacing between is scaled to fit), each taking the
/// analysis mark nearest its source time, Hann grains overlap-added and divided by the window sum.
/// Samples no grain reaches are 0. Returns nullopt if the interval holds fewer than two analysis
/// marks (the caller sends it through the noise path). `period` is the source's pitch (analysis
/// marks, grain sizes); `output` the pitch the synthesis marks follow: the same curve re-timed, or
/// the slow part plus a regenerated vibrato (ADR-0023 amendment 3). Pure; thread-safe; not
/// real-time safe.
[[nodiscard]] std::optional<std::vector<double>>
synthesise_voiced_run(const grain::Signal& x, const grain::PeriodCurve& period,
                      const OutputPitch& output, const SpanGeometry& geometry, std::int64_t begin,
                      std::int64_t end);

}  // namespace anno::stretch_detail
