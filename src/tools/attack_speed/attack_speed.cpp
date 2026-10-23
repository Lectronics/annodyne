#include "tools/attack_speed/attack_speed.h"

#include <cmath>

namespace anno {

Result<EditSpec> attack_speed_edit(const AnalysisResult& analysis, const AttackSpeed& speed) {
    const Note* note = find_note(analysis, speed.note);
    if (note == nullptr || !note->is_active) {
        return fail(ErrorCode::invalid_argument, "attack_speed_edit: unknown or inactive note");
    }
    if (!std::isfinite(speed.time_scale.value()) || speed.time_scale.value() <= 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "attack_speed_edit: the time scale must be finite and > 0");
    }
    if (!(note->source_start < note->attack_end)) {
        return fail(ErrorCode::invalid_argument, "attack_speed_edit: the note has no attack");
    }
    if (speed.time_scale < k_min_time_scale) {
        return fail(ErrorCode::out_of_range,
                    "attack_speed_edit: the scale is below the smallest time scale");
    }
    EditSpec spec{.origin = "attack_speed_tool",
                  .target = {.segment_ids = {segment_id(speed.note)}, .orphaned_segment_ids = {}},
                  .category = EditCategory::attack,
                  .param = EditParam::attack_time_scale,
                  .op = EditOp::set,
                  .value = speed.time_scale};
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

}  // namespace anno
