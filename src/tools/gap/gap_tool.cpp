#include "tools/gap/gap_tool.h"

#include "core/types/effective_gap.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr const char* k_origin = "gap_tool";

Unexpected invalid(const std::string& what) {
    return fail(ErrorCode::invalid_argument, "gap tool: " + what);
}

Result<void> check_gap(const AnalysisResult& analysis, const SegmentId& gap) {
    if (!analysis.timeline) {
        return invalid("the analysis has no segment timeline");
    }
    const auto gaps = analysis.timeline->gaps();
    if (gap.kind != SegmentKind::gap ||
        std::ranges::none_of(gaps, [&](const Gap& g) { return g.id == gap; })) {
        return invalid("'" + gap.value + "' is not a gap of the segment timeline");
    }
    return {};
}

EditSpec gain_spec(const SegmentId& gap, EditOp op, EditValue value) {
    EditSpec spec;
    spec.origin = k_origin;
    spec.target.segment_ids = {gap};
    spec.category = EditCategory::gap;
    spec.param = EditParam::gap_gain;
    spec.op = op;
    spec.value = value;
    return spec;
}

}  // namespace

Result<EditSpec> gap_gain_edit(const AnalysisResult& analysis, const GapGain& gain) {
    if (auto r = check_gap(analysis, gain.gap); !r) {
        return Unexpected{r.error()};
    }
    if (!std::isfinite(gain.gain.value())) {
        return invalid("the gain must be finite");
    }
    EditSpec spec =
        gain_spec(gain.gap, gain.op == GapGainOp::set ? EditOp::set : EditOp::add, gain.gain);
    if (auto r = validate(spec); !r) {  // a set outside [mute, +24 dB]
        return invalid(r.error().message);
    }
    return spec;
}

Result<EditSpec> gap_mute_edit(const AnalysisResult& analysis, const SegmentId& gap) {
    return gap_gain_edit(analysis, {.gap = gap, .gain = k_gap_mute_db, .op = GapGainOp::set});
}

Result<EditSpec> gap_unmute_edit(const AnalysisResult& analysis, const SegmentId& gap) {
    if (auto r = check_gap(analysis, gap); !r) {
        return Unexpected{r.error()};
    }
    return gain_spec(gap, EditOp::reset, EditValue{});
}

}  // namespace anno
