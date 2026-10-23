#include "tools/fade/fade.h"

#include <cmath>

namespace anno {

namespace {

EditSpec spec_of(const NoteId& note, EditParam param, EditValue value) {
    return EditSpec{.origin = "fade_tool",
                    .target = {.segment_ids = {segment_id(note)}, .orphaned_segment_ids = {}},
                    .category = EditCategory::fade,
                    .param = param,
                    .op = EditOp::set,
                    .value = value};
}

}  // namespace

Result<std::vector<EditSpec>> fade_edits(const AnalysisResult& analysis, const Fade& fade) {
    const Note* note = find_note(analysis, fade.note);
    if (note == nullptr || !note->is_active) {
        return fail(ErrorCode::invalid_argument, "fade_edits: unknown or inactive note");
    }
    for (const Seconds d : {fade.in, fade.out}) {
        if (!std::isfinite(d.value()) || d.value() < 0.0) {
            return fail(ErrorCode::invalid_argument,
                        "fade_edits: a fade duration must be finite and >= 0");
        }
    }
    if (fade.shape != FadeShape::equal_power && fade.shape != FadeShape::linear &&
        fade.shape != FadeShape::log) {
        return fail(ErrorCode::invalid_argument, "fade_edits: unknown fade shape");
    }
    std::vector<EditSpec> specs{spec_of(fade.note, EditParam::fade_in, fade.in),
                                spec_of(fade.note, EditParam::fade_out, fade.out),
                                spec_of(fade.note, EditParam::fade_shape, fade.shape)};
    for (const EditSpec& spec : specs) {
        if (auto ok = validate_for_add(spec); !ok) {
            return Unexpected{ok.error()};
        }
    }
    return specs;
}

}  // namespace anno
