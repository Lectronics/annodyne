#pragma once

#include "core/types/analysis_result.h"
#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/units.h"

// Gap Tool (ROADMAP Phase 5): the edits a gain or mute gesture on a gap makes. Reads the analysis'
// segment timeline, never audio. Design: docs/design/phase5-gaps-core.md (slice 7d); ADR-0024.

namespace anno {

enum class GapGainOp { set, add };

/// A gain gesture on one gap: `gain` is the new gain (set) or a change (add), dB.
struct GapGain {
    SegmentId gap;  // a gap of the analysis' timeline
    Decibels gain;
    GapGainOp op = GapGainOp::set;
};

/// The edit that sets or changes the gain of the gap's `silence` regions: origin "gap_tool",
/// category gap, param gap_gain, the op as given. A set must lie in [k_gap_mute_db, +24 dB]; an add
/// is any finite change (the fold clamps the total once).
/// Errors: invalid_argument (the analysis has no segment timeline; the id is not a gap of it; a
/// non-finite gain; a set outside the range). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<EditSpec> gap_gain_edit(const AnalysisResult& analysis, const GapGain& gain);

/// The edit that mutes the gap: gap_gain set to k_gap_mute_db. Errors as gap_gain_edit.
[[nodiscard]] Result<EditSpec> gap_mute_edit(const AnalysisResult& analysis, const SegmentId& gap);

/// The edit that returns the gap to 0 dB: a reset of gap_gain (history keeps the earlier edits).
/// Errors as gap_gain_edit.
[[nodiscard]] Result<EditSpec> gap_unmute_edit(const AnalysisResult& analysis,
                                               const SegmentId& gap);

}  // namespace anno
