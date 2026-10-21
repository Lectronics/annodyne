#include "tools/main_tool/main_tool.h"

#include <cmath>

namespace anno {

Result<EditSpec> pitch_drag_edit(const AnalysisResult& analysis, const PitchDrag& drag) {
    if (!std::isfinite(drag.target.value())) {
        return fail(ErrorCode::invalid_argument, "pitch_drag_edit: the target is not finite");
    }
    const Note* note = find_note(analysis, drag.note);
    if (note == nullptr || !note->is_active) {
        return fail(ErrorCode::invalid_argument, "pitch_drag_edit: unknown or inactive note");
    }
    const PitchTargetKind kind = note->pitch_target.kind;
    if (kind == PitchTargetKind::multi || kind == PitchTargetKind::unpitched) {
        return fail(ErrorCode::invalid_argument,
                    "pitch_drag_edit: the note has no single pitch target (split it or leave it)");
    }
    const Cents offset = drag.target - note->pitch_target.target;
    if (std::abs(offset.value()) > k_max_pitch_offset.value()) {
        return fail(ErrorCode::out_of_range,
                    "pitch_drag_edit: the move is larger than k_max_pitch_offset");
    }
    EditSpec spec{.origin = "main_tool",
                  .target = {.segment_ids = {segment_id(drag.note)}, .orphaned_segment_ids = {}},
                  .category = EditCategory::pitch,
                  .param = EditParam::center_offset,
                  .op = EditOp::set,
                  .value = offset};
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

}  // namespace anno
