#include "tools/formant/formant.h"

#include <cmath>

namespace anno {

Result<EditSpec> formant_edit(const AnalysisResult& analysis, const NoteId& note, Cents offset) {
    const Note* found = find_note(analysis, note);
    if (found == nullptr || !found->is_active) {
        return fail(ErrorCode::invalid_argument, "formant_edit: unknown or inactive note");
    }
    if (!std::isfinite(offset.value())) {
        return fail(ErrorCode::invalid_argument, "formant_edit: the offset must be finite");
    }
    EditSpec spec{.origin = "formant_tool",
                  .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                  .category = EditCategory::formant,
                  .param = EditParam::formant_offset,
                  .op = EditOp::set,
                  .value = offset};
    if (auto ok = validate_for_add(spec); !ok) {
        return Unexpected{ok.error()};
    }
    return spec;
}

}  // namespace anno
