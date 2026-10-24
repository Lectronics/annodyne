#include "macros/strip_silence/strip_silence.h"

#include <cmath>

namespace anno {

namespace {

Unexpected invalid(const std::string& what) {
    return fail(ErrorCode::invalid_argument, "strip_silence: " + what);
}

/// Total length of the gap's `silence` regions.
Seconds silence_of(const Gap& gap) {
    Seconds total{0.0};
    for (const GapRegion& r : gap.regions) {
        if (r.kind == GapRegionKind::silence) {
            total = total + (r.end - r.start);
        }
    }
    return total;
}

bool in_scope(const Gap& gap, StripScope scope) {
    return scope == StripScope::all_gaps || (gap.before.has_value() && gap.after.has_value());
}

}  // namespace

Result<std::vector<EditSpec>> strip_silence_edits(const AnalysisResult& analysis,
                                                  const StripSilenceParams& params) {
    if (!analysis.timeline) {
        return invalid("the analysis has no segment timeline");
    }
    if (!std::isfinite(params.min_silence.value()) || params.min_silence.value() < 0.0) {
        return invalid("min_silence must be finite and >= 0");
    }
    EditSpec spec;
    spec.origin = "strip_silence";
    spec.category = EditCategory::gap;
    spec.param = EditParam::gap_gain;
    spec.op = EditOp::set;
    spec.value = params.gain;
    for (const Gap& gap : analysis.timeline->gaps()) {
        if (in_scope(gap, params.scope) && silence_of(gap) >= params.min_silence) {
            spec.target.segment_ids.push_back(gap.id);
        }
    }
    if (spec.target.segment_ids.empty()) {
        // Still check the gain: a bad parameter is an error even when nothing qualifies.
        spec.target.segment_ids = {gap_segment_id(std::nullopt, std::nullopt)};
        if (auto r = validate(spec); !r) {
            return invalid(r.error().message);
        }
        return std::vector<EditSpec>{};
    }
    if (auto r = validate(spec); !r) {
        return invalid(r.error().message);
    }
    return std::vector<EditSpec>{std::move(spec)};
}

}  // namespace anno
