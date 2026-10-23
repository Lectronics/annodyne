#include "tools/pitch_drift/pitch_drift.h"

#include <cmath>

namespace anno {

Result<EditSpec> drift_edit(const AnalysisResult& analysis, const NoteId& note, Ratio scale) {
    const Note* found = find_note(analysis, note);
    if (found == nullptr || !found->is_active) {
        return fail(ErrorCode::invalid_argument, "drift_edit: unknown or inactive note");
    }
    if (!std::isfinite(scale.value())) {
        return fail(ErrorCode::invalid_argument, "drift_edit: the scale must be finite");
    }
    EditSpec spec{.origin = "pitch_drift_tool",
                  .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                  .category = EditCategory::pitch_drift,
                  .param = EditParam::drift_scale,
                  .op = EditOp::set,
                  .value = scale};
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

}  // namespace anno
