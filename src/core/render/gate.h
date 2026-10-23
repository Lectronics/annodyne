#pragma once

#include "core/types/effective_gap.h"
#include "core/types/result.h"
#include "core/types/segments.h"
#include "core/types/time_map.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <cstdint>
#include <vector>

// The gap gate: a per-sample gain for the `silence` regions of a gap with a gap_gain edit
// (ADR-0024 and its amendment 1; design: docs/design/phase5-gaps-core.md, "The gate").

namespace anno {

/// A `silence` region of a gap on the output axis, in whole samples [first, last), and which of its
/// edges ramp (the edge borders an ungated neighbour).
struct GatedSpan {
    std::int64_t first = 0;
    std::int64_t last = 0;
    bool ramp_start = false;
    bool ramp_end = false;
};

struct GateParams {
    Seconds gate_fade{0.005};  // >= 0; the ramp at a region edge that borders an ungated neighbour
};

/// The silence regions of `gap` through `map` at `rate`, in order. Errors: invalid_argument (a
/// region's start or end maps into a cut, or the rate is not finite and > 0). Pure; thread-safe.
[[nodiscard]] Result<std::vector<GatedSpan>> gated_spans(const Gap& gap, const TimeMap& map,
                                                         Hz rate);

/// The gain, one value per output sample of `range`, that `effective` applies to `gap`: 1 outside
/// every gated region; inside a `silence` region the linear gain of `effective.gain` (exactly 0 at
/// or below k_gap_mute_db), ramping linearly in gain over `gate_fade` at a region edge that borders
/// an ungated neighbour (a note, or a region of any other kind: tail, protected_preroll, audible,
/// sibilant, breath, plosive), never into the neighbour; no ramp at the recording's start or end
/// (a leading gap's start, a trailing gap's end). The ramp lies inside the region: the boundary
/// sample is 1, the gain reaches its value after `gate_fade`. A region shorter than 2 x gate_fade
/// ramps over half its length each side (one ramp edge: over at most its whole length). Only
/// `silence` regions are ever gated: every other kind is left alone. Region boundaries are the
/// gap's SourceTime range through `map` (to_output / to_output_end), snapped to whole samples at
/// `rate`; a block of the curve equals the same samples of any larger range's curve. If
/// `!effective.is_edited` the curve is all 1 (callers skip the multiplication then).
/// Errors: invalid_argument (gate_fade negative or not finite; rate not finite and > 0;
/// `effective.gap != gap.id`; a gated region whose start or end maps into a cut); out_of_range
/// (`range` outside [0, map.output_length()]). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<std::vector<float>> gate_curve(const Gap& gap, const EffectiveGap& effective,
                                                    const TimeMap& map, Hz rate, OutputRange range,
                                                    const GateParams& params);

}  // namespace anno
