#include "tools/pitch_modulation/pitch_modulation.h"

#include <cmath>

namespace anno {

Result<EditSpec> modulation_edit(const AnalysisResult& analysis, const NoteId& note, Ratio scale) {
    const Note* found = find_note(analysis, note);
    if (found == nullptr || !found->is_active) {
        return fail(ErrorCode::invalid_argument, "modulation_edit: unknown or inactive note");
    }
    if (!std::isfinite(scale.value())) {
        return fail(ErrorCode::invalid_argument, "modulation_edit: the scale must be finite");
    }
    EditSpec spec{.origin = "pitch_modulation_tool",
                  .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                  .category = EditCategory::pitch_mod,
                  .param = EditParam::mod_scale,
                  .op = EditOp::set,
                  .value = scale};
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

}  // namespace anno
